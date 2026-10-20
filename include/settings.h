#ifndef PS2FILEMANAGER_SETTINGS_H
#define PS2FILEMANAGER_SETTINGS_H

/*
 * Tela de configuracao de rede e SMB (lista de dispositivos).
 * Edita uma copia da configuracao; so "Salvar" (ou START)
 * copia para config_get() e grava no Memory Card.
 */

typedef enum {
    SETTINGS_OPEN = 0,
    SETTINGS_CANCELLED,
    SETTINGS_SAVED,
    SETTINGS_SAVE_FAILED   /* aplicado, mas nao gravado no MC */
} SettingsResult;

void settings_open(void);

/*
 * buttons = botoes recem-pressionados (mascara PAD_*).
 */
SettingsResult settings_update(unsigned int buttons);

void settings_draw(void);

#endif
