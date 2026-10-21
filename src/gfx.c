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

/*
 * PNGs embutidos (assets/, gerados por tools/make_assets.sh).
 */
extern unsigned char bg_png[];
extern unsigned int size_bg_png;

extern unsigned char font_mono_png[];
extern unsigned int size_font_mono_png;

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
 * Fonte: cinza -> RGBA branco com alfa (0..128 do GS). A cor do
 * texto vem da cor do sprite (modulacao).
 */
static int load_font(void)
{
    unsigned char *gray;
    unsigned char *rgba;
    int w;
    int h;
    int i;

    gray = png_decode(font_mono_png, size_font_mono_png, 1, &w, &h);

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

    font.Mem = (u32 *)rgba;
    font.Width = w;
    font.Height = h;
    font.PSM = GS_PSM_CT32;
    font.Filter = GS_FILTER_NEAREST;
    font.Vram = 0;

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

    if (load_font() < 0)
        printf("FM GFX: fonte nao carregou\n");

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

        static int logged = 0;
        unsigned int vram = gsKit_TexManager_bind(gs, &background);

        if (!logged) {
            printf("FM GFX: fundo na VRAM 0x%x\n", vram);
            logged = 1;
        }

        gsKit_prim_sprite_texture(gs, &background,
                                  0, 0, 0, 0,
                                  gs->Width, gs->Height,
                                  background.Width, background.Height,
                                  0, COLOR_WHITE);
    }

    /*
     * Painel translucido atras do texto (com borda), deixando o
     * fundo aparecer nas margens.
     */
    gsKit_prim_sprite(gs, 4, 4 * scale_y, SCREEN_W - 4, (SCREEN_H - 4) * scale_y, 1, COLOR_PANEL);

    if (font.Mem)
        draw_text_layer();

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
