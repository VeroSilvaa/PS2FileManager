#ifndef PS2FILEMANAGER_HDL_H
#define PS2FILEMANAGER_HDL_H

#include "iso9660.h"

/*
 * Instalacao HDLoader no HDD (APA), no formato que o OPL le:
 *
 * - particao "PP.SXXX-YYYZZ.HDL.<titulo>" do tipo HDL (0x1337),
 *   criada pelo ps2hdd-hdl.irx, com subparticoes se precisar
 * - cabecalho de 1 KB no offset 0x100000 da area de atributos
 * - dados da ISO gravados pelo hdlwrite.irx ("hdlw:")
 */

/*
 * Progresso: chamado no inicio de cada fase (done = 0) e a cada
 * bloco gravado/conferido. done/total em bytes da fase atual.
 */
#define HDL_PHASE_WRITE   1
#define HDL_PHASE_VERIFY  2

typedef void (*HdlProgressFn)(
    int phase,
    unsigned long long done,
    unsigned long long total
);

void hdl_set_progress(HdlProgressFn fn);

/*
 * Inicia o HDD (hdd_init) e carrega o hdlwrite.irx uma vez.
 */
int hdl_init(void);

/*
 * Instala a ISO: cria a particao, grava cabecalho e dados e confere
 * como o OPL (amostras por fatia). Com verify != 0 tambem rele o HDD
 * inteiro comparando com a origem (dobra o tempo). Em erro, apaga a
 * particao. 0 = ok; detalhe em hdl_error() e no log.
 */
int hdl_install(
    const char *iso_path,
    const IsoGameInfo *game,
    const char *title,
    int verify
);

const char *hdl_error(void);

/*
 * Situacao de um jogo antes de instalar (HDD APA ja iniciado).
 * HDL_GAME_INSTALLED: 'found' recebe o nome da particao.
 */
#define HDL_GAME_OK         0
#define HDL_GAME_INSTALLED  1
#define HDL_GAME_DVD9       2
#define HDL_GAME_BAD_ID     3

int hdl_game_status(const IsoGameInfo *game, char *found, int size);

#endif
