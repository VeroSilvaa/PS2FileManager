#ifndef PS2FILEMANAGER_CONSOLE_H
#define PS2FILEMANAGER_CONSOLE_H

/*
 * Camada de texto: substitui a debug screen da libdebug com as
 * mesmas funcoes (init_scr, scr_printf, scr_setXY, scr_clear...).
 * O texto fica numa grade de 80x28 (celulas de 8x16) que o gfx.c
 * desenha a cada quadro, sobre o fundo.
 */

#define CON_COLS 80
#define CON_ROWS 28

/*
 * Bytes Windows-1252 (o atlas da fonte cobre 32..255); 0 = vazio.
 */
extern unsigned char con_text[CON_ROWS][CON_COLS];

#endif
