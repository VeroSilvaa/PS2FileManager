#include <stdio.h>
#include <string.h>
#include <time.h>

#include <debug.h>
#include <libpad.h>

#include "osk.h"
#include "ui.h"

/*
 * Paginas do teclado (texto UTF-8). Sem / \ : * ? " < > | (invalidos
 * em nome de arquivo no FAT/SMB). Acentos: so onde o destino aceita
 * (fora do Memory Card, das particoes APA e dos campos de rede).
 */
#define OSK_PAGES  4
#define OSK_ROWS   4

/*
 * Linha das acoes, abaixo das letras.
 */
#define OSK_ACTION_ROW  OSK_ROWS

enum {
    OSK_ACTION_PAGE = 0,
    OSK_ACTION_SPACE,
    OSK_ACTION_DELETE,
    OSK_ACTION_CLEAR,
    OSK_ACTION_DONE
};

#define OSK_MAX_ACTIONS 4

/*
 * Acoes de cada modo (a linha de baixo).
 */
static const int text_actions[] = { OSK_ACTION_PAGE, OSK_ACTION_SPACE, OSK_ACTION_DELETE, OSK_ACTION_DONE };
static const int numeric_actions[] = { OSK_ACTION_DELETE, OSK_ACTION_CLEAR, OSK_ACTION_DONE };

/*
 * Teclado numerico (enderecos IP).
 */
static const char *numeric_rows[OSK_ROWS] = { "123", "456", "789", ".0" };

#define OSK_PAGE_ACCENTS  3

static const char *osk_pages[OSK_PAGES][OSK_ROWS] = {
    { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_.!", "0123456789" },
    { "abcdefghij", "klmnopqrst", "uvwxyz-_.!", "0123456789" },
    { "#$%&'()+,;", "=@[]^`{}~!", "-_.",        "0123456789" },
    { "áàâãéêíóôõ", "úüçñèìòùäö", "ÁÀÂÃÉÊÍÓÔÕ", "ÚÜÇÑºª-_.!" }
};

static const char *osk_page_names[OSK_PAGES] = {
    "ABC",
    "abc",
    "#+=",
    "áéç"
};

static char osk_title[64];
static char osk_buffer[OSK_MAX_TEXT + 1];
static int osk_length;      /* bytes */
static int osk_max;
static int osk_page;
static int osk_row;
static int osk_col;
static int osk_accents;     /* pagina de acentos disponivel */
static int osk_numeric;     /* so numeros e ponto */


static const char *page_row(int row)
{
    return osk_numeric ? numeric_rows[row] : osk_pages[osk_page][row];
}


static int action_count(void)
{
    return osk_numeric ? 3 : 4;
}


static int action_at(int col)
{
    return osk_numeric ? numeric_actions[col] : text_actions[col];
}


/* ------------------------------------------------------------
 * UTF-8: as linhas tem caracteres de 1 ou 2 bytes
 * ------------------------------------------------------------ */

static int utf8_bytes(unsigned char c)
{
    if (c < 0x80)
        return 1;

    if ((c & 0xE0) == 0xC0)
        return 2;

    if ((c & 0xF0) == 0xE0)
        return 3;

    return 4;
}


static int row_chars(const char *row)
{
    int count = 0;

    while (*row) {
        row += utf8_bytes((unsigned char)*row);
        count++;
    }

    return count;
}


/*
 * Caractere 'index' da linha, copiado (com terminador) para 'out'.
 */
static void row_char(const char *row, int index, char *out)
{
    int bytes;

    while (*row && index > 0) {
        row += utf8_bytes((unsigned char)*row);
        index--;
    }

    bytes = *row ? utf8_bytes((unsigned char)*row) : 0;

    memcpy(out, row, bytes);
    out[bytes] = '\0';
}


static int row_width(int row)
{
    if (row == OSK_ACTION_ROW)
        return action_count();

    return row_chars(page_row(row));
}


static void osk_clamp_col(void)
{
    int width = row_width(osk_row);

    if (osk_col >= width)
        osk_col = width - 1;
}


static void osk_type(const char *text)
{
    int bytes = strlen(text);

    if (osk_length + bytes > osk_max)
        return;

    memcpy(osk_buffer + osk_length, text, bytes);
    osk_length += bytes;
    osk_buffer[osk_length] = '\0';
}


/*
 * Apaga o ultimo caractere (todos os bytes dele).
 */
static void osk_delete(void)
{
    while (osk_length > 0) {

        unsigned char c = (unsigned char)osk_buffer[--osk_length];

        if ((c & 0xC0) != 0x80)
            break;
    }

    osk_buffer[osk_length] = '\0';
}


static void osk_next_page(void)
{
    int pages = osk_accents ? OSK_PAGES : OSK_PAGE_ACCENTS;

    if (osk_numeric)
        return;

    osk_page = (osk_page + 1) % pages;

    if (osk_row != OSK_ACTION_ROW)
        osk_clamp_col();
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
    osk_accents = 1;
    osk_numeric = 0;
}


void osk_numeric_only(void)
{
    osk_numeric = 1;
    osk_row = 0;
    osk_col = 0;
}


void osk_ascii_only(void)
{
    osk_accents = 0;

    if (osk_page == OSK_PAGE_ACCENTS)
        osk_page = 0;
}


void osk_focus_digits(void)
{
    osk_row = OSK_ROWS - 1;
    osk_col = 0;
}


OskResult osk_update(unsigned int buttons)
{
    if (buttons & (PAD_UP | PAD_DOWN)) {

        int old_width = row_width(osk_row);

        if (buttons & PAD_UP)
            osk_row = osk_row > 0 ? osk_row - 1 : OSK_ACTION_ROW;
        else
            osk_row = osk_row < OSK_ACTION_ROW ? osk_row + 1 : 0;

        /*
         * Entre as letras (10) e as acoes (4): mantem a posicao
         * proporcional.
         */
        if (old_width != row_width(osk_row))
            osk_col = osk_col * row_width(osk_row) / old_width;

        osk_clamp_col();
    }

    if (buttons & PAD_LEFT) {
        int width = row_width(osk_row);
        osk_col = osk_col > 0 ? osk_col - 1 : width - 1;
    }

    if (buttons & PAD_RIGHT) {
        int width = row_width(osk_row);
        osk_col = osk_col < width - 1 ? osk_col + 1 : 0;
    }

    if (buttons & PAD_CROSS) {

        if (osk_row == OSK_ACTION_ROW) {

            switch (action_at(osk_col)) {
                case OSK_ACTION_PAGE:   osk_next_page(); break;
                case OSK_ACTION_SPACE:  osk_type(" "); break;
                case OSK_ACTION_DELETE: osk_delete(); break;
                case OSK_ACTION_CLEAR:  osk_length = 0; osk_buffer[0] = '\0'; break;
                default:                return OSK_DONE;
            }

        } else {

            char key[8];

            row_char(page_row(osk_row), osk_col, key);
            osk_type(key);
        }
    }

    if ((buttons & PAD_R1) && !osk_numeric)
        osk_type(" ");

    if (buttons & PAD_SQUARE)
        osk_delete();

    if (buttons & PAD_TRIANGLE)
        osk_next_page();

    if (buttons & PAD_START)
        return OSK_DONE;

    if (buttons & PAD_CIRCLE)
        return OSK_CANCEL;

    return OSK_EDITING;
}


/*
 * Camada de texto (telas ainda nao convertidas).
 */
void osk_draw(void)
{
    char key[8];
    int r;
    int c;
    int p;


    scr_printf("PS2FileManager\n");
    scr_printf("===============\n");
    scr_printf("%.70s\n\n", osk_title);

    scr_printf("Nome: %s_\n", osk_buffer);
    scr_printf("      (%d de %d caracteres)\n\n", osk_length, osk_max);


    for (r = 0; r < OSK_ROWS; r++) {

        const char *row = page_row(r);

        scr_printf("   ");

        for (c = 0; c < row_chars(row); c++) {

            row_char(row, c, key);

            if (r == osk_row && c == osk_col)
                scr_printf("[%s]", key);
            else
                scr_printf(" %s ", key);
        }

        scr_printf("\n\n");
    }


    scr_printf("   Teclado:");

    for (p = 0; p < (osk_accents ? OSK_PAGES : OSK_PAGE_ACCENTS); p++)
        scr_printf(p == osk_page ? " [%s]" : "  %s ", osk_page_names[p]);

    scr_printf("\n\n");

    scr_printf("X digitar | QUADRADO apagar | R1 espaco\n");
    scr_printf("TRIANGULO maiusc/minusc/simbolos\n");
    scr_printf("START confirmar | O cancelar\n");
}


/*
 * Interface grafica: janela por cima da tela atual.
 */
#define OSK_X       64
#define OSK_Y       44
#define OSK_W       512
#define OSK_H       344
#define KEY_W       44
#define KEY_H       30
#define KEY_GAP     4

static void draw_key(float x, float y, float w, const char *label, int focused, int highlight)
{
    gfx_color text = focused ? UI_TEXT : highlight ? UI_OK : UI_TEXT2;

    if (focused)
        gui_selection(x, y, w, KEY_H, 1);
    else
        gfx_rect(x, y, w, KEY_H, UI_PANEL);

    gfx_border(x, y, w, KEY_H, 1, focused ? UI_SEL_BOTTOM : UI_BORDER);

    gfx_text(x + (w - gfx_text_width(label)) / 2, y + 5, label, text);
}


void osk_draw_gui(void)
{
    static const char *action_names[] = { "", "Espaço", "Apagar", "Limpar", "OK" };
    float grid_w = 10 * KEY_W + 9 * KEY_GAP;
    float grid_x = OSK_X + (OSK_W - grid_w) / 2;

    /*
     * Numerico: 3 teclas largas por linha, centradas.
     */
    float key_w = osk_numeric ? 96 : KEY_W;
    float keys_w = osk_numeric ? 3 * key_w + 2 * KEY_GAP : grid_w;
    float keys_x = OSK_X + (OSK_W - keys_w) / 2;
    float y = gui_window(OSK_X, OSK_Y, OSK_W, OSK_H, osk_title);
    char line[160];
    char key[8];
    float x;
    int pages = osk_accents ? OSK_PAGES : OSK_PAGE_ACCENTS;
    int r;
    int c;
    int p;


    /*
     * Campo de texto com cursor piscando; mostra o fim se nao couber.
     */
    gfx_rect(grid_x, y, grid_w, 30, gfx_rgba(0x05, 0x0B, 0x1E, 0x70));
    gfx_border(grid_x, y, grid_w, 30, 1, UI_SEL_BOTTOM);

    {
        const char *shown = osk_buffer;
        float max_w = grid_w - 24;
        float text_w;

        while (*shown && gfx_text_width(shown) > max_w)
            shown += utf8_bytes((unsigned char)*shown);

        text_w = gfx_text(grid_x + 8, y + 5, shown, UI_TEXT);

        if ((clock() / (CLOCKS_PER_SEC / 2)) % 2 == 0)
            gfx_rect(grid_x + 9 + text_w, y + 6, 2, 18, UI_SEL_BOTTOM);
    }

    y += 36;

    snprintf(line, sizeof(line), "%d de %d", osk_length, osk_max);
    gfx_text_right(grid_x + grid_w, y, line, osk_length >= osk_max ? UI_WARNING : UI_TEXT_OFF);


    /*
     * Paginas (a atual em destaque).
     */
    x = grid_x;

    if (osk_numeric) {

        gfx_text(x, y, "Números (endereço IP)", UI_TEXT_OFF);

    } else {

        for (p = 0; p < pages; p++) {

            float w = gfx_text_width(osk_page_names[p]) + 14;

            if (p == osk_page)
                gfx_rect(x, y + 1, w, 18, UI_SEL_DIM);

            gfx_text(x + 7, y, osk_page_names[p], p == osk_page ? UI_TEXT : UI_TEXT_OFF);

            x += w + 4;
        }
    }

    y += 26;


    /*
     * Letras.
     */
    for (r = 0; r < OSK_ROWS; r++) {

        const char *row = page_row(r);

        for (c = 0; c < row_chars(row); c++) {

            row_char(row, c, key);

            draw_key(keys_x + c * (key_w + KEY_GAP), y + r * (KEY_H + KEY_GAP), key_w, key,
                     osk_row == r && osk_col == c, 0);
        }
    }

    y += OSK_ROWS * (KEY_H + KEY_GAP) + 4;


    /*
     * Acoes: no teclado de texto a primeira mostra a proxima pagina.
     */
    {
        int count = action_count();
        float action_w = (keys_w - (count - 1) * KEY_GAP) / count;

        for (c = 0; c < count; c++) {

            int action = action_at(c);
            const char *label = action_names[action];

            if (action == OSK_ACTION_PAGE) {
                snprintf(line, sizeof(line), "Teclas: %s", osk_page_names[(osk_page + 1) % pages]);
                label = line;
            }

            draw_key(keys_x + c * (action_w + KEY_GAP), y, action_w, label,
                     osk_row == OSK_ACTION_ROW && osk_col == c, action == OSK_ACTION_DONE);
        }
    }

    y += KEY_H + 12;


    /*
     * Atalhos.
     */
    x = grid_x;

    x += gfx_button_hint(x, y, GFX_BTN_CROSS, "Digitar");
    x += gfx_button_hint(x, y, GFX_BTN_SQUARE, "Apagar");

    if (!osk_numeric) {
        x += gfx_button_hint(x, y, GFX_BTN_R1, "Espaço");
        gfx_button_hint(x, y, GFX_BTN_TRIANGLE, "Teclas");
    }

    x = grid_x;
    y += 24;

    x += gfx_button_hint(x, y, GFX_BTN_START, "Confirmar");
    gfx_button_hint(x, y, GFX_BTN_CIRCLE, "Cancelar");
}


const char *osk_text(void)
{
    return osk_buffer;
}
