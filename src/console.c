#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <tamtypes.h>
#include <debug.h>

#include "console.h"
#include "gfx.h"

/*
 * Mesmas funcoes da debug screen (libdebug, que nao e mais
 * ligada): as telas antigas continuam funcionando, agora sobre o
 * fundo grafico.
 */

unsigned char con_text[CON_ROWS][CON_COLS];

static int cursor_x;
static int cursor_y;


static void con_newline(void)
{
    cursor_x = 0;
    cursor_y++;

    /*
     * A debug screen voltava ao topo depois da ultima linha.
     */
    if (cursor_y >= CON_ROWS)
        cursor_y = 0;
}


static void con_put(unsigned char c)
{
    if (c == '\n') {
        con_newline();
        return;
    }

    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    if (c == '\t') {
        do {
            con_put(' ');
        } while (cursor_x % 8);
        return;
    }

    if (cursor_x >= CON_COLS)
        con_newline();

    con_text[cursor_y][cursor_x++] = c;
}


void init_scr(void)
{
    scr_clear();

    gfx_init();
}


void scr_vprintf(const char *format, va_list args)
{
    char buffer[1024];
    int i;

    vsnprintf(buffer, sizeof(buffer), format, args);

    for (i = 0; buffer[i]; i++)
        con_put((unsigned char)buffer[i]);

    /*
     * Telas de progresso escrevem sem voltar ao laco principal.
     */
    gfx_frame_throttled();
}


void scr_printf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    scr_vprintf(format, args);
    va_end(args);
}


void scr_setXY(int x, int y)
{
    if (x >= 0 && x < CON_COLS)
        cursor_x = x;

    if (y >= 0 && y < CON_ROWS)
        cursor_y = y;
}


int scr_getX(void)
{
    return cursor_x;
}


int scr_getY(void)
{
    return cursor_y;
}


void scr_clear(void)
{
    memset(con_text, 0, sizeof(con_text));

    cursor_x = 0;
    cursor_y = 0;

    /*
     * Quem limpa a tela vai escrever nela (progresso, conexao...):
     * mostra a camada de texto ate o laco principal escolher a
     * tela grafica de novo.
     */
    gfx_set_screen(NULL);
}
