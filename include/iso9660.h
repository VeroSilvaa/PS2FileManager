#ifndef PS2FILEMANAGER_ISO9660_H
#define PS2FILEMANAGER_ISO9660_H

/*
 * Reconhecimento de ISO de jogo de PS2 (HDLoader, Etapa 1):
 * le o descritor primario (setor 16), a pasta raiz e o
 * SYSTEM.CNF. Somente leitura.
 */

#define ISO_SECTOR_SIZE     2048

/*
 * Acima disso nao cabe em CD (~737 MB): DVD.
 */
#define ISO_CD_MAX_SECTORS  360000

typedef struct {
    char game_id[16];       /* "SLUS_123.45" (BOOT2 do SYSTEM.CNF) */
    char volume_id[33];     /* rotulo do volume ISO9660 */
    unsigned int sectors;   /* tamanho do volume em setores de 2048 */
    int is_dvd;
    char reason[48];        /* por que nao e jogo de PS2 */
} IsoGameInfo;

/*
 * 1 se o nome termina em ".iso" (sem diferenciar maiusculas).
 */
int iso_has_extension(const char *name);

/*
 * 1 = ISO de jogo de PS2 (info preenchido);
 * 0 = nao e (info->reason diz o motivo).
 */
int iso_probe_game(const char *path, IsoGameInfo *info);

#endif
