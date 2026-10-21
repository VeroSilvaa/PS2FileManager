#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

#include <tamtypes.h>
#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>

#include "gfx.h"
#include "console.h"
#include "pngload.h"
#include "font_sans_14.h"

/*
 * PNGs embutidos (assets/, gerados por tools/make_assets.sh).
 */
extern unsigned char bg_png[];
extern unsigned int size_bg_png;

extern unsigned char font_mono_png[];
extern unsigned int size_font_mono_png;

extern unsigned char font_sans_png[];
extern unsigned int size_font_sans_png;

extern unsigned char logo_png[];
extern unsigned int size_logo_png;

#define SCREEN_W        640
#define SCREEN_H        448

#define CELL_W          8
#define CELL_H          16
#define FONT_FIRST      32
#define FONT_PER_ROW    16

/*
 * Cores (docs/interface-640x448.md, alfa do GS 0..128).
 */
#define COLOR_TEXT      GS_SETREG_RGBAQ(0xE6, 0xEE, 0xFF, 0x80, 0x00)
#define COLOR_PANEL     GS_SETREG_RGBAQ(0x05, 0x0B, 0x1E, 0x58, 0x00)
#define COLOR_CLEAR     GS_SETREG_RGBAQ(0x05, 0x0B, 0x1E, 0x80, 0x00)
#define COLOR_WHITE     GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00)

static GSGLOBAL *gs;
static GSTEXTURE background;
static GSTEXTURE font;
static GSTEXTURE sans;
static GSTEXTURE logo;

/*
 * Tela convertida (desenha o quadro) ou NULL (camada de texto).
 */
static void (*screen_draw)(void) = NULL;

static int ready = 0;
static float scale_y = 1.0f;        /* PAL: 512 linhas */
static clock_t last_frame = 0;


/*
 * Fundo: RGBA (CT32) com alfa 0x80 (opaco no GS). Em CT24 o gsKit
 * nao desenhava nada (e na VRAM os dois ocupam 4 bytes/pixel).
 */
static int load_background(void)
{
    unsigned char *pixels;
    int w;
    int h;
    int i;

    pixels = png_decode(bg_png, size_bg_png, 4, &w, &h);

    if (!pixels)
        return -1;

    for (i = 0; i < w * h; i++)
        pixels[i * 4 + 3] = 0x80;

    background.Mem = (u32 *)pixels;
    background.Width = w;
    background.Height = h;
    background.PSM = GS_PSM_CT32;
    background.Filter = GS_FILTER_LINEAR;
    background.Vram = 0;

    return 0;
}


/*
 * Imagem RGBA com transparencia (alfa 0..255 -> 0..128 do GS).
 */
static int load_rgba(const unsigned char *png, unsigned int size, GSTEXTURE *tex)
{
    unsigned char *pixels;
    int w;
    int h;
    int i;

    pixels = png_decode(png, size, 4, &w, &h);

    if (!pixels)
        return -1;

    for (i = 0; i < w * h; i++)
        pixels[i * 4 + 3] = (unsigned char)((pixels[i * 4 + 3] + 1) >> 1);

    tex->Mem = (u32 *)pixels;
    tex->Width = w;
    tex->Height = h;
    tex->PSM = GS_PSM_CT32;
    tex->Filter = GS_FILTER_NEAREST;
    tex->Vram = 0;

    return 0;
}


/*
 * Atlas de fonte: cinza -> RGBA branco com alfa (0..128 do GS). A
 * cor do texto vem da cor do sprite (modulacao).
 */
static int load_font(const unsigned char *png, unsigned int size, GSTEXTURE *tex)
{
    unsigned char *gray;
    unsigned char *rgba;
    int w;
    int h;
    int i;

    gray = png_decode(png, size, 1, &w, &h);

    if (!gray)
        return -1;

    rgba = memalign(128, w * h * 4);

    if (!rgba) {
        free(gray);
        return -1;
    }

    for (i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = 0xFF;
        rgba[i * 4 + 1] = 0xFF;
        rgba[i * 4 + 2] = 0xFF;
        rgba[i * 4 + 3] = (unsigned char)((gray[i] + 1) >> 1);
    }

    free(gray);

    tex->Mem = (u32 *)rgba;
    tex->Width = w;
    tex->Height = h;
    tex->PSM = GS_PSM_CT32;
    tex->Filter = GS_FILTER_NEAREST;
    tex->Vram = 0;

    return 0;
}


int gfx_init(void)
{
    if (ready)
        return 0;

    gs = gsKit_init_global();

    /*
     * Buffers de tela em 16 bits (com dithering): 2 x 640x512 em 24
     * bits ocupam 2,6 MB dos 4 MB de VRAM e o fundo (1,15 MB) nao
     * cabia.
     */
    gs->PSM = GS_PSM_CT16S;
    gs->Dithering = GS_SETTING_ON;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAlphaEnable = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    /*
     * Transparencia: Cs*As + Cd*(1-As).
     */
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(gs, GS_ZTEST_OFF);
    gsKit_set_clamp(gs, GS_CMODE_CLAMP);

    scale_y = (float)gs->Height / SCREEN_H;

    printf("FM GFX: %dx%d (%s), interlace %d, field %d\n",
           gs->Width, gs->Height,
           gs->Mode == GS_MODE_PAL ? "PAL" : "NTSC",
           gs->Interlace, gs->Field);

    if (load_background() < 0)
        printf("FM GFX: fundo nao carregou\n");

    if (load_font(font_mono_png, size_font_mono_png, &font) < 0)
        printf("FM GFX: fonte mono nao carregou\n");

    if (load_font(font_sans_png, size_font_sans_png, &sans) < 0)
        printf("FM GFX: fonte sans nao carregou\n");

    if (load_rgba(logo_png, size_logo_png, &logo) < 0)
        printf("FM GFX: logo nao carregou\n");

    /*
     * Em PAL a imagem e esticada na vertical: filtro suave.
     */
    logo.Filter = GS_FILTER_LINEAR;

    ready = 1;

    gfx_frame();

    return 0;
}


static void draw_text_layer(void)
{
    int row;
    int col;

    gsKit_TexManager_bind(gs, &font);

    for (row = 0; row < CON_ROWS; row++) {

        float y = row * CELL_H * scale_y;

        for (col = 0; col < CON_COLS; col++) {

            unsigned char c = con_text[row][col];
            float x = col * CELL_W;
            int index;
            float u;
            float v;

            if (c <= ' ')
                continue;

            index = c - FONT_FIRST;
            u = (index % FONT_PER_ROW) * CELL_W;
            v = (index / FONT_PER_ROW) * CELL_H;

            gsKit_prim_sprite_texture(gs, &font,
                                      x, y, u, v,
                                      x + CELL_W, y + CELL_H * scale_y,
                                      u + CELL_W, v + CELL_H,
                                      2, COLOR_TEXT);
        }
    }
}


void gfx_frame(void)
{
    if (!ready)
        return;

    gsKit_clear(gs, COLOR_CLEAR);

    if (background.Mem) {

        gsKit_TexManager_bind(gs, &background);

        gsKit_prim_sprite_texture(gs, &background,
                                  0, 0, 0, 0,
                                  gs->Width, gs->Height,
                                  background.Width, background.Height,
                                  0, COLOR_WHITE);
    }

    if (screen_draw) {

        screen_draw();

    } else {

        /*
         * Camada de texto (telas ainda nao convertidas): painel
         * translucido atras, o fundo aparece nas margens.
         */
        gsKit_prim_sprite(gs, 4, 4 * scale_y, SCREEN_W - 4, (SCREEN_H - 4) * scale_y, 1, COLOR_PANEL);

        if (font.Mem)
            draw_text_layer();
    }

    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
    gsKit_TexManager_nextFrame(gs);

    last_frame = clock();
}


void gfx_frame_throttled(void)
{
    if (clock() - last_frame >= CLOCKS_PER_SEC / 20)
        gfx_frame();
}


void gfx_set_screen(void (*draw)(void))
{
    screen_draw = draw;
}


/* ============================================================
 * Primitivas (coordenadas 640x448)
 * ============================================================ */

gfx_color gfx_rgba(int r, int g, int b, int a)
{
    return GS_SETREG_RGBAQ(r, g, b, a, 0x00);
}


void gfx_rect(float x, float y, float w, float h, gfx_color color)
{
    gsKit_prim_sprite(gs, x, y * scale_y, x + w, (y + h) * scale_y, 1, color);
}


void gfx_rect_vgradient(float x, float y, float w, float h, gfx_color top, gfx_color bottom)
{
    gsKit_prim_quad_gouraud(gs,
                            x, y * scale_y,
                            x + w, y * scale_y,
                            x, (y + h) * scale_y,
                            x + w, (y + h) * scale_y,
                            1, top, top, bottom, bottom);
}


void gfx_border(float x, float y, float w, float h, int thick, gfx_color color)
{
    gfx_rect(x, y, w, thick, color);
    gfx_rect(x, y + h - thick, w, thick, color);
    gfx_rect(x, y + thick, thick, h - 2 * thick, color);
    gfx_rect(x + w - thick, y + thick, thick, h - 2 * thick, color);
}


static int char_width(unsigned char c)
{
    if (c < FONT_FIRST)
        return 0;

    return font_sans_width[c - FONT_FIRST];
}


/*
 * Proximo caractere como byte Windows-1252 (o atlas). Textos do
 * programa sao UTF-8 (os .c); nomes de arquivo que nao forem UTF-8
 * valido passam byte a byte. Avanca *text.
 */
static unsigned char next_char(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned int code;

    if (p[0] >= 0xC2 && p[0] <= 0xDF && (p[1] & 0xC0) == 0x80) {

        code = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        *text += 2;

        return code <= 0xFF ? (unsigned char)code : '?';
    }

    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {

        code = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *text += 3;

        switch (code) {
            case 0x2013: return 0x96;   /* travessao curto */
            case 0x2014: return 0x97;
            case 0x2018: return 0x91;
            case 0x2019: return 0x92;
            case 0x201C: return 0x93;
            case 0x201D: return 0x94;
            case 0x2022: return 0x95;   /* marcador */
            case 0x2026: return 0x85;   /* reticencias */
            case 0x20AC: return 0x80;   /* euro */
        }

        return '?';
    }

    *text += 1;

    return p[0];
}


int gfx_text_width(const char *text)
{
    int width = 0;

    while (*text)
        width += char_width(next_char(&text));

    return width;
}


/*
 * Desenha ate 'length' bytes de text (sem limite: -1).
 */
static int draw_sans(float x, float y, const char *text, int length, gfx_color color)
{
    const char *end = length < 0 ? NULL : text + length;
    float start = x;

    if (!sans.Mem)
        return 0;

    gsKit_TexManager_bind(gs, &sans);

    while (*text && (!end || text < end)) {

        unsigned char c = next_char(&text);
        int width = char_width(c);
        int index;
        float u;
        float v;

        if (c > ' ') {

            index = c - FONT_FIRST;
            u = (index % FONT_PER_ROW) * FONT_SANS_CELL_W;
            v = (index / FONT_PER_ROW) * FONT_SANS_CELL_H;

            /*
             * A celula tem 1 px de margem a esquerda (make_assets.py).
             */
            gsKit_prim_sprite_texture(gs, &sans,
                                      x - 1, y * scale_y, u, v,
                                      x - 1 + FONT_SANS_CELL_W, (y + FONT_SANS_CELL_H) * scale_y,
                                      u + FONT_SANS_CELL_W, v + FONT_SANS_CELL_H,
                                      2, color);
        }

        x += width;
    }

    return (int)(x - start);
}


int gfx_text(float x, float y, const char *text, gfx_color color)
{
    return draw_sans(x, y, text, -1, color);
}


void gfx_text_fit(float x, float y, float max_w, const char *text, gfx_color color)
{
    static const char ellipsis[] = "\x85";      /* ... em Windows-1252 */
    int ellipsis_w = char_width(0x85);
    const char *p = text;
    int width = 0;

    if (gfx_text_width(text) <= max_w) {
        draw_sans(x, y, text, -1, color);
        return;
    }

    while (*p) {

        const char *next = p;
        int w = char_width(next_char(&next));

        if (width + w + ellipsis_w > max_w)
            break;

        width += w;
        p = next;
    }

    draw_sans(x, y, text, (int)(p - text), color);
    draw_sans(x + width, y, ellipsis, -1, color);
}


void gfx_text_right(float right, float y, const char *text, gfx_color color)
{
    draw_sans(right - gfx_text_width(text), y, text, -1, color);
}


void gfx_logo(float x, float y)
{
    if (!logo.Mem)
        return;

    gsKit_TexManager_bind(gs, &logo);

    gsKit_prim_sprite_texture(gs, &logo,
                              x, y * scale_y, 0, 0,
                              x + logo.Width, (y + logo.Height) * scale_y,
                              logo.Width, logo.Height,
                              2, COLOR_WHITE);
}


void gfx_line(float x1, float y1, float x2, float y2, gfx_color color)
{
    gsKit_prim_line(gs, x1, y1 * scale_y, x2, y2 * scale_y, 1, color);
}


void gfx_triangle(float x1, float y1, float x2, float y2, float x3, float y3, gfx_color color)
{
    gsKit_prim_triangle(gs, x1, y1 * scale_y, x2, y2 * scale_y, x3, y3 * scale_y, 1, color);
}


/*
 * Linha "grossa" (2 px): evita a cintilacao do entrelacado.
 */
static void thick_line(float x1, float y1, float x2, float y2, gfx_color color)
{
    gfx_line(x1, y1, x2, y2, color);
    gfx_line(x1 + 1, y1, x2 + 1, y2, color);
    gfx_line(x1, y1 + 1, x2, y2 + 1, color);
}


int gfx_button_hint(float x, float y, GfxButton button, const char *label)
{
    static const char *names[] = { "", "", "", "", "L1", "R1", "SELECT", "START" };
    gfx_color text = gfx_rgba(0xE6, 0xEE, 0xFF, 0x80);
    gfx_color dark = gfx_rgba(0x0B, 0x16, 0x30, 0x80);
    float icon_w = 18;

    switch (button) {

        case GFX_BTN_CROSS: {
            gfx_color c = gfx_rgba(0x7C, 0xB2, 0xFF, 0x80);
            thick_line(x + 3, y + 4, x + 14, y + 15, c);
            thick_line(x + 14, y + 4, x + 3, y + 15, c);
            break;
        }

        case GFX_BTN_CIRCLE: {
            gfx_color c = gfx_rgba(0xFF, 0x6B, 0x6B, 0x80);
            gfx_border(x + 3, y + 3, 13, 13, 2, c);
            gfx_rect(x + 3, y + 3, 2, 2, dark);         /* cantos "arredondados" */
            gfx_rect(x + 14, y + 3, 2, 2, dark);
            gfx_rect(x + 3, y + 14, 2, 2, dark);
            gfx_rect(x + 14, y + 14, 2, 2, dark);
            break;
        }

        case GFX_BTN_SQUARE:
            gfx_border(x + 3, y + 3, 13, 13, 2, gfx_rgba(0xF2, 0x8C, 0xE6, 0x80));
            break;

        case GFX_BTN_TRIANGLE: {
            gfx_color c = gfx_rgba(0x5C, 0xE6, 0xB0, 0x80);
            thick_line(x + 9, y + 3, x + 2, y + 15, c);
            thick_line(x + 9, y + 3, x + 16, y + 15, c);
            thick_line(x + 2, y + 15, x + 16, y + 15, c);
            break;
        }

        default:
            /*
             * L1/R1/SELECT/START: rotulo numa caixa.
             */
            icon_w = gfx_text_width(names[button]) + 8;
            gfx_rect(x, y + 1, icon_w, 17, gfx_rgba(0xC8, 0xD4, 0xEE, 0x70));
            gfx_text(x + 4, y - 1, names[button], dark);
            break;
    }

    return (int)(icon_w + 4 + gfx_text(x + icon_w + 4, y - 1, label, text) + 16);
}
