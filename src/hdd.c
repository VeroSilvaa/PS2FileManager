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
 * Aqui a particao e escolhida pelo usuario, em vez de fixa.
 */

#define HDD_MOUNT_POINT "pfs0:"

extern unsigned char PS2ATAD_irx[];
extern unsigned int size_PS2ATAD_irx;

extern unsigned char PS2HDD_irx[];
extern unsigned int size_PS2HDD_irx;

extern unsigned char PS2FS_irx[];
extern unsigned int size_PS2FS_irx;

static int hdd_ready = 0;
static int hdd_mounted = 0;

static char mounted_name[64];
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


static int hdd_load(const char *name, void *buffer, unsigned int size)
{
    int id;
    int result = 0;

    id = SifExecModuleBuffer(
        buffer,
        size,
        0,
        NULL,
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


    if (hdd_ready)
        return 0;


    last_error[0] = '\0';


    result = dev9_load();

    if (result < 0) {
        hdd_set_error("DEV9 falhou", result);
        return -1;
    }


    result = hdd_load("PS2ATAD", PS2ATAD_irx, size_PS2ATAD_irx);

    if (result < 0) {
        hdd_set_error("ATAD falhou (sem HDD?)", result);
        return -2;
    }


    result = hdd_load("PS2HDD", PS2HDD_irx, size_PS2HDD_irx);

    if (result < 0) {
        hdd_set_error("PS2HDD (APA) falhou", result);
        return -3;
    }


    result = hdd_load("PS2FS", PS2FS_irx, size_PS2FS_irx);

    if (result < 0) {
        hdd_set_error("PS2FS (PFS) falhou", result);
        return -4;
    }


    hdd_ready = 1;

    printf("HDD: pilha pronta\n");

    return 0;
}


int hdd_mount_partition(const char *name)
{
    char block_device[96];
    int result;


    if (!hdd_ready) {
        hdd_set_error("HDD nao iniciado", 0);
        return -1;
    }


    if (hdd_mounted)
        hdd_unmount();


    snprintf(
        block_device,
        sizeof(block_device),
        "hdd0:%s",
        name
    );


    result = fileXioMount(
        HDD_MOUNT_POINT,
        block_device,
        FIO_MT_RDWR
    );

    printf(
        "HDD: mount %s em %s -> %d\n",
        block_device,
        HDD_MOUNT_POINT,
        result
    );

    if (result < 0) {
        hdd_set_error("Particao nao montou (nao e PFS?)", result);
        return result;
    }


    hdd_mounted = 1;

    snprintf(
        mounted_name,
        sizeof(mounted_name),
        "%s",
        name
    );

    return 0;
}


void hdd_unmount(void)
{
    int result;


    if (!hdd_mounted)
        return;


    /*
     * Grava os dados pendentes antes de desmontar.
     */
    fileXioDevctl(
        HDD_MOUNT_POINT,
        PDIOC_CLOSEALL,
        NULL,
        0,
        NULL,
        0
    );

    result = fileXioSync(
        HDD_MOUNT_POINT,
        FXIO_WAIT
    );

    printf("HDD: sync %s -> %d\n", HDD_MOUNT_POINT, result);

    result = fileXioUmount(HDD_MOUNT_POINT);

    printf("HDD: umount %s -> %d\n", HDD_MOUNT_POINT, result);


    hdd_mounted = 0;
    mounted_name[0] = '\0';
}


int hdd_is_mounted(void)
{
    return hdd_mounted;
}


const char *hdd_mounted_name(void)
{
    return mounted_name;
}


const char *hdd_error(void)
{
    return last_error;
}
