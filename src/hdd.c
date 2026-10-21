#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>

/*
 * fileXioMount/fileXioUmount nao tem equivalente POSIX.
 */
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include "dev9.h"
#include "hdd.h"
#include "bdmcore.h"
#include "usbfmt.h"

/*
 * Baseado no hdd.c do PS2DVDPlayer (DEV9 -> ATAD -> HDD -> PFS).
 *
 * Particoes ficam montadas em pfs0:/pfs1: ate serem substituidas,
 * para que caminhos no clipboard continuem validos enquanto o
 * usuario navega em outros dispositivos.
 */

#define HDD_SLOTS 2

extern unsigned char PS2ATAD_irx[];
extern unsigned int size_PS2ATAD_irx;

extern unsigned char PS2HDD_irx[];
extern unsigned int size_PS2HDD_irx;

extern unsigned char PS2FS_irx[];
extern unsigned int size_PS2FS_irx;

/*
 * Mesmos parametros do OPL: ate 4 montagens, 10 arquivos
 * abertos, 40 buffers.
 */
/*
 * ps2hdd: o padrao e 1 arquivo aberto; a particao montada ocupava
 * esse unico handle e a lista de particoes (opendir hdd0:) falhava.
 */
static char ps2hdd_args[] = "-o\0" "4\0" "-n\0" "20";

static char ps2fs_args[] = "-m\0" "4\0" "-o\0" "10\0" "-n\0" "40";

typedef struct {
    int mounted;
    char name[64];
    char point[8];
    unsigned int last_use;
} HddSlot;

static HddSlot slots[HDD_SLOTS];

static int hdd_ready = 0;
static int atad_loaded = 0;

/*
 * Tabela FATFMT_TABLE_* do disco quando ele nao e APA (0 = APA ou
 * ainda nao verificado).
 */
static int hdd_not_apa = 0;
static int hdd_table = -1;
static unsigned int use_counter = 0;

static char last_error[128];


static void hdd_set_error(const char *message, int code)
{
    snprintf(
        last_error,
        sizeof(last_error),
        "%s (%d)",
        message,
        code
    );

    printf("HDD: %s\n", last_error);
}


static int hdd_load(const char *name, void *buffer, unsigned int size, int arg_len, const char *args)
{
    int id;
    int result = 0;

    id = SifExecModuleBuffer(
        buffer,
        size,
        arg_len,
        args,
        &result
    );

    printf(
        "HDD: %s id %d ret %d\n",
        name,
        id,
        result
    );

    if (id < 0)
        return id;

    /*
     * ret 1 = NO_RESIDENT_END: o modulo descarregou
     * (por exemplo, nenhum HDD encontrado).
     */
    if (result == 1)
        return -1;

    return 0;
}


static int hdd_check_layout(void)
{
    fatfmt_disk_t disk;
    int result;

    result = usbfmt_hdd_disk(&disk);

    if (result < 0) {
        hdd_set_error("HDD nao encontrado no BDM (ata_bd/fatfmt)", result);
        return -6;
    }

    hdd_table = disk.table;

    printf(
        "HDD: setores 0-1: %s, %u MB\n",
        usbfmt_table_name(disk.table),
        usbfmt_size_mb(&disk)
    );

    if (disk.table == FATFMT_TABLE_APA || disk.table == FATFMT_TABLE_EMPTY)
        return 0;

    hdd_not_apa = disk.table == FATFMT_TABLE_NONE ? 99 : (int)disk.table;

    hdd_set_error(
        disk.table == FATFMT_TABLE_NONE
            ? "HDD com dados desconhecidos: driver APA nao carregado"
            : "HDD nao e APA (MBR/GPT/exFAT?): driver APA nao carregado",
        hdd_not_apa
    );

    return -5;
}


int hdd_layout_table(void)
{
    return hdd_table;
}


void hdd_set_layout(int table)
{
    hdd_table = table;

    hdd_not_apa =
        (table == FATFMT_TABLE_APA || table == FATFMT_TABLE_EMPTY)
            ? 0
            : (table == FATFMT_TABLE_NONE ? 99 : table);

    printf("HDD: tabela agora %s\n", usbfmt_table_name(table));
}


int hdd_stack_loaded(void)
{
    return atad_loaded;
}


int hdd_init(void)
{
    int result;
    int i;


    /*
     * Disco nao-APA (visto agora ou formatado em MBR/GPT nesta
     * sessao): o driver APA nao e usado, mesmo se ja carregado.
     */
    if (hdd_not_apa) {
        hdd_set_error("HDD nao e APA (MBR/GPT/exFAT?): driver APA nao carregado", hdd_not_apa);
        return -5;
    }


    if (hdd_ready)
        return 0;


    last_error[0] = '\0';

    for (i = 0; i < HDD_SLOTS; i++) {
        memset(&slots[i], 0, sizeof(HddSlot));
        snprintf(slots[i].point, sizeof(slots[i].point), "pfs%d:", i);
    }


    if (!atad_loaded) {

        result = dev9_load();

        if (result < 0) {
            hdd_set_error("DEV9 falhou", result);
            return -1;
        }


        /*
         * ata_bd.irx: o ATAD do PS2SDK com BDM (mesma biblioteca
         * "atad" para o driver APA). Precisa do BDM carregado.
         */
        result = bdm_core_load();

        if (result < 0) {
            hdd_set_error("BDM falhou", result);
            return -1;
        }

        result = hdd_load("ATA_BD", PS2ATAD_irx, size_PS2ATAD_irx, 0, NULL);

        if (result < 0) {
            hdd_set_error("ATAD falhou (sem HDD?)", result);
            return -2;
        }

        atad_loaded = 1;
    }


    /*
     * Como o OPL: o driver APA grava no disco ao iniciar. So
     * carrega se os setores 0-1 forem APA ou zerados; MBR/GPT
     * (exFAT do OPL) ou dados desconhecidos ficam intactos.
     */
    result = hdd_check_layout();

    if (result < 0)
        return result;


    result = hdd_load("PS2HDD", PS2HDD_irx, size_PS2HDD_irx, sizeof(ps2hdd_args), ps2hdd_args);

    if (result < 0) {
        hdd_set_error("PS2HDD (APA) falhou", result);
        return -3;
    }


    result = hdd_load("PS2FS", PS2FS_irx, size_PS2FS_irx, sizeof(ps2fs_args), ps2fs_args);

    if (result < 0) {
        hdd_set_error("PS2FS (PFS) falhou", result);
        return -4;
    }


    hdd_ready = 1;

    printf("HDD: pilha pronta\n");

    return 0;
}


static void hdd_sync_slot(HddSlot *slot)
{
    int result;

    if (!slot->mounted)
        return;

    result = fileXioSync(
        slot->point,
        FXIO_WAIT
    );

    printf("HDD: sync %s (%s) -> %d\n", slot->point, slot->name, result);
}


static void hdd_unmount_slot(HddSlot *slot)
{
    int result;

    if (!slot->mounted)
        return;

    fileXioDevctl(
        slot->point,
        PDIOC_CLOSEALL,
        NULL,
        0,
        NULL,
        0
    );

    hdd_sync_slot(slot);

    result = fileXioUmount(slot->point);

    printf("HDD: umount %s (%s) -> %d\n", slot->point, slot->name, result);

    slot->mounted = 0;
    slot->name[0] = '\0';
}


const char *hdd_mount_partition(const char *name, const char *keep_point)
{
    char block_device[96];
    HddSlot *slot;
    int result;
    int i;


    if (!hdd_ready) {
        hdd_set_error("HDD nao iniciado", 0);
        return NULL;
    }


    /*
     * Ja montada: reutiliza.
     */
    for (i = 0; i < HDD_SLOTS; i++) {

        if (slots[i].mounted &&
            strcmp(slots[i].name, name) == 0) {

            slots[i].last_use = ++use_counter;

            return slots[i].point;
        }
    }


    /*
     * Escolhe um slot: livre; senao, o usado ha mais tempo,
     * desde que nao seja o que o clipboard usa (keep_point).
     */
    slot = NULL;

    for (i = 0; i < HDD_SLOTS; i++) {

        if (!slots[i].mounted) {
            slot = &slots[i];
            break;
        }
    }

    if (!slot) {

        for (i = 0; i < HDD_SLOTS; i++) {

            if (keep_point &&
                strcmp(slots[i].point, keep_point) == 0)
                continue;

            if (!slot ||
                slots[i].last_use < slot->last_use)
                slot = &slots[i];
        }
    }

    if (!slot) {
        hdd_set_error("Sem ponto de montagem livre", 0);
        return NULL;
    }


    hdd_unmount_slot(slot);


    snprintf(
        block_device,
        sizeof(block_device),
        "hdd0:%s",
        name
    );


    result = fileXioMount(
        slot->point,
        block_device,
        FIO_MT_RDWR
    );

    printf(
        "HDD: mount %s em %s -> %d\n",
        block_device,
        slot->point,
        result
    );

    if (result < 0) {
        hdd_set_error("Particao nao montou (nao e PFS?)", result);
        return NULL;
    }


    slot->mounted = 1;
    slot->last_use = ++use_counter;

    snprintf(
        slot->name,
        sizeof(slot->name),
        "%s",
        name
    );

    return slot->point;
}


void hdd_sync_all(void)
{
    int i;

    for (i = 0; i < HDD_SLOTS; i++)
        hdd_sync_slot(&slots[i]);
}


const char *hdd_partition_of(const char *path)
{
    int i;

    if (!path)
        return NULL;

    for (i = 0; i < HDD_SLOTS; i++) {

        if (slots[i].mounted &&
            strncmp(path, slots[i].point, strlen(slots[i].point)) == 0)
            return slots[i].name;
    }

    return NULL;
}


int hdd_mounted_count(void)
{
    int i;
    int count = 0;

    for (i = 0; i < HDD_SLOTS; i++) {
        if (slots[i].mounted)
            count++;
    }

    return count;
}


void hdd_unmount_all(void)
{
    int i;

    for (i = 0; i < HDD_SLOTS; i++)
        hdd_unmount_slot(&slots[i]);
}


unsigned int hdd_total_mb(void)
{
    int sectors;

    if (!hdd_ready)
        return 0;

    sectors = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);

    if (sectors <= 0)
        return 0;

    return (unsigned int)sectors / 2048;
}


/*
 * HDIOC_STATUS: 0 = APA formatado; 1..3 = sem formato/inutilizavel.
 */
int hdd_is_formatted(void)
{
    if (!hdd_ready)
        return 0;

    return fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0) == 0;
}


/*
 * Subparticoes (jogos HDLoader > 1 GB) tem o bit 0 do attr.
 */
#define APA_FLAG_SUB 0x0001

int hdd_partition_count(void)
{
    iox_dirent_t entry;
    int count = 0;
    int fd;

    if (!hdd_ready)
        return -1;

    fd = fileXioDopen("hdd0:");

    if (fd < 0)
        return fd;

    while (fileXioDread(fd, &entry) > 0) {

        if (!(entry.stat.attr & APA_FLAG_SUB))
            count++;
    }

    fileXioDclose(fd);

    return count;
}


/*
 * Zona do PFS: mesmo valor da libhdd (PFS_ZONE_SIZE).
 */
static const int pfs_format_arg[1] = { 8192 };

int hdd_format_apa(HddFormatStepFn step)
{
    static const char *partitions[] = {
        "hdd0:__net",
        "hdd0:__system",
        "hdd0:__sysconf",
        "hdd0:__common",
        NULL
    };

    int result;
    int i;


    if (!hdd_ready) {
        hdd_set_error("HDD nao iniciado", 0);
        return -1;
    }

    last_error[0] = '\0';

    hdd_unmount_all();


    if (step)
        step(0, HDD_FORMAT_STEPS, "APA (tabela de particoes)");

    result = fileXioFormat("hdd0:", NULL, NULL, 0);

    printf("HDD: format hdd0: (APA) -> %d\n", result);

    if (result < 0) {
        hdd_set_error("Formatar APA falhou", result);
        return result;
    }


    for (i = 0; partitions[i]; i++) {

        if (step)
            step(i + 1, HDD_FORMAT_STEPS, partitions[i] + 5);

        result = fileXioFormat(
            "pfs:",
            partitions[i],
            (const char *)pfs_format_arg,
            sizeof(pfs_format_arg)
        );

        printf("HDD: format pfs %s -> %d\n", partitions[i], result);

        if (result < 0) {
            hdd_set_error("Formatar PFS falhou", result);
            return result;
        }
    }

    if (step)
        step(HDD_FORMAT_STEPS, HDD_FORMAT_STEPS, "concluido");

    return 0;
}


/*
 * Livre = total - (particoes + subparticoes), como o wLaunchELF.
 * O HDIOC_FREESECTOR devolve so o "installable" (limitado ao
 * tamanho maximo de uma particao): nao serve aqui.
 */
#define APA_TYPE_FREE 0x0000

unsigned int hdd_free_mb(void)
{
    iox_dirent_t entry;
    unsigned int used = 0;
    unsigned int total;
    int fd;

    total = hdd_total_mb();

    if (total == 0)
        return 0;

    fd = fileXioDopen("hdd0:");

    if (fd < 0)
        return 0;

    while (fileXioDread(fd, &entry) > 0) {

        if (entry.stat.mode == APA_TYPE_FREE)
            continue;

        used += entry.stat.size / 2048;
    }

    fileXioDclose(fd);

    printf("HDD: total %u MB, usado %u MB\n", total, used);

    return used < total ? total - used : 0;
}


/*
 * Tamanhos aceitos pelo APA (o maximo depende do disco:
 * HDIOC_MAXSECTOR).
 */
static const struct {
    unsigned int mb;
    const char *text;
} apa_sizes[] = {
    { 128,   "128M" },
    { 256,   "256M" },
    { 512,   "512M" },
    { 1024,  "1G" },
    { 2048,  "2G" },
    { 4096,  "4G" },
    { 8192,  "8G" },
    { 16384, "16G" },
    { 32768, "32G" }
};

#define APA_SIZE_COUNT ((int)(sizeof(apa_sizes) / sizeof(apa_sizes[0])))


/*
 * Maior tamanho <= limit_mb que nao passa de max_index.
 */
static int apa_size_index(unsigned int limit_mb, int max_index)
{
    int i;

    for (i = max_index; i >= 0; i--) {

        if (apa_sizes[i].mb <= limit_mb)
            return i;
    }

    return -1;
}


int hdd_create_partition(const char *name, unsigned int size_mb)
{
    char path[96];
    unsigned int max_mb;
    unsigned int left;
    int max_index;
    int index;
    int fd;
    int result;


    if (!hdd_ready) {
        hdd_set_error("HDD nao iniciado", 0);
        return -1;
    }

    last_error[0] = '\0';

    if (size_mb == 0 || size_mb % 128) {
        hdd_set_error("Tamanho deve ser multiplo de 128 MB", (int)size_mb);
        return -EINVAL;
    }


    /*
     * Ja existe? (-EACCES = existe, com senha.)
     */
    snprintf(path, sizeof(path), "hdd0:%s", name);

    fd = fileXioOpen(path, FIO_O_RDONLY);

    if (fd >= 0 || fd == -EACCES) {

        if (fd >= 0)
            fileXioClose(fd);

        hdd_set_error("Particao ja existe", fd);
        return -EEXIST;
    }


    max_mb = (u32)fileXioDevctl("hdd0:", HDIOC_MAXSECTOR, NULL, 0, NULL, 0) / 2048;
    max_index = apa_size_index(max_mb, APA_SIZE_COUNT - 1);

    if (max_index < 0) {
        hdd_set_error("Tamanho maximo de particao invalido", (int)max_mb);
        return -1;
    }


    /*
     * Principal: maior tamanho que cabe; sem espaco, tenta menor.
     */
    fd = -ENOSPC;

    for (index = apa_size_index(size_mb, max_index); index >= 0; index--) {

        snprintf(path, sizeof(path), "hdd0:%s,,,%s,PFS", name, apa_sizes[index].text);

        fd = fileXioOpen(path, FIO_O_RDWR | FIO_O_CREAT);

        printf("HDD: criar %s -> %d\n", path, fd);

        if (fd >= 0 || fd != -ENOSPC)
            break;
    }

    if (fd < 0) {
        hdd_set_error(fd == -ENOSPC ? "Sem espaco no HDD" : "Criar particao falhou", fd);
        return fd;
    }

    left = size_mb - apa_sizes[index].mb;


    /*
     * Subparticoes ate completar o tamanho pedido.
     */
    while (left > 0) {

        index = apa_size_index(left, index);

        if (index < 0)
            break;

        result = fileXioIoctl2(
            fd,
            HIOCADDSUB,
            (void *)apa_sizes[index].text,
            strlen(apa_sizes[index].text) + 1,
            NULL,
            0
        );

        printf("HDD: subparticao %s -> %d\n", apa_sizes[index].text, result);

        if (result == -ENOSPC) {

            /*
             * Nao coube: tenta um tamanho menor.
             */
            index--;

            if (index < 0)
                break;

            continue;
        }

        if (result < 0)
            break;   /* -EFBIG: limite de subparticoes */

        left -= apa_sizes[index].mb;
    }

    fileXioClose(fd);


    snprintf(path, sizeof(path), "hdd0:%s", name);

    result = fileXioFormat(
        "pfs:",
        path,
        (const char *)pfs_format_arg,
        sizeof(pfs_format_arg)
    );

    printf("HDD: format pfs %s -> %d\n", path, result);

    if (result < 0) {
        hdd_set_error("Formatar PFS falhou", result);
        return result;
    }

    return (int)(size_mb - left);
}


int hdd_remove_partition(const char *name)
{
    char path[96];
    int result;
    int i;


    if (!hdd_ready) {
        hdd_set_error("HDD nao iniciado", 0);
        return -1;
    }

    last_error[0] = '\0';


    for (i = 0; i < HDD_SLOTS; i++) {

        if (slots[i].mounted && strcmp(slots[i].name, name) == 0)
            hdd_unmount_slot(&slots[i]);
    }


    snprintf(path, sizeof(path), "hdd0:%s", name);

    result = fileXioRemove(path);

    printf("HDD: remove %s -> %d\n", path, result);

    if (result < 0) {
        hdd_set_error("Excluir particao falhou", result);
        return result;
    }

    return 0;
}


const char *hdd_error(void)
{
    return last_error;
}
