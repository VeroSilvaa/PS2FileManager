/*
 * fatfmt.irx - tabela de particoes (MBR/GPT) e formatacao FAT32/exFAT
 * de discos do BDM (pendrive USB).
 *
 * O bdmfs_fatfs do PS2SDK le e grava FAT32/exFAT, mas foi compilado
 * sem f_mkfs (formatar devolve EIO). Este modulo usa o FatFs com
 * FF_USE_MKFS e grava direto no block_device do BDM:
 *
 *   1. bdm_disconnect_bd(disco): desmonta massN: (e particoes)
 *   2. grava a tabela ou formata a 1a particao
 *   3. bdm_connect_bd(disco): o BDM rele a tabela e monta de novo
 *
 * Interface: devctl em "fatfmt:" (include/fatfmt_devctl.h).
 *
 * PS2FileManager - licenca MIT. FatFs (fatfs/) - licenca FatFs (ChaN).
 */

#include <errno.h>
#include <irx.h>
#include <loadcore.h>
#include <iomanX.h>
#include <io_common.h>
#include <stdio.h>
#include <sysclib.h>
#include <sysmem.h>
#include <thbase.h>
#include <bdm.h>

#include "ff.h"
#include "diskio.h"
#include "fatfmt_devctl.h"

#define MODNAME "fatfmt"
IRX_ID(MODNAME, 1, 1);

#define SECTOR_SIZE     512

/*
 * Particao alinhada em 1 MiB (como Windows/Linux).
 */
#define PART_START      2048

#define GPT_ENTRIES     128
#define GPT_ENTRY_SIZE  128
#define GPT_ENTRY_SECTORS (GPT_ENTRIES * GPT_ENTRY_SIZE / SECTOR_SIZE)   /* 32 */

/*
 * Buffer de trabalho do f_mkfs e da tabela GPT (16 KB de entradas).
 */
#define WORK_SIZE       (32 * 1024)

static unsigned char *work;

/*
 * Volume "0:" do FatFs = a particao sendo formatada.
 */
static struct block_device *target_bd;
static u64 target_start;
static u64 target_count;

IOMANX_RETURN_VALUE_IMPL(0);
IOMANX_RETURN_VALUE_IMPL(EIO);


/* ============================================================
 * Utilitarios
 * ============================================================ */

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

static void put32(unsigned char *p, u32 v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static void put64(unsigned char *p, u64 v)
{
    put32(p, (u32)v);
    put32(p + 4, (u32)(v >> 32));
}

static u32 get32(const unsigned char *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 get64(const unsigned char *p)
{
    return (u64)get32(p) | ((u64)get32(p + 4) << 32);
}


static u32 crc32(const unsigned char *data, u32 size)
{
    u32 crc = 0xFFFFFFFF;
    int bit;

    while (size--) {

        crc ^= *data++;

        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
    }

    return ~crc;
}


/*
 * Numeros "aleatorios" para assinatura do MBR e GUIDs do GPT.
 */
static u32 rand_state;

static u32 rand32(void)
{
    rand_state = rand_state * 1103515245 + 12345;

    return rand_state ^ (rand_state >> 16);
}

static void rand_seed(struct block_device *bd)
{
    iop_sys_clock_t clock;

    GetSystemTime(&clock);

    rand_state = clock.lo ^ clock.hi ^ (u32)bd->sectorCount ^ (bd->devNr << 24);
}

static void rand_guid(unsigned char *guid)
{
    int i;

    for (i = 0; i < 16; i += 4)
        put32(guid + i, rand32());

    guid[7] = (guid[7] & 0x0F) | 0x40;   /* versao 4 */
    guid[8] = (guid[8] & 0x3F) | 0x80;   /* variante */
}


/* ============================================================
 * Acesso ao disco
 * ============================================================ */

static int disk_rw(struct block_device *bd, u64 sector, void *buffer, u32 count, int write)
{
    int result;

    while (count > 0) {

        u16 chunk = count > 0x8000 ? 0x8000 : (u16)count;

        result = write
            ? bd->write(bd, sector, buffer, chunk)
            : bd->read(bd, sector, buffer, chunk);

        if (result != chunk) {
            printf(MODNAME ": %s setor %u -> %d\n",
                   write ? "write" : "read", (u32)sector, result);
            return -FATFMT_ERR_IO;
        }

        sector += chunk;
        buffer = (unsigned char *)buffer + chunk * SECTOR_SIZE;
        count -= chunk;
    }

    return 0;
}

static int disk_zero(struct block_device *bd, u64 sector, u32 count)
{
    u32 chunk;
    int result;

    memset(work, 0, WORK_SIZE);

    while (count > 0) {

        chunk = count > WORK_SIZE / SECTOR_SIZE ? WORK_SIZE / SECTOR_SIZE : count;

        result = disk_rw(bd, sector, work, chunk, 1);

        if (result < 0)
            return result;

        sector += chunk;
        count -= chunk;
    }

    return 0;
}


/* ============================================================
 * Interface de disco do FatFs (volume 0 = target)
 * ============================================================ */

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0 && target_bd) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !target_bd)
        return RES_NOTRDY;

    if (sector + count > target_count)
        return RES_PARERR;

    return disk_rw(target_bd, target_start + sector, buff, count, 0) == 0 ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !target_bd)
        return RES_NOTRDY;

    if (sector + count > target_count)
        return RES_PARERR;

    return disk_rw(target_bd, target_start + sector, (void *)buff, count, 1) == 0 ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || !target_bd)
        return RES_NOTRDY;

    switch (cmd) {

        case CTRL_SYNC:
            if (target_bd->flush)
                target_bd->flush(target_bd);
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = target_count;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD *)buff = SECTOR_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
    }

    return RES_PARERR;
}


/* ============================================================
 * Discos, tabela e sistema de arquivos atuais
 * ============================================================ */

/*
 * Disco inteiro (parNr 0) do BDM. So USB por enquanto.
 */
static int is_supported_disk(struct block_device *bd)
{
    return bd != NULL &&
           bd->parNr == 0 &&
           bd->sectorSize == SECTOR_SIZE &&
           bd->name != NULL &&
           (strcmp(bd->name, "usb") == 0 || strcmp(bd->name, "ata") == 0);
}

/*
 * Discos desconectados do BDM pelo WIPE (nao aparecem mais no
 * bdm_get_bd ate a proxima TABLE/FORMAT reconectar).
 */
#define MAX_DETACHED 4

static struct block_device *detached[MAX_DETACHED];


/*
 * Discos conhecidos: os do BDM + os desconectados pelo WIPE.
 * list deve ter espaco para 20 + MAX_DETACHED.
 */
static int all_disks(struct block_device **list)
{
    int i;

    bdm_get_bd(list, 20);

    for (i = 0; i < MAX_DETACHED; i++)
        list[20 + i] = detached[i];

    return 20 + MAX_DETACHED;
}


static void set_detached(struct block_device *bd, int value)
{
    int i;

    for (i = 0; i < MAX_DETACHED; i++) {

        if (detached[i] == bd)
            detached[i] = NULL;
    }

    if (!value)
        return;

    for (i = 0; i < MAX_DETACHED; i++) {

        if (detached[i] == NULL) {
            detached[i] = bd;
            return;
        }
    }
}


static struct block_device *find_disk(const fatfmt_request_t *req)
{
    struct block_device *list[20 + MAX_DETACHED];
    int count;
    int i;

    count = all_disks(list);

    for (i = 0; i < count; i++) {

        if (is_supported_disk(list[i]) &&
            strcmp(list[i]->name, req->name) == 0 &&
            list[i]->devNr == req->dev_nr)
            return list[i];
    }

    return NULL;
}


/*
 * Sistema de arquivos pelo setor de boot (VBR).
 */
static unsigned int detect_fs(const unsigned char *vbr)
{
    if (memcmp(vbr + 3, "EXFAT   ", 8) == 0)
        return FATFMT_FS_EXFAT;

    if (memcmp(vbr + 0x52, "FAT32   ", 8) == 0)
        return FATFMT_FS_FAT32;

    if (memcmp(vbr + 0x36, "FAT", 3) == 0)
        return FATFMT_FS_FAT;

    return FATFMT_FS_NONE;
}


/*
 * Tabela e 1a particao. start/count = 0 sem particao.
 * Disco sem tabela mas com FAT no setor 0 ("superfloppy"):
 * TABLE_NONE com start 0 e fs detectado.
 */
static int read_layout(struct block_device *bd, unsigned int *table, u64 *start, u64 *count, unsigned int *fs)
{
    unsigned char *sector = work;
    u64 entries_lba;
    u32 entries;
    u32 entry_size;
    u32 i;
    int result;
    int j;

    *table = FATFMT_TABLE_NONE;
    *start = 0;
    *count = 0;
    *fs = FATFMT_FS_NONE;

    result = disk_rw(bd, 0, sector, 2, 0);

    if (result < 0)
        return result;


    /*
     * HDD do PS2: cabecalho APA no setor 0 ("APA" no offset 4,
     * mesmo teste do OPL). Disco zerado: sem nada a proteger.
     */
    if (memcmp(sector + 4, "APA", 3) == 0) {
        *table = FATFMT_TABLE_APA;
        return 0;
    }

    for (j = 0; j < 2 * SECTOR_SIZE && sector[j] == 0; j++)
        ;

    if (j == 2 * SECTOR_SIZE) {
        *table = FATFMT_TABLE_EMPTY;
        return 0;
    }


    if (memcmp(sector + SECTOR_SIZE, "EFI PART", 8) == 0) {

        *table = FATFMT_TABLE_GPT;

        entries_lba = get64(sector + SECTOR_SIZE + 72);
        entries = get32(sector + SECTOR_SIZE + 80);
        entry_size = get32(sector + SECTOR_SIZE + 84);

        if (entry_size < 128 || entry_size > SECTOR_SIZE || SECTOR_SIZE % entry_size)
            return 0;

        for (i = 0; i < entries && i < 512; i++) {

            u32 offset = (i * entry_size) % SECTOR_SIZE;

            if (offset == 0) {

                result = disk_rw(bd, entries_lba + (i * entry_size) / SECTOR_SIZE, sector, 1, 0);

                if (result < 0)
                    return result;
            }

            for (j = 0; j < 16 && sector[offset + j] == 0; j++)
                ;

            if (j < 16) {

                *start = get64(sector + offset + 32);
                *count = get64(sector + offset + 40) - *start + 1;
                break;
            }
        }

    } else if (sector[510] == 0x55 && sector[511] == 0xAA) {

        *fs = detect_fs(sector);

        if (*fs != FATFMT_FS_NONE)
            return 0;   /* superfloppy: FAT no setor 0, sem tabela */

        *table = FATFMT_TABLE_MBR;

        for (j = 0; j < 4; j++) {

            unsigned char *entry = sector + 0x1BE + j * 16;

            if (entry[4] != 0 && entry[4] != 0xEE) {

                *start = get32(entry + 8);
                *count = get32(entry + 12);
                break;
            }
        }
    }


    if (*count > 0 && *start + *count <= bd->sectorCount) {

        result = disk_rw(bd, *start, sector, 1, 0);

        if (result < 0)
            return result;

        *fs = detect_fs(sector);
    }

    return 0;
}


static int devctl_list(fatfmt_disk_t *out, unsigned int buflen)
{
    struct block_device *list[20 + MAX_DETACHED];
    unsigned int table;
    unsigned int fs;
    u64 start;
    u64 count;
    int total;
    int n = 0;
    int i;

    total = all_disks(list);

    for (i = 0; i < total; i++) {

        if (!is_supported_disk(list[i]))
            continue;

        if ((unsigned int)(n + 1) * sizeof(fatfmt_disk_t) > buflen)
            break;

        memset(&out[n], 0, sizeof(fatfmt_disk_t));

        strncpy(out[n].name, list[i]->name, sizeof(out[n].name) - 1);
        out[n].dev_nr = list[i]->devNr;
        out[n].sector_size = list[i]->sectorSize;
        out[n].sectors_lo = (u32)list[i]->sectorCount;
        out[n].sectors_hi = (u32)(list[i]->sectorCount >> 32);

        if (read_layout(list[i], &table, &start, &count, &fs) == 0) {
            out[n].table = table;
            out[n].fs = fs;
        }

        n++;
    }

    return n;
}


/* ============================================================
 * Criar tabela (uma particao no disco inteiro)
 * ============================================================ */

static void mbr_entry(unsigned char *entry, unsigned int type, u32 start, u32 count)
{
    entry[0] = 0x00;                    /* nao inicializavel */
    entry[1] = 0xFE; entry[2] = 0xFF; entry[3] = 0xFF;   /* CHS: usar LBA */
    entry[4] = type;
    entry[5] = 0xFE; entry[6] = 0xFF; entry[7] = 0xFF;
    put32(entry + 8, start);
    put32(entry + 12, count);
}


/*
 * Ultimo setor da particao: fim alinhado em 1 MiB, ate 'limit'
 * (inclusive).
 */
static u64 part_last(u64 limit)
{
    return ((limit + 1) / PART_START) * PART_START - 1;
}


/*
 * Total de setores que o disco realmente aceita: o PCSX2 recusa o
 * ultimo setor do HDD (IDNF). Le e regrava o mesmo conteudo (nada
 * muda) e recua ate dar certo. 0 = falhou.
 */
static u64 usable_total(struct block_device *bd)
{
    u64 total = bd->sectorCount;
    int tries;

    for (tries = 0; tries < 8 && total > PART_START; tries++, total--) {

        if (disk_rw(bd, total - 1, work, 1, 0) == 0 &&
            disk_rw(bd, total - 1, work, 1, 1) == 0) {

            if (total != bd->sectorCount)
                printf(MODNAME ": disco aceita %u de %u setores\n",
                       (u32)total, (u32)bd->sectorCount);

            return total;
        }
    }

    return 0;
}


/*
 * Ordem segura: tudo fora do setor 0 primeiro; o setor 0 (que
 * decide o tipo do disco) so e gravado no fim. Se algo falhar no
 * meio, a tabela antiga continua no lugar.
 */
static int write_mbr(struct block_device *bd, u64 total)
{
    unsigned char *sector = work;
    u64 count;
    int result;

    /*
     * Inicio da particao (nenhum sistema de arquivos antigo) e
     * cabecalhos GPT antigos (primario e reserva).
     */
    result = disk_zero(bd, PART_START, WORK_SIZE / SECTOR_SIZE);

    if (result == 0)
        result = disk_zero(bd, 1, 1);

    if (result == 0)
        result = disk_zero(bd, total - 1, 1);

    if (result < 0)
        return result;


    count = part_last(total - 1) - PART_START + 1;

    if (count > 0xFFFFFFFF - PART_START)
        count = (0xFFFFFFFF / PART_START) * PART_START - PART_START;

    memset(sector, 0, SECTOR_SIZE);

    put32(sector + 0x1B8, rand32());
    mbr_entry(sector + 0x1BE, 0x0C, PART_START, (u32)count);   /* FAT32 LBA */
    sector[510] = 0x55;
    sector[511] = 0xAA;

    return disk_rw(bd, 0, sector, 1, 1);
}


/*
 * Basic Data (EBD0A0A2-B9E5-4433-87C0-68B6B72699C7), ordem do disco.
 */
static const unsigned char gpt_basic_data[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static void gpt_header(unsigned char *header, u64 current, u64 backup, u64 last_usable,
                       const unsigned char *disk_guid, u64 entries_lba, u32 entries_crc)
{
    memset(header, 0, SECTOR_SIZE);

    memcpy(header, "EFI PART", 8);
    put32(header + 8, 0x00010000);
    put32(header + 12, 92);
    put64(header + 24, current);
    put64(header + 32, backup);
    put64(header + 40, 2 + GPT_ENTRY_SECTORS);
    put64(header + 48, last_usable);
    memcpy(header + 56, disk_guid, 16);
    put64(header + 72, entries_lba);
    put32(header + 80, GPT_ENTRIES);
    put32(header + 84, GPT_ENTRY_SIZE);
    put32(header + 88, entries_crc);
    put32(header + 16, crc32(header, 92));
}


static int write_gpt(struct block_device *bd, u64 total)
{
    unsigned char *entries = work;                          /* 16 KB */
    unsigned char *sector = work + GPT_ENTRY_SECTORS * SECTOR_SIZE;
    unsigned char disk_guid[16];
    static const char name[] = "PS2";
    u64 last_usable = total - 1 - GPT_ENTRY_SECTORS - 1;    /* total - 34 */
    u32 entries_crc;
    int result;
    int i;


    /*
     * Inicio da particao zerado antes de montar as entradas (o
     * disk_zero usa o mesmo buffer).
     */
    result = disk_zero(bd, PART_START, WORK_SIZE / SECTOR_SIZE);

    if (result < 0)
        return result;


    rand_guid(disk_guid);


    /*
     * Entradas: uma particao Basic Data de PART_START ate o fim.
     */
    memset(entries, 0, GPT_ENTRY_SECTORS * SECTOR_SIZE);

    memcpy(entries, gpt_basic_data, 16);
    rand_guid(entries + 16);
    put64(entries + 32, PART_START);
    put64(entries + 40, part_last(last_usable));

    for (i = 0; name[i]; i++)
        put16(entries + 56 + i * 2, (unsigned char)name[i]);

    entries_crc = crc32(entries, GPT_ENTRY_SECTORS * SECTOR_SIZE);


    /*
     * Entradas primaria (LBA 2) e reserva (antes do ultimo setor).
     */
    result = disk_rw(bd, 2, entries, GPT_ENTRY_SECTORS, 1);

    if (result < 0)
        return result;

    result = disk_rw(bd, total - 1 - GPT_ENTRY_SECTORS, entries, GPT_ENTRY_SECTORS, 1);

    if (result < 0)
        return result;


    /*
     * Cabecalhos: primario (LBA 1) e reserva (ultimo setor).
     */
    gpt_header(sector, 1, total - 1, last_usable, disk_guid, 2, entries_crc);

    result = disk_rw(bd, 1, sector, 1, 1);

    if (result < 0)
        return result;

    gpt_header(sector, total - 1, 1, last_usable, disk_guid,
               total - 1 - GPT_ENTRY_SECTORS, entries_crc);

    result = disk_rw(bd, total - 1, sector, 1, 1);

    if (result < 0)
        return result;


    /*
     * MBR protetora: uma entrada 0xEE cobrindo o disco.
     */
    memset(sector, 0, SECTOR_SIZE);

    mbr_entry(sector + 0x1BE, 0xEE, 1, total - 1 > 0xFFFFFFFF ? 0xFFFFFFFF : (u32)(total - 1));
    sector[0x1BE + 1] = 0x00; sector[0x1BE + 2] = 0x02; sector[0x1BE + 3] = 0x00;
    sector[510] = 0x55;
    sector[511] = 0xAA;

    return disk_rw(bd, 0, sector, 1, 1);
}


static int devctl_table(const fatfmt_request_t *req)
{
    struct block_device *bd;
    u64 total;
    int result;


    if (req->value != FATFMT_TABLE_MBR && req->value != FATFMT_TABLE_GPT)
        return -EINVAL;

    bd = find_disk(req);

    if (!bd)
        return -ENODEV;

    total = usable_total(bd);

    if (total == 0)
        return -FATFMT_ERR_IO;

    if (total < PART_START + 2 * 2048)
        return -FATFMT_ERR_TOO_SMALL;

    rand_seed(bd);

    printf(MODNAME ": tabela %s em %s%u (%u setores)\n",
           req->value == FATFMT_TABLE_GPT ? "GPT" : "MBR",
           bd->name, bd->devNr, (u32)total);


    bdm_disconnect_bd(bd);

    result = req->value == FATFMT_TABLE_GPT ? write_gpt(bd, total) : write_mbr(bd, total);

    if (bd->flush)
        bd->flush(bd);

    set_detached(bd, 0);
    bdm_connect_bd(bd);


    printf(MODNAME ": tabela -> %d\n", result);

    return result;
}


/* ============================================================
 * Formatar a 1a particao
 * ============================================================ */

static int devctl_format(const fatfmt_request_t *req)
{
    struct block_device *bd;
    MKFS_PARM opt;
    unsigned int table;
    unsigned int fs;
    u64 start;
    u64 count;
    FRESULT fr;
    int result;


    if (req->value != FATFMT_FS_FAT32 && req->value != FATFMT_FS_EXFAT)
        return -EINVAL;

    bd = find_disk(req);

    if (!bd)
        return -ENODEV;

    result = read_layout(bd, &table, &start, &count, &fs);

    if (result < 0)
        return result;

    if (table == FATFMT_TABLE_NONE || count == 0)
        return -FATFMT_ERR_NO_PARTITION;

    printf(MODNAME ": formatar %s em %s%u, particao %u + %u setores\n",
           req->value == FATFMT_FS_EXFAT ? "exFAT" : "FAT32",
           bd->name, bd->devNr, (u32)start, (u32)count);


    bdm_disconnect_bd(bd);

    target_bd = bd;
    target_start = start;
    target_count = count;

    memset(&opt, 0, sizeof(opt));
    opt.fmt = (req->value == FATFMT_FS_EXFAT ? FM_EXFAT : FM_FAT32) | FM_SFD;

    /*
     * FAT32 com duas copias da FAT, como o Windows.
     */
    opt.n_fat = req->value == FATFMT_FS_EXFAT ? 1 : 2;

    fr = f_mkfs("0:", &opt, work, WORK_SIZE);

    target_bd = NULL;

    printf(MODNAME ": f_mkfs -> %d\n", fr);

    result = fr == FR_OK ? 0 : -(FATFMT_ERR_FATFS + (int)fr);


    /*
     * FM_SFD grava "hidden sectors" = 0: no FAT32 o campo deve ser
     * o inicio da particao (setor de boot e copia no setor 6).
     * No exFAT o PartitionOffset 0 significa "ignorar".
     */
    if (result == 0 && req->value == FATFMT_FS_FAT32 && start <= 0xFFFFFFFF) {

        int copy;

        for (copy = 0; copy < 2 && result == 0; copy++) {

            u64 vbr = start + (copy ? 6 : 0);

            result = disk_rw(bd, vbr, work, 1, 0);

            if (result == 0 && memcmp(work + 0x52, "FAT32   ", 8) == 0) {

                put32(work + 0x1C, (u32)start);

                result = disk_rw(bd, vbr, work, 1, 1);
            }
        }
    }


    /*
     * MBR: tipo da particao conforme o sistema (0x0C FAT32 LBA,
     * 0x07 exFAT). No GPT o Basic Data serve para os dois.
     */
    if (result == 0 && table == FATFMT_TABLE_MBR) {

        result = disk_rw(bd, 0, work, 1, 0);

        if (result == 0) {

            int j;

            for (j = 0; j < 4; j++) {

                unsigned char *entry = work + 0x1BE + j * 16;

                if (get32(entry + 8) == (u32)start && entry[4] != 0) {
                    entry[4] = req->value == FATFMT_FS_EXFAT ? 0x07 : 0x0C;
                    break;
                }
            }

            result = disk_rw(bd, 0, work, 1, 1);
        }
    }

    if (bd->flush)
        bd->flush(bd);

    set_detached(bd, 0);
    bdm_connect_bd(bd);

    printf(MODNAME ": formatar -> %d\n", result);

    return result;
}


/* ============================================================
 * Apagar tabelas (para voltar ao APA)
 * ============================================================ */

static int devctl_wipe(const fatfmt_request_t *req)
{
    struct block_device *bd;
    u64 total;
    int result;


    bd = find_disk(req);

    if (!bd)
        return -ENODEV;

    total = usable_total(bd);

    if (total == 0)
        return -FATFMT_ERR_IO;

    if (total < PART_START + 64)
        return -FATFMT_ERR_TOO_SMALL;

    printf(MODNAME ": apagar tabelas de %s%u\n", bd->name, bd->devNr);


    /*
     * Fica desconectado: o bdmfs_fatfs nao pode ficar com o volume
     * montado enquanto outro driver (APA) grava no disco.
     */
    bdm_disconnect_bd(bd);
    set_detached(bd, 1);

    /*
     * Fim (GPT reserva) antes do inicio (setor 0 por ultimo).
     */
    result = disk_zero(bd, total - 1 - GPT_ENTRY_SECTORS, GPT_ENTRY_SECTORS + 1);

    if (result == 0)
        result = disk_zero(bd, 0, PART_START);

    if (bd->flush)
        bd->flush(bd);

    printf(MODNAME ": apagar -> %d\n", result);

    return result;
}


/* ============================================================
 * Dispositivo "fatfmt:"
 * ============================================================ */

static int fatfmt_devctl(iomanX_iop_file_t *f, const char *name, int cmd,
                         void *arg, unsigned int arglen, void *buf, unsigned int buflen)
{
    (void)f;
    (void)name;

    switch (cmd) {

        case FATFMT_DEVCTL_LIST:
            return devctl_list((fatfmt_disk_t *)buf, buflen);

        case FATFMT_DEVCTL_TABLE:
            if (arglen < sizeof(fatfmt_request_t))
                return -EINVAL;
            return devctl_table((const fatfmt_request_t *)arg);

        case FATFMT_DEVCTL_FORMAT:
            if (arglen < sizeof(fatfmt_request_t))
                return -EINVAL;
            return devctl_format((const fatfmt_request_t *)arg);

        case FATFMT_DEVCTL_WIPE:
            if (arglen < sizeof(fatfmt_request_t))
                return -EINVAL;
            return devctl_wipe((const fatfmt_request_t *)arg);
    }

    return -EINVAL;
}


static iomanX_iop_device_ops_t fatfmt_ops = {
    IOMANX_RETURN_VALUE(0),     /* init */
    IOMANX_RETURN_VALUE(0),     /* deinit */
    IOMANX_RETURN_VALUE(EIO),   /* format */
    IOMANX_RETURN_VALUE(EIO),   /* open */
    IOMANX_RETURN_VALUE(EIO),   /* close */
    IOMANX_RETURN_VALUE(EIO),   /* read */
    IOMANX_RETURN_VALUE(EIO),   /* write */
    IOMANX_RETURN_VALUE(EIO),   /* lseek */
    IOMANX_RETURN_VALUE(EIO),   /* ioctl */
    IOMANX_RETURN_VALUE(EIO),   /* remove */
    IOMANX_RETURN_VALUE(EIO),   /* mkdir */
    IOMANX_RETURN_VALUE(EIO),   /* rmdir */
    IOMANX_RETURN_VALUE(EIO),   /* dopen */
    IOMANX_RETURN_VALUE(EIO),   /* dclose */
    IOMANX_RETURN_VALUE(EIO),   /* dread */
    IOMANX_RETURN_VALUE(EIO),   /* getstat */
    IOMANX_RETURN_VALUE(EIO),   /* chstat */
    IOMANX_RETURN_VALUE(EIO),   /* rename */
    IOMANX_RETURN_VALUE(EIO),   /* chdir */
    IOMANX_RETURN_VALUE(EIO),   /* sync */
    IOMANX_RETURN_VALUE(EIO),   /* mount */
    IOMANX_RETURN_VALUE(EIO),   /* umount */
    IOMANX_RETURN_VALUE_S64(EIO), /* lseek64 */
    &fatfmt_devctl,             /* devctl */
    IOMANX_RETURN_VALUE(EIO),   /* symlink */
    IOMANX_RETURN_VALUE(EIO),   /* readlink */
    IOMANX_RETURN_VALUE(EIO),   /* ioctl2 */
};

static iomanX_iop_device_t fatfmt_device = {
    "fatfmt",
    IOP_DT_FS | IOP_DT_FSEXT,
    1,
    "FAT32/exFAT formatter",
    &fatfmt_ops,
};


int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    work = AllocSysMemory(ALLOC_FIRST, WORK_SIZE, NULL);

    if (work == NULL) {
        printf(MODNAME ": sem memoria\n");
        return MODULE_NO_RESIDENT_END;
    }

    if (iomanX_AddDrv(&fatfmt_device) != 0) {
        printf(MODNAME ": AddDrv falhou\n");
        return MODULE_NO_RESIDENT_END;
    }

    printf(MODNAME ": pronto (fatfmt:)\n");

    return MODULE_RESIDENT_END;
}
