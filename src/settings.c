#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <debug.h>
#include <libpad.h>

#include "config.h"
#include "osk.h"
#include "settings.h"

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
    FIELD("Mascara", FIELD_IP, netmask),
    FIELD("Gateway", FIELD_IP, gateway),
    FIELD("DNS", FIELD_IP, dns),
    FIELD("Servidor SMB (IP)", FIELD_SERVER, smb_server),
    FIELD("Compartilhamento", FIELD_TEXT, smb_share),
    FIELD("Usuario", FIELD_TEXT, smb_user),
    FIELD("Senha", FIELD_PASSWORD, smb_password),
    { "Salvar", FIELD_SAVE, 0, 0 },
    { "Voltar sem salvar", FIELD_CANCEL, 0, 0 }
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
                osk_focus_digits();

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


    if (buttons & PAD_UP)
        cursor = cursor > 0 ? cursor - 1 : FIELD_COUNT - 1;

    if (buttons & PAD_DOWN)
        cursor = cursor < FIELD_COUNT - 1 ? cursor + 1 : 0;

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
