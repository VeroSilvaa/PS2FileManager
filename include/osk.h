#ifndef PS2FILEMANAGER_OSK_H
#define PS2FILEMANAGER_OSK_H

/*
 * Teclado virtual (Etapa 4c) na debug screen. Uma instancia so:
 * criar pasta, renomear e, depois, a configuracao do SMB.
 *
 * Direcional move | X digita | QUADRADO apaga | R1 espaco
 * TRIANGULO troca maiusculas/minusculas/simbolos
 * START confirma | O cancela
 */

#define OSK_MAX_TEXT  64

typedef enum {
    OSK_EDITING = 0,
    OSK_DONE,
    OSK_CANCEL
} OskResult;

void osk_open(const char *title, const char *initial, int max_length);

/*
 * buttons = botoes recem-pressionados (mascara PAD_* da libpad).
 */
OskResult osk_update(unsigned int buttons);

void osk_draw(void);

const char *osk_text(void);

#endif
