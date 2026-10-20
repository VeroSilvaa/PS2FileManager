#ifndef PS2FILEMANAGER_FATFMT_DEVCTL_H
#define PS2FILEMANAGER_FATFMT_DEVCTL_H

/*
 * Interface do fatfmt.irx (iop/fatfmt), usada pelo EE com
 * fileXioDevctl("fatfmt:", ...). Compartilhado entre EE e IOP:
 * so tipos de 32 bits (u64 tem alinhamento diferente).
 */

/*
 * LIST: buf = fatfmt_disk_t[FATFMT_MAX_DISKS]; devolve quantos.
 * TABLE: arg = fatfmt_request_t (value = FATFMT_TABLE_*): cria a
 *        tabela com uma particao no disco inteiro. APAGA TUDO.
 * FORMAT: arg = fatfmt_request_t (value = FATFMT_FS_FAT32/EXFAT):
 *        formata a primeira particao da tabela existente.
 */
#define FATFMT_DEVCTL_LIST      0x4601
#define FATFMT_DEVCTL_TABLE     0x4602
#define FATFMT_DEVCTL_FORMAT    0x4603

#define FATFMT_MAX_DISKS        8

#define FATFMT_TABLE_NONE       0
#define FATFMT_TABLE_MBR        1
#define FATFMT_TABLE_GPT        2

#define FATFMT_FS_NONE          0
#define FATFMT_FS_FAT           1   /* FAT12/FAT16 */
#define FATFMT_FS_FAT32         2
#define FATFMT_FS_EXFAT         3

/*
 * Erros proprios (alem de -errno). FatFs: -(FATFMT_ERR_FATFS + FRESULT).
 */
#define FATFMT_ERR_NO_PARTITION 1001   /* FORMAT sem tabela/particao */
#define FATFMT_ERR_TOO_SMALL    1002
#define FATFMT_ERR_IO           1003
#define FATFMT_ERR_FATFS        1100

typedef struct {
    char name[8];               /* "usb" (e "ata" no futuro) */
    unsigned int dev_nr;
    unsigned int sector_size;
    unsigned int sectors_lo;    /* total de setores (64 bits) */
    unsigned int sectors_hi;
    unsigned int table;         /* FATFMT_TABLE_* */
    unsigned int fs;            /* FATFMT_FS_* da 1a particao */
} fatfmt_disk_t;

typedef struct {
    char name[8];
    unsigned int dev_nr;
    unsigned int value;
} fatfmt_request_t;

#endif
