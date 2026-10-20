#ifndef PS2FILEMANAGER_USBFMT_H
#define PS2FILEMANAGER_USBFMT_H

#include "fatfmt_devctl.h"

/*
 * Formatacao de pendrive (fatfmt.irx, carregado por usb_init).
 *
 * Discos inteiros do BDM (nao os volumes massN:): um pendrive sem
 * tabela ou sem FAT tambem aparece aqui.
 */

int usbfmt_ready(void);

/*
 * Lista os discos USB. Devolve quantos (ate max) ou < 0.
 */
int usbfmt_list(fatfmt_disk_t *disks, int max);

/*
 * HDD interno visto pelo BDM (ata_bd.irx): tabela atual lida dos
 * setores 0-1, sem gravar nada. 0 = ok, < 0 = sem HDD/fatfmt.
 */
int usbfmt_hdd_disk(fatfmt_disk_t *disk);

/*
 * Cria a tabela (FATFMT_TABLE_MBR/GPT) com uma particao no disco
 * inteiro. APAGA TUDO. 0 = ok.
 */
int usbfmt_table(const fatfmt_disk_t *disk, unsigned int table);

/*
 * Formata a 1a particao (FATFMT_FS_FAT32/EXFAT). APAGA a particao.
 */
int usbfmt_format(const fatfmt_disk_t *disk, unsigned int fs);

/*
 * Tamanho em MB e textos para a tela.
 */
unsigned int usbfmt_size_mb(const fatfmt_disk_t *disk);
const char *usbfmt_table_name(unsigned int table);
const char *usbfmt_fs_name(unsigned int fs);
const char *usbfmt_error_text(int code);

#endif
