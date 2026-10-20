#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dirent.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>

#include "usb.h"
#include "bdmcore.h"

/*
 * Mesma ordem de carga do OPL/wLaunchELF:
 * usbd (pilha USB) -> bdm (dispositivos de bloco) ->
 * bdmfs_fatfs (FAT32/exFAT em usbN:) -> usbmass_bd (pendrive).
 *
 * A deteccao do pendrive e assincrona: depois de carregar,
 * espera-se ate USB_WAIT_SECONDS pela montagem de usbN:.
 */

#define USB_WAIT_SECONDS 5

extern unsigned char USBD_irx[];
extern unsigned int size_USBD_irx;

extern unsigned char USBMASS_BD_irx[];
extern unsigned int size_USBMASS_BD_irx;

static int usb_loaded = 0;

static char last_error[128];


static void usb_set_error(const char *message, int code)
{
    snprintf(
        last_error,
        sizeof(last_error),
        "%s (%d)",
        message,
        code
    );

    printf("USB: %s\n", last_error);
}


static int usb_load(const char *name, void *buffer, unsigned int size)
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
        "USB: %s id %d ret %d\n",
        name,
        id,
        result
    );

    if (id < 0)
        return id;

    /*
     * ret 1 = NO_RESIDENT_END: o modulo descarregou.
     */
    if (result == 1)
        return -1;

    return 0;
}


int usb_available(int unit)
{
    char root[16];
    DIR *dir;

    if (!usb_loaded ||
        unit < 0 ||
        unit >= USB_UNITS)
        return 0;

    snprintf(root, sizeof(root), USB_PREFIX "%d:/", unit);

    dir = opendir(root);

    if (!dir)
        return 0;

    closedir(dir);

    return 1;
}


static int usb_count_available(void)
{
    int unit;
    int count = 0;

    for (unit = 0; unit < USB_UNITS; unit++) {
        if (usb_available(unit))
            count++;
    }

    return count;
}


int usb_init(void)
{
    int result;
    int count;


    if (usb_loaded)
        return 0;


    last_error[0] = '\0';


    result = usb_load("USBD", USBD_irx, size_USBD_irx);

    if (result < 0) {
        usb_set_error("USBD falhou", result);
        return -1;
    }


    /*
     * BDM + bdmfs_fatfs: compartilhados com o HDD (ata_bd).
     */
    result = bdm_core_load();

    if (result < 0) {
        usb_set_error("BDM/BDMFS_FATFS falhou", result);
        return -2;
    }


    result = usb_load("USBMASS_BD", USBMASS_BD_irx, size_USBMASS_BD_irx);

    if (result < 0) {
        usb_set_error("USBMASS_BD falhou", result);
        return -4;
    }


    usb_loaded = 1;


    count = usb_wait_mount();

    if (count == 0)
        usb_set_error("Nenhum pendrive encontrado", 0);

    return 0;
}


int usb_wait_mount(void)
{
    clock_t start;
    clock_t tick;
    int count;


    /*
     * Espera todas as unidades ou o tempo limite
     * (com um so pendrive, a espera vai ate o fim).
     */
    start = clock();

    do {
        count = usb_count_available();

        if (count >= USB_UNITS)
            break;

        tick = clock();

        while (clock() - tick < CLOCKS_PER_SEC / 4) {
        }

    } while (clock() - start <
             (clock_t)USB_WAIT_SECONDS * CLOCKS_PER_SEC);


    printf(
        "USB: %d unidade(s) montada(s) em %d ms\n",
        count,
        (int)((clock() - start) * 1000 / CLOCKS_PER_SEC)
    );

    return count;
}


/*
 * Formatacao (modulo proprio, iop/fatfmt): carregado so quando
 * o usuario pede para formatar, depois dos pendrives montados.
 */
int usb_format_loaded(void)
{
    return usb_loaded && bdm_fatfmt_load();
}


int usb_ready(void)
{
    return usb_loaded;
}


const char *usb_error(void)
{
    return last_error;
}
