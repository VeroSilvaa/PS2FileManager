#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>

#include <tamtypes.h>
#include <kernel.h>
#include <sbv_patches.h>
#include <elf-loader.h>

#include "elfrun.h"
#include "hdd.h"
#include "usb.h"
#include "fatfmt_devctl.h"
#include "mp3player.h"

static char last_error[128];


int elf_is_elf(const char *name)
{
    int length = name ? strlen(name) : 0;

    return length > 4 && strcasecmp(name + length - 4, ".elf") == 0;
}


int elf_check(const char *path)
{
    unsigned char magic[4];
    int fd;
    int got;

    fd = open(path, O_RDONLY);

    if (fd < 0)
        return 0;

    got = read(fd, magic, 4);

    close(fd);

    return got == 4 && magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}


const char *elf_error(void)
{
    return last_error;
}


/*
 * Caminho apos "dev:" (sem a barra inicial).
 */
static const char *after_device(const char *path)
{
    const char *colon = strchr(path, ':');

    if (!colon)
        return path;

    colon++;

    while (*colon == '/')
        colon++;

    return colon;
}


int elf_run(const char *path)
{
    static char filename[1024];
    static char partition[96];
    const char *rest = after_device(path);


    last_error[0] = '\0';
    partition[0] = '\0';


    if (!elf_check(path)) {
        snprintf(last_error, sizeof(last_error), "nao e um ELF valido");
        return -1;
    }


    if (strncmp(path, "pfs", 3) == 0) {

        /*
         * HDD APA: o loader le "pfs:" (unidade 0) e o app recebe
         * "hdd0:<particao>:pfs:/caminho" (formato padrao do PS2).
         * A particao precisa estar montada em pfs0:.
         */
        const char *name = hdd_partition_of(path);
        char part_name[64];
        const char *point;

        if (!name) {
            snprintf(last_error, sizeof(last_error), "particao do ELF nao encontrada");
            return -1;
        }

        snprintf(part_name, sizeof(part_name), "%s", name);

        if (strncmp(path, "pfs0:", 5) != 0) {

            hdd_unmount_all();

            point = hdd_mount_partition(part_name, NULL);

            if (!point || strcmp(point, "pfs0:") != 0) {
                snprintf(last_error, sizeof(last_error), "nao montou %s em pfs0:", part_name);
                return -1;
            }
        }

        hdd_sync_all();

        snprintf(partition, sizeof(partition), "hdd0:%s:", part_name);
        snprintf(filename, sizeof(filename), "pfs:/%s", rest);

    } else if (strncmp(path, USB_PREFIX, strlen(USB_PREFIX)) == 0 &&
               hdd_layout_table() != FATFMT_TABLE_MBR &&
               hdd_layout_table() != FATFMT_TABLE_GPT) {

        /*
         * Pendrive: "massN:" e o nome que os apps conhecem. Sem HDD
         * exFAT montado, a numeracao do mass e a mesma do usbN.
         */
        snprintf(filename, sizeof(filename), "mass%c:/%s",
                 path[strlen(USB_PREFIX)], rest);

    } else {

        snprintf(filename, sizeof(filename), "%s", path);
    }


    printf("FM ELF: executar %s | particao '%s' | arquivo %s\n", path, partition, filename);


    /*
     * Musica: a thread do player e o audsrv param antes.
     */
    mp3player_stop();

    /*
     * O LOADFILE da BIOS so carrega de alguns dispositivos sem
     * este patch (exigido pelo elf-loader).
     */
    sbv_patch_disable_prefix_check();

    LoadELFFromFileWithPartition(filename, partition[0] ? partition : NULL, 0, NULL);

    /*
     * So chega aqui se o loader nao encontrou o arquivo.
     */
    snprintf(last_error, sizeof(last_error), "o loader nao achou %.100s", filename);

    return -1;
}
