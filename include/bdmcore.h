#ifndef PS2FILEMANAGER_BDMCORE_H
#define PS2FILEMANAGER_BDMCORE_H

/*
 * Nucleo do BDM, compartilhado pelo USB (usbmass_bd) e pelo HDD
 * (ata_bd): bdm.irx + bdmfs_fatfs.irx, carregados uma unica vez.
 */
int bdm_core_load(void);

/*
 * fatfmt.irx (tabela/formatacao e leitura dos setores 0-1 dos
 * discos do BDM). Precisa do nucleo. 1 = carregado.
 */
int bdm_fatfmt_load(void);

#endif
