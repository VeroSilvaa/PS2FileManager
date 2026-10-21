#include <stdio.h>
#include <string.h>
#include <errno.h>

/*
 * fileXioDevctl nao tem equivalente POSIX.
 */
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include "usb.h"
#include "usbfmt.h"
#include "bdmcore.h"

#define FATFMT_DEVICE "fatfmt:"


int usbfmt_ready(void)
{
    return usb_format_loaded();
}


/*
 * Todos os discos do fatfmt ("usb" e "ata"), so do tipo 'name'.
 */
static int fatfmt_list_by_name(const char *name, fatfmt_disk_t *disks, int max)
{
    static fatfmt_disk_t buffer[FATFMT_MAX_DISKS] __attribute__((aligned(64)));
    int result;
    int count = 0;
    int i;

    result = fileXioDevctl(FATFMT_DEVICE, FATFMT_DEVCTL_LIST, NULL, 0, buffer, sizeof(buffer));

    printf("FM USBFMT: list -> %d\n", result);

    if (result < 0)
        return result;

    for (i = 0; i < result && count < max; i++) {

        if (strcmp(buffer[i].name, name) == 0)
            disks[count++] = buffer[i];
    }

    return count;
}


int usbfmt_list(fatfmt_disk_t *disks, int max)
{
    if (!usbfmt_ready())
        return -ENODEV;

    return fatfmt_list_by_name("usb", disks, max);
}


int usbfmt_hdd_disk(fatfmt_disk_t *disk)
{
    int result;

    if (!bdm_fatfmt_load())
        return -ENODEV;

    result = fatfmt_list_by_name("ata", disk, 1);

    if (result == 0)
        return -ENODEV;

    return result < 0 ? result : 0;
}


static int usbfmt_request(int cmd, const fatfmt_disk_t *disk, unsigned int value)
{
    static fatfmt_request_t request __attribute__((aligned(64)));
    int result;

    if (!usbfmt_ready())
        return -ENODEV;

    memset(&request, 0, sizeof(request));
    memcpy(request.name, disk->name, sizeof(request.name));
    request.dev_nr = disk->dev_nr;
    request.value = value;

    result = fileXioDevctl(FATFMT_DEVICE, cmd, &request, sizeof(request), NULL, 0);

    printf(
        "FM USBFMT: %s %s%u valor %u -> %d\n",
        cmd == FATFMT_DEVCTL_TABLE
            ? "tabela"
            : cmd == FATFMT_DEVCTL_WIPE ? "apagar" : "formatar",
        disk->name,
        disk->dev_nr,
        value,
        result
    );

    return result;
}


int usbfmt_table(const fatfmt_disk_t *disk, unsigned int table)
{
    return usbfmt_request(FATFMT_DEVCTL_TABLE, disk, table);
}


int usbfmt_format(const fatfmt_disk_t *disk, unsigned int fs)
{
    return usbfmt_request(FATFMT_DEVCTL_FORMAT, disk, fs);
}


int usbfmt_wipe(const fatfmt_disk_t *disk)
{
    return usbfmt_request(FATFMT_DEVCTL_WIPE, disk, 0);
}


unsigned int usbfmt_size_mb(const fatfmt_disk_t *disk)
{
    unsigned long long sectors =
        ((unsigned long long)disk->sectors_hi << 32) | disk->sectors_lo;

    return (unsigned int)(sectors * disk->sector_size / (1024 * 1024));
}


const char *usbfmt_table_name(unsigned int table)
{
    switch (table) {
        case FATFMT_TABLE_MBR:   return "MBR";
        case FATFMT_TABLE_GPT:   return "GPT";
        case FATFMT_TABLE_APA:   return "APA";
        case FATFMT_TABLE_EMPTY: return "vazio";
    }

    return "sem tabela";
}


const char *usbfmt_fs_name(unsigned int fs)
{
    switch (fs) {
        case FATFMT_FS_FAT:   return "FAT16";
        case FATFMT_FS_FAT32: return "FAT32";
        case FATFMT_FS_EXFAT: return "exFAT";
    }

    return "sem formato";
}


const char *usbfmt_error_text(int code)
{
    if (code >= 0)
        return "ok";

    code = -code;

    if (code == FATFMT_ERR_NO_PARTITION)
        return "sem tabela/particao";

    if (code == FATFMT_ERR_TOO_SMALL)
        return "midia pequena demais";

    if (code == FATFMT_ERR_IO)
        return "erro de leitura/gravacao";


    if (code == FATFMT_ERR_FATFS + 14)
        return "FatFs: tamanho invalido para este formato";

    if (code > FATFMT_ERR_FATFS)
        return "erro do FatFs";

    if (code == ENODEV)
        return "pendrive nao encontrado";

    return "erro";
}
