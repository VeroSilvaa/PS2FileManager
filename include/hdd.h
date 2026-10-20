#ifndef PS2FILEMANAGER_HDD_H
#define PS2FILEMANAGER_HDD_H

/*
 * HDD interno (Etapa 5): APA (particoes) + PFS (arquivos).
 * Baseado no hdd.c do PS2DVDPlayer.
 *
 * - "hdd0:"  lista de particoes (somente leitura aqui)
 * - "pfs0:/" particao montada
 */

int hdd_init(void);

int hdd_mount_partition(const char *name);

void hdd_unmount(void);

int hdd_is_mounted(void);

const char *hdd_mounted_name(void);

const char *hdd_error(void);

#endif
