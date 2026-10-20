#include <stdio.h>
#include <string.h>

#include <debug.h>
#include <libpad.h>

#include "osk.h"

/*
 * Paginas do teclado. Sem / \ : * ? " < > | (invalidos em nome
 * de arquivo no FAT/SMB). Espaco fica no R1.
 */
#define OSK_PAGES  3
#define OSK_ROWS   4

static const char *osk_pages[OSK_PAGES][OSK_ROWS] = {
    { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_.!", "0123456789" },
    { "abcdefghij", "klmnopqrst", "uvwxyz-_.!", "0123456789" },
    { "#$%&'()+,;", "=@[]^`{}~!", "-_.",        "0123456789" }
};

static const char *osk_page_names[OSK_PAGES] = {
    "ABC",
    "abc",
    "#+="
};

static char osk_title[64];
static char osk_buffer[OSK_MAX_TEXT + 1];
static int osk_length;
static int osk_max;
static int osk_page;
static int osk_row;
static int osk_col;


/*
 * Coluna dentro da linha atual (linhas podem ser mais curtas).
 */
static void osk_clamp_col(void)
{
    int width = strlen(osk_pages[osk_page][osk_row]);

    if (osk_col >= width)
        osk_col = width - 1;
}


static void osk_type(char c)
{
    if (osk_length >= osk_max)
        return;

    osk_buffer[osk_length++] = c;
    osk_buffer[osk_length] = '\0';
}


void osk_open(const char *title, const char *initial, int max_length)
{
    snprintf(osk_title, sizeof(osk_title), "%s", title ? title : "");

    if (max_length <= 0 || max_length > OSK_MAX_TEXT)
        max_length = OSK_MAX_TEXT;

    osk_max = max_length;

    snprintf(osk_buffer, (size_t)osk_max + 1, "%s", initial ? initial : "");
    osk_length = strlen(osk_buffer);

    osk_page = 0;
    osk_row = 0;
    osk_col = 0;
}


OskResult osk_update(unsigned int buttons)
{
    if (buttons & PAD_UP) {
        osk_row = osk_row > 0 ? osk_row - 1 : OSK_ROWS - 1;
        osk_clamp_col();
    }

    if (buttons & PAD_DOWN) {
        osk_row = osk_row < OSK_ROWS - 1 ? osk_row + 1 : 0;
        osk_clamp_col();
    }

    if (buttons & PAD_LEFT) {
        int width = strlen(osk_pages[osk_page][osk_row]);
        osk_col = osk_col > 0 ? osk_col - 1 : width - 1;
    }

    if (buttons & PAD_RIGHT) {
        int width = strlen(osk_pages[osk_page][osk_row]);
        osk_col = osk_col < width - 1 ? osk_col + 1 : 0;
    }

    if (buttons & PAD_CROSS)
        osk_type(osk_pages[osk_page][osk_row][osk_col]);

    if (buttons & PAD_R1)
        osk_type(' ');

    if ((buttons & PAD_SQUARE) && osk_length > 0)
        osk_buffer[--osk_length] = '\0';

    if (buttons & PAD_TRIANGLE) {
        osk_page = (osk_page + 1) % OSK_PAGES;
        osk_clamp_col();
    }

    if (buttons & PAD_START)
        return OSK_DONE;

    if (buttons & PAD_CIRCLE)
        return OSK_CANCEL;

    return OSK_EDITING;
}


void osk_draw(void)
{
    const char *row;
    int r;
    int c;
    int p;


    scr_printf("PS2FileManager\n");
    scr_printf("===============\n");
    scr_printf("%.70s\n\n", osk_title);

    scr_printf("Nome: %s_\n", osk_buffer);
    scr_printf("      (%d de %d caracteres)\n\n", osk_length, osk_max);


    for (r = 0; r < OSK_ROWS; r++) {

        row = osk_pages[osk_page][r];

        scr_printf("   ");

        for (c = 0; row[c]; c++) {

            if (r == osk_row && c == osk_col)
                scr_printf("[%c]", row[c]);
            else
                scr_printf(" %c ", row[c]);
        }

        scr_printf("\n\n");
    }


    scr_printf("   Teclado:");

    for (p = 0; p < OSK_PAGES; p++)
        scr_printf(p == osk_page ? " [%s]" : "  %s ", osk_page_names[p]);

    scr_printf("\n\n");

    scr_printf("X digitar | QUADRADO apagar | R1 espaco\n");
    scr_printf("TRIANGULO maiusc/minusc/simbolos\n");
    scr_printf("START confirmar | O cancelar\n");
}


const char *osk_text(void)
{
    return osk_buffer;
}
