#ifndef PS2FILEMANAGER_HDL_H
#define PS2FILEMANAGER_HDL_H

/*
 * Instalacao HDLoader no HDD (APA).
 *
 * Dados do jogo gravados pelo hdlwrite.irx ("hdlw:"), particoes
 * criadas pelo ps2hdd-hdl.irx (tipo HDL).
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
 * Autoteste do hdlwrite: cria uma particao HDL de teste (128 MB +
 * subparticao), grava 130 MB, rele e confere, e apaga a particao.
 * 0 = ok. Resultado detalhado em hdl_error() e no log.
 */
int hdl_selftest(void);

const char *hdl_error(void);

#endif
