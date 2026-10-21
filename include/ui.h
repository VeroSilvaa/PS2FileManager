#ifndef PS2FILEMANAGER_UI_H
#define PS2FILEMANAGER_UI_H

/*
 * Tema da interface grafica (docs/interface-640x448.md): cores,
 * regioes e as pecas comuns (painel, selecao, janela). Usado pelo
 * main.c, teclado virtual e configuracoes.
 */

#include "gfx.h"

/*
 * Cores (secao 4 do documento; alfa do GS 0..128).
 */
#define UI_PANEL        gfx_rgba(0x0B, 0x16, 0x30, 0x64)
#define UI_BORDER       gfx_rgba(0x2E, 0x5F, 0xB8, 0x80)
#define UI_TITLE_BAR    gfx_rgba(0x12, 0x30, 0x6E, 0x80)
#define UI_SEL_TOP      gfx_rgba(0x1E, 0x5B, 0xD8, 0x80)
#define UI_SEL_BOTTOM   gfx_rgba(0x2F, 0x7B, 0xFF, 0x80)
#define UI_SEL_DIM      gfx_rgba(0x16, 0x3B, 0x7A, 0x80)
#define UI_MARKED       gfx_rgba(0x16, 0x3B, 0x7A, 0x80)
#define UI_TEXT         gfx_rgba(0xE6, 0xEE, 0xFF, 0x80)
#define UI_TEXT2        gfx_rgba(0x9D, 0xB2, 0xD9, 0x80)
#define UI_TEXT_OFF     gfx_rgba(0x56, 0x62, 0x7A, 0x80)
#define UI_OK           gfx_rgba(0x3D, 0xDC, 0x84, 0x80)
#define UI_ERROR        gfx_rgba(0xE5, 0x48, 0x4D, 0x80)
#define UI_FOLDER       gfx_rgba(0xF2, 0xC1, 0x4E, 0x80)
#define UI_FILE         gfx_rgba(0xC8, 0xD4, 0xEE, 0x80)
#define UI_MUSIC        gfx_rgba(0xD9, 0x8C, 0xFF, 0x80)

#define UI_DIM          gfx_rgba(0x00, 0x00, 0x00, 0x40)
#define UI_WINDOW       gfx_rgba(0x0B, 0x16, 0x30, 0x78)
#define UI_WARNING      gfx_rgba(0xFF, 0xB0, 0x20, 0x80)

/*
 * Regioes (secao 2).
 */
#define UI_DEV_X        24
#define UI_DEV_W        176
#define UI_PANEL_Y      56
#define UI_PANEL_H      320
#define UI_FILES_X      208
#define UI_FILES_W      408
#define UI_ROW_H        22
#define UI_ROWS         12

/*
 * Painel com barra de titulo.
 */
void gui_panel(float x, float y, float w, float h, const char *title);

/*
 * Faixa do item escolhido (focused = 0: painel sem foco).
 */
void gui_selection(float x, float y, float w, float h, int focused);

/*
 * Escurece a tela e desenha a janela com titulo. Devolve o y onde
 * comeca o conteudo.
 */
float gui_window(float x, float y, float w, float h, const char *title);

#endif
