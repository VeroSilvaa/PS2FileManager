#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <debug.h>
#include <libpad.h>

#include "config.h"
#include "network.h"
#include "osk.h"
#include "settings.h"
#include "smb.h"
#include "ui.h"

typedef enum {
    FIELD_MODE = 0,
    FIELD_IP,           /* IP do PS2: so no modo estatico */
    FIELD_SERVER,       /* IP do servidor SMB */
    FIELD_TEXT,
    FIELD_PASSWORD,
    FIELD_SAVE,
    FIELD_CANCEL
} FieldType;

typedef struct {
    const char *label;
    FieldType type;
    size_t offset;
    int size;
} SettingsField;

#define FIELD(label, type, member) \
    { label, type, offsetof(AppConfig, member), sizeof(((AppConfig *)0)->member) }

static const SettingsField fields[] = {
    { "Modo de IP", FIELD_MODE, 0, 0 },
    FIELD("IP do PS2", FIELD_IP, ip),
    FIELD("Máscara", FIELD_IP, netmask),
    FIELD("Gateway", FIELD_IP, gateway),
    FIELD("DNS", FIELD_IP, dns),
    FIELD("Servidor SMB (IP)", FIELD_SERVER, smb_server),
    FIELD("Compartilhamento", FIELD_TEXT, smb_share),
    FIELD("Usuário", FIELD_TEXT, smb_user),
    FIELD("Senha", FIELD_PASSWORD, smb_password),
    { "Salvar", FIELD_SAVE, 0, 0 },
    { "Cancelar", FIELD_CANCEL, 0, 0 }
};

#define FIELD_COUNT ((int)(sizeof(fields) / sizeof(fields[0])))

static AppConfig edit;
static int cursor;
static int editing;     /* campo no teclado, -1 = nenhum */
static char message[96];


static char *field_text(int index)
{
    return (char *)&edit + fields[index].offset;
}


void settings_open(void)
{
    edit = *config_get();

    cursor = 0;
    editing = -1;
    message[0] = '\0';
}


/*
 * Volta do teclado: valida e guarda no campo.
 */
static void settings_finish_edit(const char *text)
{
    const SettingsField *field = &fields[editing];

    if ((field->type == FIELD_IP || field->type == FIELD_SERVER) &&
        !config_valid_ip(text)) {

        snprintf(message, sizeof(message), "IP invalido: %.40s", text);
        return;
    }

    if (field->type == FIELD_TEXT && text[0] == '\0') {

        snprintf(message, sizeof(message), "Campo obrigatorio.");
        return;
    }

    snprintf(field_text(editing), (size_t)field->size, "%s", text);

    message[0] = '\0';
}


static SettingsResult settings_save(void)
{
    *config_get() = edit;

    printf(
        "FM CONFIG: %s | ip %s | smb %s/%s user %s\n",
        edit.dhcp ? "DHCP" : "ESTATICO",
        edit.ip,
        edit.smb_server,
        edit.smb_share,
        edit.smb_user
    );

    return config_save() == 0 ? SETTINGS_SAVED : SETTINGS_SAVE_FAILED;
}


static SettingsResult settings_select(void)
{
    const SettingsField *field = &fields[cursor];

    switch (field->type) {

        case FIELD_MODE:
            edit.dhcp = !edit.dhcp;
            break;

        case FIELD_SAVE:
            return settings_save();

        case FIELD_CANCEL:
            return SETTINGS_CANCELLED;

        case FIELD_IP:
            if (edit.dhcp) {
                snprintf(message, sizeof(message),
                         "Com DHCP o IP e automatico. Mude o modo para ESTATICO.");
                break;
            }
            /* fallthrough */

        default:
            osk_open(field->label, field_text(cursor), field->size - 1);
            osk_ascii_only();

            if (field->type == FIELD_IP || field->type == FIELD_SERVER)
                osk_numeric_only();

            editing = cursor;
            message[0] = '\0';
            break;
    }

    return SETTINGS_OPEN;
}


SettingsResult settings_update(unsigned int buttons)
{
    OskResult result;


    if (editing >= 0) {

        result = osk_update(buttons);

        if (result == OSK_DONE)
            settings_finish_edit(osk_text());

        if (result != OSK_EDITING)
            editing = -1;

        return SETTINGS_OPEN;
    }


    /*
     * Salvar e Cancelar ficam lado a lado: CIMA/BAIXO tratam os dois
     * como uma linha so; ESQUERDA/DIREITA andam entre eles.
     */
    if (buttons & PAD_UP) {
        if (fields[cursor].type == FIELD_SAVE || fields[cursor].type == FIELD_CANCEL)
            cursor = FIELD_COUNT - 3;
        else
            cursor = cursor > 0 ? cursor - 1 : FIELD_COUNT - 2;
    }

    if (buttons & PAD_DOWN) {
        if (fields[cursor].type == FIELD_SAVE || fields[cursor].type == FIELD_CANCEL)
            cursor = 0;
        else
            cursor++;
    }

    if (buttons & (PAD_LEFT | PAD_RIGHT)) {

        if (fields[cursor].type == FIELD_MODE)
            edit.dhcp = !edit.dhcp;
        else if (fields[cursor].type == FIELD_SAVE)
            cursor++;
        else if (fields[cursor].type == FIELD_CANCEL)
            cursor--;
    }

    if (buttons & PAD_CROSS)
        return settings_select();

    if (buttons & PAD_START)
        return settings_save();

    if (buttons & PAD_CIRCLE)
        return SETTINGS_CANCELLED;

    return SETTINGS_OPEN;
}


void settings_draw(void)
{
    const SettingsField *field;
    const char *value;
    int i;


    if (editing >= 0) {
        osk_draw();
        return;
    }


    scr_printf("PS2FileManager\n");
    scr_printf("===============\n");
    scr_printf("CONFIGURACAO DE REDE E SMB\n\n");


    for (i = 0; i < FIELD_COUNT; i++) {

        field = &fields[i];

        if (field->type == FIELD_SAVE)
            scr_printf("\n");

        scr_printf(i == cursor ? "> " : "  ");

        switch (field->type) {

            case FIELD_MODE:
                scr_printf("%-18s: %s\n", field->label, edit.dhcp ? "DHCP" : "ESTATICO");
                break;

            case FIELD_IP:
                scr_printf("%-18s: %s\n", field->label,
                           edit.dhcp ? "(automatico)" : field_text(i));
                break;

            case FIELD_PASSWORD:
                value = field_text(i);
                scr_printf("%-18s: %s\n", field->label,
                           value[0] ? "********" : "(sem senha)");
                break;

            case FIELD_SAVE:
            case FIELD_CANCEL:
                scr_printf("[ %s ]\n", field->label);
                break;

            default:
                scr_printf("%-18s: %s\n", field->label, field_text(i));
                break;
        }
    }


    scr_printf("\n%s\n", message);

    scr_printf(
        "Arquivo: %s\n\n",
        config_path()[0] ? config_path() : "(ainda nao gravado)"
    );

    scr_printf("X editar/alternar | START salvar | O voltar\n");
    scr_printf("SMB: servidor pelo IP, protocolo SMB1 (NT1).\n");
}


/*
 * Interface grafica: janela por cima da lista de dispositivos; o
 * teclado abre por cima dela.
 */
#define SET_X       72
#define SET_Y       34
#define SET_W       496
#define SET_H       384
#define SET_ROW_H   24
#define SET_LABEL_W 170

void settings_draw_gui(void)
{
    const NetworkInfo *net = network_get_info();
    const SMBInfo *smb = smb_get_info();
    float y = gui_window(SET_X, SET_Y, SET_W, SET_H, "Configurações de rede e SMB");
    float x = SET_X + 16;
    float w = SET_W - 32;
    char line[128];
    int i;


    for (i = 0; i < FIELD_COUNT && fields[i].type != FIELD_SAVE; i++) {

        const SettingsField *field = &fields[i];
        float row = y + i * SET_ROW_H;
        int off = field->type == FIELD_IP && edit.dhcp;
        const char *value = field_text(i);
        gfx_color color = off ? UI_TEXT_OFF : UI_TEXT;

        if (i == cursor && editing < 0)
            gui_selection(x - 6, row - 1, w + 12, SET_ROW_H - 2, 1);

        gfx_text(x, row, field->label, off ? UI_TEXT_OFF : UI_TEXT2);

        switch (field->type) {

            case FIELD_MODE:
                snprintf(line, sizeof(line), "%s", edit.dhcp ? "DHCP (automático)" : "Estático");
                gfx_text(x + SET_LABEL_W, row, line, UI_TEXT);

                /*
                 * Setas: ESQUERDA/DIREITA alternam.
                 */
                if (i == cursor) {
                    gfx_triangle(x + w - 34, row + 9, x + w - 27, row + 4, x + w - 27, row + 14, UI_TEXT);
                    gfx_triangle(x + w - 4, row + 9, x + w - 11, row + 4, x + w - 11, row + 14, UI_TEXT);
                }
                break;

            case FIELD_IP:
                gfx_text(x + SET_LABEL_W, row, off ? "(automático)" : value, color);
                break;

            case FIELD_PASSWORD:
                if (value[0]) {
                    int n = strlen(value) < 12 ? strlen(value) : 12;
                    int k;

                    for (k = 0; k < n; k++)
                        gfx_rect(x + SET_LABEL_W + 2 + k * 11, row + 6, 7, 7, UI_TEXT);
                } else {
                    gfx_text(x + SET_LABEL_W, row, "(sem senha)", UI_TEXT_OFF);
                }
                break;

            default:
                gfx_text_fit(x + SET_LABEL_W, row, w - SET_LABEL_W, value, color);
                break;
        }
    }

    y += i * SET_ROW_H + 6;


    /*
     * Salvar / Cancelar.
     */
    for (; i < FIELD_COUNT; i++) {

        float bw = (w - 12) / 2;
        float bx = x + (fields[i].type == FIELD_CANCEL ? bw + 12 : 0);
        int focused = i == cursor && editing < 0;

        if (focused)
            gui_selection(bx, y, bw, 28, 1);
        else
            gfx_rect(bx, y, bw, 28, UI_PANEL);

        gfx_border(bx, y, bw, 28, 1, focused ? UI_SEL_BOTTOM : UI_BORDER);

        gfx_text(bx + (bw - gfx_text_width(fields[i].label)) / 2, y + 4, fields[i].label,
                 focused ? UI_TEXT : fields[i].type == FIELD_SAVE ? UI_OK : UI_TEXT2);
    }

    y += 38;


    /*
     * Aviso (vermelho) ou situacao atual da rede e do SMB.
     */
    if (message[0]) {

        gfx_text_fit(x, y, w, message, UI_ERROR);

    } else {

        if (net->initialized)
            snprintf(line, sizeof(line), "Em uso: IP %s (%s)", net->ip,
                     net->mode == NETWORK_DHCP ? "DHCP" : "estático");
        else
            snprintf(line, sizeof(line), "Rede ainda não iniciada (abre ao entrar no SMB).");

        gfx_text_fit(x, y, w, line, UI_TEXT2);
    }

    y += 22;

    if (smb->connected)
        snprintf(line, sizeof(line), "SMB conectado: %s/%s (usuário %s)", smb->server, smb->share, smb->user);
    else
        snprintf(line, sizeof(line), "SMB desconectado. Arquivo: %s",
                 config_path()[0] ? config_path() : "(ainda não gravado)");

    gfx_text_fit(x, y, w, line, smb->connected ? UI_OK : UI_TEXT_OFF);

    y += 30;


    {
        float bx = x;

        bx += gfx_button_hint(bx, y, GFX_BTN_CROSS, "Editar");
        bx += gfx_button_hint(bx, y, GFX_BTN_START, "Salvar");
        bx += gfx_button_hint(bx, y, GFX_BTN_CIRCLE, "Voltar");
        gfx_text(bx, y - 1, "SMB1 (NT1), servidor pelo IP", UI_TEXT_OFF);
    }


    if (editing >= 0)
        osk_draw_gui();
}
