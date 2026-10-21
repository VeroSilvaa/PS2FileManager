#ifndef PS2FILEMANAGER_GFX_H
#define PS2FILEMANAGER_GFX_H

/*
 * Interface grafica (Etapa 10) com gsKit. Coordenadas sempre em
 * 640x448 (em PAL o gfx estica para 512 linhas).
 *
 * Telas ainda nao convertidas: o texto do console.c (scr_printf)
 * sobre o fundo. Telas convertidas: gfx_set_screen(desenho) e a
 * funcao desenha a cada quadro com as primitivas abaixo.
 */

#define GFX_W 640
#define GFX_H 448

/*
 * Cor do GS (alfa 0..128 = opaco).
 */
typedef unsigned long long gfx_color;

gfx_color gfx_rgba(int r, int g, int b, int a);

int gfx_init(void);

/*
 * Desenha um quadro e troca o buffer. Espera o VSync.
 */
void gfx_frame(void);

/*
 * Como gfx_frame, mas no maximo ~20 vezes por segundo (para telas
 * de progresso que escrevem muito).
 */
void gfx_frame_throttled(void);

/*
 * Tela convertida: 'draw' desenha o quadro inteiro (depois do
 * fundo). NULL volta para a camada de texto.
 */
void gfx_set_screen(void (*draw)(void));

/* Primitivas (so dentro da funcao de desenho da tela) */

void gfx_rect(float x, float y, float w, float h, gfx_color color);

/*
 * Gradiente vertical (top -> bottom).
 */
void gfx_rect_vgradient(float x, float y, float w, float h, gfx_color top, gfx_color bottom);

/*
 * Borda de 'thick' pixels por dentro do retangulo.
 */
void gfx_border(float x, float y, float w, float h, int thick, gfx_color color);

/*
 * Texto proporcional (Noto Sans 14). y = topo da linha (20 px).
 * Devolve a largura em pixels.
 */
int gfx_text(float x, float y, const char *text, gfx_color color);

int gfx_text_width(const char *text);

/*
 * Corta com "..." para caber em max_w.
 */
void gfx_text_fit(float x, float y, float max_w, const char *text, gfx_color color);

void gfx_text_right(float right, float y, const char *text, gfx_color color);

/*
 * Logo "PS2FileManager" (Orbitron, assets/logo.png), ~218x34.
 */
void gfx_logo(float x, float y);

void gfx_line(float x1, float y1, float x2, float y2, gfx_color color);

void gfx_triangle(float x1, float y1, float x2, float y2, float x3, float y3, gfx_color color);

/*
 * Botoes do controle (icone 18x18 a partir de x,y).
 */
typedef enum {
    GFX_BTN_CROSS = 0,
    GFX_BTN_CIRCLE,
    GFX_BTN_SQUARE,
    GFX_BTN_TRIANGLE,
    GFX_BTN_L1,
    GFX_BTN_R1,
    GFX_BTN_SELECT,
    GFX_BTN_START,
    GFX_BTN_UP,
    GFX_BTN_DOWN,
    GFX_BTN_LEFT,
    GFX_BTN_RIGHT,
    GFX_BTN_L2,
    GFX_BTN_R2
} GfxButton;

/*
 * Icone + rotulo; devolve a largura total (para o proximo par).
 */
int gfx_button_hint(float x, float y, GfxButton button, const char *label);

#endif
