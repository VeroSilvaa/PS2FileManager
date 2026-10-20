#include <stdio.h>
#include <string.h>

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


int hdd_init(void)
{
    int result;
    int i;


    if (hdd_ready)
        return 0;


    last_error[0] = '\0';

    for (i = 0; i < HDD_SLOTS; i++) {
        memset(&slots[i], 0, sizeof(HddSlot));
        snprintf(slots[i].point, sizeof(slots[i].point), "pfs%d:", i);
    }


    result = dev9_load();

    if (result < 0) {
        hdd_set_error("DEV9 falhou", result);
        return -1;
    }


    result = hdd_load("PS2ATAD", PS2ATAD_irx, size_PS2ATAD_irx, 0, NULL);

    if (result < 0) {
        hdd_set_error("ATAD falhou (sem HDD?)", result);
        return -2;
    }


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


const char *hdd_error(void)
{
    return last_error;
}
