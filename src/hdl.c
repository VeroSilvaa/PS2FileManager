#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>

/*
 * Criar particao, subparticoes (ioctl2), devctl e remover particao
 * nao tem equivalente POSIX. A ISO de origem continua em POSIX.
 */
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include "hdd.h"
#include "hdl.h"

extern unsigned char HDLWRITE_irx[];
extern unsigned int size_HDLWRITE_irx;

#define HDL_CHUNK            (32 * 1024)

#define HDL_INFO_MAGIC       0xDEADFEED
#define HDL_GAME_DATA_OFFSET 0x100000

#define HDL_MAX_PARTS        65
#define HDL_MAIN_RESERVED    0x2000   /* setores de 512 */
#define HDL_SUB_RESERVED     4

/*
 * Tipo de disco do cdvdman (SCECdPS2CD / SCECdPS2DVD).
 */
#define HDL_DISC_CD          0x12
#define HDL_DISC_DVD         0x14

/*
 * DVD de uma camada: acima disso e DVD9 (precisa do layer break).
 */
#define HDL_DVD5_MAX_SECTORS 2295104

/*
 * Cabecalho HDL (1024 bytes). Unidades conferidas no cdvdman do OPL:
 * part_offset em setores de CD do jogo, data_start em LBA absoluto
 * do HDD, part_size em bytes.
 */
typedef struct {
    u32 part_offset;
    u32 data_start;
    u32 part_size;
} hdl_part_spec_t;

typedef struct {
    u32 magic;
    u16 reserved;
    u16 version;
    char gamename[160];
    u8 hdl_compat_flags;
    u8 ops2l_compat_flags;
    u8 dma_type;
    u8 dma_mode;
    char startup[60];   /* sem o ";1" */
    u32 layer1_start;
    u32 discType;
    int num_partitions;
    hdl_part_spec_t part_specs[HDL_MAX_PARTS];
} hdl_header_t;

typedef char hdl_header_size_check[sizeof(hdl_header_t) == 1024 ? 1 : -1];

/*
 * Tamanhos de particao APA (setores de 512) e o texto do ps2hdd.
 */
static const struct {
    u32 sectors;
    const char *text;
} part_sizes[] = {
    { 0x40000,  "128M" },
    { 0x80000,  "256M" },
    { 0x100000, "512M" },
    { 0x200000, "1G" },
    { 0x400000, "2G" },
    { 0x800000, "4G" }
};

#define PART_SIZE_COUNT ((int)(sizeof(part_sizes) / sizeof(part_sizes[0])))

static int hdl_ready = 0;

static char last_error[128];

static HdlProgressFn progress_fn = NULL;


void hdl_set_progress(HdlProgressFn fn)
{
    progress_fn = fn;
}


static void hdl_progress(int phase, unsigned long long done, unsigned long long total)
{
    if (progress_fn)
        progress_fn(phase, done, total);
}


static void hdl_set_error(const char *message, int code)
{
    snprintf(last_error, sizeof(last_error), "%s (%d)", message, code);

    printf("FM HDL: %s\n", last_error);
}


int hdl_init(void)
{
    int id;
    int result = 0;

    if (hdl_ready)
        return 0;

    last_error[0] = '\0';

    if (hdd_init() < 0) {
        hdl_set_error(hdd_error(), 0);
        return -1;
    }

    id = SifExecModuleBuffer(HDLWRITE_irx, size_HDLWRITE_irx, 0, NULL, &result);

    printf("FM HDL: HDLWRITE id %d ret %d\n", id, result);

    if (id < 0 || result == 1) {
        hdl_set_error("HDLWRITE falhou", id < 0 ? id : result);
        return -2;
    }

    hdl_ready = 1;

    return 0;
}


/* ============================================================
 * Nome e plano das particoes
 * ============================================================ */

/*
 * "SLUS_999.98" -> "SLUS-99998" (formato do HDLGameInstaller).
 */
static int hdl_disc_id(const char *game_id, char *disc_id)
{
    if (strlen(game_id) != 11 ||
        game_id[4] != '_' ||
        game_id[8] != '.')
        return 0;

    memcpy(disc_id, game_id, 4);
    disc_id[4] = '-';
    memcpy(disc_id + 5, game_id + 5, 3);
    memcpy(disc_id + 8, game_id + 9, 2);
    disc_id[10] = '\0';

    return 1;
}


/*
 * "PP.SLUS-99998.HDL." + ate 14 caracteres do titulo (APA: 32).
 * Fora de letras, numeros e "_" vira "_".
 */
static void hdl_part_name(char *out, const char *disc_id, const char *title)
{
    int length;
    int i;

    length = sprintf(out, "PP.%s.HDL.", disc_id);

    for (i = 0; title[i] && i < 14; i++) {
        out[length + i] =
            (isalnum((unsigned char)title[i]) || title[i] == '_')
                ? title[i]
                : '_';
    }

    out[length + i] = '\0';
}


/*
 * Ja existe uma particao "PP.<disc_id>." (mesmo jogo)?
 */
static int hdl_already_installed(const char *disc_id, char *found, int size)
{
    char prefix[24];
    struct dirent *de;
    DIR *dir;
    int length;

    length = snprintf(prefix, sizeof(prefix), "PP.%s.", disc_id);

    dir = opendir("hdd0:");

    if (!dir)
        return 0;

    while ((de = readdir(dir)) != NULL) {

        if (strncmp(de->d_name, prefix, length) == 0) {
            snprintf(found, size, "%s", de->d_name);
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);

    return 0;
}


int hdl_game_status(const IsoGameInfo *game, char *found, int size)
{
    char disc_id[16];

    found[0] = '\0';

    if (!hdl_disc_id(game->game_id, disc_id))
        return HDL_GAME_BAD_ID;

    if (game->sectors > HDL_DVD5_MAX_SECTORS)
        return HDL_GAME_DVD9;

    if (hdl_already_installed(disc_id, found, size))
        return HDL_GAME_INSTALLED;

    return HDL_GAME_OK;
}


/*
 * Escolhe o tamanho de uma particao para 'needed' setores de dados
 * (512): a menor que caiba, sem desperdicar 128 MB ou mais (nesse
 * caso uma menor + subparticoes), limitada ao maximo do disco.
 */
static int hdl_pick_size(u32 needed, u32 reserved, u32 max_sectors)
{
    int i;
    int last = -1;

    for (i = 0; i < PART_SIZE_COUNT && part_sizes[i].sectors <= max_sectors; i++) {

        last = i;

        if (part_sizes[i].sectors - reserved >= needed) {

            if (i > 0 &&
                part_sizes[i].sectors - reserved - needed >= part_sizes[0].sectors)
                return i - 1;

            return i;
        }
    }

    return last;
}


static u32 hdl_capacity(u32 part_sectors, u32 reserved)
{
    /*
     * Inteiro de setores de CD (4 x 512), como o hdlwrite.irx.
     */
    return ((part_sectors - reserved) / 4) * 4;
}


/* ============================================================
 * Instalacao
 * ============================================================ */

static int hdl_create_partition(
    const char *part_name,
    const IsoGameInfo *game,
    const char *title,
    hdl_header_t *header)
{
    char command[64];
    unsigned char zero[512];
    u32 remaining;
    u32 max_sectors;
    u32 free_sectors;
    u32 total_sectors;
    u32 offset;
    int plan[HDL_MAX_PARTS];
    int nparts;
    int fd;
    int result;
    int i;


    /*
     * Plano: particao principal + subparticoes.
     */
    max_sectors = fileXioDevctl("hdd0:", HDIOC_MAXSECTOR, NULL, 0, NULL, 0);

    remaining = game->sectors * 4;
    total_sectors = 0;
    nparts = 0;

    while (remaining > 0) {

        u32 reserved = nparts == 0 ? HDL_MAIN_RESERVED : HDL_SUB_RESERVED;
        u32 capacity;
        int size;

        if (nparts >= HDL_MAX_PARTS) {
            hdl_set_error("jogo grande demais (subparticoes)", nparts);
            return -1;
        }

        size = hdl_pick_size(remaining, reserved, max_sectors);

        if (size < 0) {
            hdl_set_error("HDD sem tamanho de particao valido", (int)max_sectors);
            return -1;
        }

        plan[nparts++] = size;
        total_sectors += part_sizes[size].sectors;

        capacity = hdl_capacity(part_sizes[size].sectors, reserved);
        remaining -= capacity > remaining ? remaining : capacity;
    }


    /*
     * Livre = total - soma das particoes (hdd_free_mb). O
     * HDIOC_FREESECTOR nao e o espaco livre: e uma mascara dos
     * tamanhos "instalaveis" (HDD.IRX da Sony) e, com buracos na
     * cadeia APA (particao excluida), cai para 128 MB. Se a criacao
     * falhar, a particao incompleta e apagada.
     */
    free_sectors = hdd_free_mb() * 2048;

    printf(
        "FM HDL: plano %d particao(oes), %u MB; livre %u MB\n",
        nparts,
        total_sectors / 2048,
        free_sectors / 2048
    );

    if (free_sectors < total_sectors) {
        snprintf(
            last_error,
            sizeof(last_error),
            "HDD sem espaco: precisa %u MB, livre %u MB",
            total_sectors / 2048,
            free_sectors / 2048
        );
        printf("FM HDL: %s\n", last_error);
        return -1;
    }


    /*
     * Cria a principal e adiciona as subparticoes.
     */
    snprintf(command, sizeof(command), "hdd0:%s,,,%s,HDL", part_name, part_sizes[plan[0]].text);

    fd = fileXioOpen(command, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0644);

    printf("FM HDL: criar %s -> %d\n", command, fd);

    if (fd < 0) {
        hdl_set_error("criar particao", fd);
        return fd;
    }

    /*
     * Zera o inicio da area de atributos: uma instalacao
     * interrompida nao deixa lixo de particao antiga.
     */
    memset(zero, 0, sizeof(zero));
    fileXioWrite(fd, zero, sizeof(zero));

    for (i = 1; i < nparts; i++) {

        result = fileXioIoctl2(
            fd,
            HIOCADDSUB,
            (void *)part_sizes[plan[i]].text,
            strlen(part_sizes[plan[i]].text) + 1,
            NULL,
            0
        );

        printf("FM HDL: subparticao %d (%s) -> %d\n", i, part_sizes[plan[i]].text, result);

        if (result < 0) {
            hdl_set_error("criar subparticao", result);
            fileXioClose(fd);
            return result;
        }
    }


    /*
     * Cabecalho: fatias com o setor real de cada particao.
     */
    memset(header, 0, sizeof(hdl_header_t));

    header->magic = HDL_INFO_MAGIC;
    header->version = 1;

    snprintf(header->gamename, sizeof(header->gamename), "%s", title);
    snprintf(header->startup, sizeof(header->startup), "%s", game->game_id);

    header->layer1_start = 0;
    header->discType = game->is_dvd ? HDL_DISC_DVD : HDL_DISC_CD;
    header->num_partitions = nparts;

    offset = 0;

    for (i = 0; i < nparts; i++) {

        u32 reserved = i == 0 ? HDL_MAIN_RESERVED : HDL_SUB_RESERVED;
        int start;
        int size;
        u32 cd_sectors;

        start = fileXioIoctl2(fd, HIOCGETPARTSTART, &i, sizeof(i), NULL, 0);
        size = fileXioIoctl2(fd, HIOCGETSIZE, &i, sizeof(i), NULL, 0);

        if (start < 0 || size <= (int)reserved) {
            hdl_set_error("ler inicio/tamanho da fatia", start < 0 ? start : size);
            fileXioClose(fd);
            return -1;
        }

        cd_sectors = hdl_capacity(size, reserved) / 4;

        if (cd_sectors > game->sectors - offset)
            cd_sectors = game->sectors - offset;

        header->part_specs[i].part_offset = offset;
        header->part_specs[i].data_start = (u32)start + reserved;
        header->part_specs[i].part_size = cd_sectors * 2048;

        printf(
            "FM HDL: fatia %d: LBA 0x%08x, dados 0x%08x, CD %u..%u\n",
            i,
            start,
            header->part_specs[i].data_start,
            offset,
            offset + cd_sectors
        );

        offset += cd_sectors;
    }

    if (offset < game->sectors) {
        hdl_set_error("particoes menores que o jogo", (int)offset);
        fileXioClose(fd);
        return -1;
    }


    result = fileXioLseek(fd, HDL_GAME_DATA_OFFSET, SEEK_SET);

    if (result >= 0)
        result = fileXioWrite(fd, header, sizeof(hdl_header_t));

    fileXioClose(fd);

    printf("FM HDL: cabecalho -> %d\n", result);

    if (result != (int)sizeof(hdl_header_t)) {
        hdl_set_error("gravar cabecalho", result);
        return -1;
    }

    return 0;
}


/*
 * Grava (phase = WRITE) ou confere (phase = VERIFY) os dados.
 */
static int hdl_stream(
    const char *iso_path,
    const char *part_name,
    unsigned long long total,
    int phase,
    unsigned char *buffer,
    unsigned char *other)
{
    char device[48];
    unsigned long long done;
    unsigned int size;
    int source;
    int target;
    int result;
    int ok = 1;


    source = open(iso_path, O_RDONLY);

    if (source < 0) {
        hdl_set_error("abrir ISO", source);
        return -1;
    }

    snprintf(device, sizeof(device), "hdlw:%s", part_name);

    target = fileXioOpen(
        device,
        phase == HDL_PHASE_WRITE ? FIO_O_WRONLY : FIO_O_RDONLY,
        0
    );

    if (target < 0) {
        close(source);
        hdl_set_error("abrir hdlw:", target);
        return -1;
    }


    hdl_progress(phase, 0, total);

    for (done = 0; done < total && ok; done += size) {

        size = HDL_CHUNK;

        if (size > total - done)
            size = (unsigned int)(total - done);

        result = read(source, buffer, size);

        if (result != (int)size) {
            printf("FM HDL: read ISO em %llu -> %d\n", done, result);
            hdl_set_error("ler ISO", result);
            ok = 0;
            break;
        }

        if (phase == HDL_PHASE_WRITE) {

            result = fileXioWrite(target, buffer, size);

            if (result != (int)size) {
                printf("FM HDL: write em %llu -> %d\n", done, result);
                hdl_set_error("gravar no HDD", result);
                ok = 0;
            }

        } else {

            result = fileXioRead(target, other, size);

            if (result != (int)size) {
                printf("FM HDL: read HDD em %llu -> %d\n", done, result);
                hdl_set_error("ler do HDD", result);
                ok = 0;

            } else if (memcmp(buffer, other, size) != 0) {

                unsigned int i;

                for (i = 0; i < size && buffer[i] == other[i]; i++)
                    ;

                printf(
                    "FM HDL: DIFERENTE no byte %llu (setor de CD %llu)\n",
                    done + i,
                    (done + i) / 2048
                );

                hdl_set_error("conferencia: dados diferentes", (int)((done + i) / 2048));
                ok = 0;
            }
        }

        if (ok)
            hdl_progress(phase, done + size, total);
    }


    close(source);

    result = fileXioClose(target);

    if (ok && result < 0) {
        hdl_set_error("fechar hdlw:", result);
        ok = 0;
    }

    return ok ? 0 : -1;
}


/* ============================================================
 * Conferencia "como o OPL"
 *
 * Independente do hdlwrite: acha o jogo, le o cabecalho e le
 * setores da ISO exatamente como o OPL (src/hdd.c e
 * modules/iopcore/cdvdman/device-hdd.c), direto do disco por LBA.
 * ============================================================ */

#define HDL_FS_MAGIC      0x1337
#define APA_FLAG_SUB      0x0001

/*
 * Setores de CD conferidos por fatia: primeiro, ultimo e
 * pontos igualmente espacados no meio.
 */
#define OPL_SAMPLES       16

/*
 * HDIOC_READSECTOR: no maximo 2 setores (1 KB) por chamada,
 * como o OPL.
 */
static int opl_read_lba(u32 lba, u32 count, void *out)
{
    hddAtaTransfer_t args;
    unsigned char *dest = out;
    u32 n;
    int result;

    while (count > 0) {

        n = count > 2 ? 2 : count;

        args.lba = lba;
        args.size = n;

        result = fileXioDevctl("hdd0:", HDIOC_READSECTOR, &args, sizeof(args), dest, n * 512);

        if (result != 0)
            return result < 0 ? result : -1;

        lba += n;
        count -= n;
        dest += n * 512;
    }

    return 0;
}


/*
 * LBA do cabecalho, pelo mesmo caminho do hddGetHDLGamelist do OPL.
 */
static int opl_find_header(const char *part_name, u32 *header_lba)
{
    iox_dirent_t dirent;
    int found = 0;
    int fd;

    fd = fileXioDopen("hdd0:");

    if (fd < 0)
        return fd;

    while (fileXioDread(fd, &dirent) > 0) {

        if (dirent.stat.mode == HDL_FS_MAGIC &&
            !(dirent.stat.attr & APA_FLAG_SUB) &&
            strcmp(dirent.name, part_name) == 0) {

            *header_lba = dirent.stat.private_5 + (HDL_GAME_DATA_OFFSET + 4096) / 512;
            found = 1;
            break;
        }
    }

    fileXioDclose(fd);

    return found ? 0 : -1;
}


static int opl_check_header(const hdl_header_t *h, const IsoGameInfo *game)
{
    u32 offset = 0;
    int i;

    if (h->num_partitions < 1 || h->num_partitions > HDL_MAX_PARTS)
        return -1;

    if (h->discType != (game->is_dvd ? HDL_DISC_DVD : HDL_DISC_CD))
        return -2;

    if (strncmp(h->startup, game->game_id, sizeof(h->startup)) != 0)
        return -3;

    for (i = 0; i < h->num_partitions; i++) {

        if (h->part_specs[i].part_offset != offset ||
            h->part_specs[i].part_size == 0 ||
            h->part_specs[i].part_size % 2048)
            return -4;

        offset += h->part_specs[i].part_size / 2048;
    }

    if (offset != game->sectors)
        return -5;

    return 0;
}


static int hdl_check_opl(
    const char *iso_path,
    const char *part_name,
    const IsoGameInfo *game,
    unsigned char *buffer,
    unsigned char *other)
{
    hdl_header_t *h = (hdl_header_t *)buffer;
    hdl_part_spec_t specs[HDL_MAX_PARTS];
    u32 header_lba;
    int nparts;
    int checked = 0;
    int source;
    int result;
    int i;
    int s;


    result = opl_find_header(part_name, &header_lba);

    if (result < 0) {
        hdl_set_error("OPL: particao HDL nao encontrada na lista", result);
        return -1;
    }

    result = opl_read_lba(header_lba, 2, buffer);

    if (result < 0) {
        hdl_set_error("OPL: ler cabecalho", result);
        return -1;
    }

    printf(
        "FM HDL: OPL cabecalho LBA 0x%08x | '%.40s' | boot %s | tipo 0x%02x | %d fatia(s)\n",
        header_lba,
        h->gamename,
        h->startup,
        (unsigned int)h->discType,
        h->num_partitions
    );

    result = opl_check_header(h, game);

    if (result < 0) {
        hdl_set_error("OPL: cabecalho invalido", result);
        return -1;
    }

    nparts = h->num_partitions;
    memcpy(specs, h->part_specs, sizeof(specs));


    source = open(iso_path, O_RDONLY);

    if (source < 0) {
        hdl_set_error("OPL: abrir ISO", source);
        return -1;
    }

    for (i = 0; i < nparts; i++) {

        u32 count = specs[i].part_size / 2048;

        for (s = 0; s < OPL_SAMPLES; s++) {

            u32 lsn;
            u32 lba;
            off_t position;

            lsn = specs[i].part_offset +
                  (u32)((unsigned long long)(count - 1) * s / (OPL_SAMPLES - 1));

            /*
             * Mesma conta do cdvdman do OPL.
             */
            lba = specs[i].data_start + ((lsn - specs[i].part_offset) << 2);

            position = (off_t)lsn * 2048;

            if (sizeof(off_t) < 8 && lsn >= 0x100000)
                continue;   /* > 2 GB sem off_t de 64 bits */

            if (lseek(source, position, SEEK_SET) != position ||
                read(source, buffer, 2048) != 2048) {
                close(source);
                hdl_set_error("OPL: ler ISO", (int)lsn);
                return -1;
            }

            result = opl_read_lba(lba, 4, other);

            if (result < 0) {
                close(source);
                hdl_set_error("OPL: ler HDD", (int)lsn);
                return -1;
            }

            if (memcmp(buffer, other, 2048) != 0) {
                close(source);
                printf("FM HDL: OPL DIFERENTE: fatia %d, setor de CD %u, LBA 0x%08x\n", i, lsn, lba);
                hdl_set_error("OPL: setor diferente", (int)lsn);
                return -1;
            }

            checked++;
        }
    }

    close(source);

    printf("FM HDL: OPL conferencia OK: %d setores de CD em %d fatia(s)\n", checked, nparts);

    return 0;
}


int hdl_install(
    const char *iso_path,
    const IsoGameInfo *game,
    const char *title,
    int verify)
{
    hdl_header_t *header;
    unsigned char *buffer;
    unsigned char *other;
    unsigned long long total;
    char disc_id[16];
    char part_name[40];
    char existing[64];
    char path[48];
    int result;


    last_error[0] = '\0';

    if (hdl_init() < 0)
        return -1;


    if (!hdl_disc_id(game->game_id, disc_id)) {
        hdl_set_error("ID do jogo fora do padrao SXXX_YYY.ZZ", 0);
        return -1;
    }

    if (game->sectors > HDL_DVD5_MAX_SECTORS) {
        hdl_set_error("DVD de duas camadas ainda nao suportado", (int)game->sectors);
        return -1;
    }

    header = memalign(64, sizeof(hdl_header_t));
    buffer = memalign(64, HDL_CHUNK);
    other = memalign(64, HDL_CHUNK);

    if (!header || !buffer || !other) {
        free(header);
        free(buffer);
        free(other);
        hdl_set_error("sem memoria", 0);
        return -1;
    }


    /*
     * Ja instalado: nao grava; confere o que esta no HDD como o OPL.
     */
    if (hdl_already_installed(disc_id, existing, sizeof(existing))) {

        printf("FM HDL: ja instalado: %s\n", existing);

        result = hdl_check_opl(iso_path, existing, game, buffer, other);

        snprintf(
            path,
            sizeof(path),
            "%s",
            result == 0 ? "conferido como o OPL: OK" : "conferencia OPL FALHOU"
        );

        if (result == 0)
            snprintf(last_error, sizeof(last_error), "ja instalado (%s)", path);
        else
            printf("FM HDL: %s\n", path);

        result = -1;
        goto end;
    }

    hdl_part_name(part_name, disc_id, title);

    printf(
        "FM HDL: instalar %s -> %s | %s | %u setores | titulo '%s'\n",
        iso_path,
        part_name,
        game->is_dvd ? "DVD" : "CD",
        game->sectors,
        title
    );


    result = hdl_create_partition(part_name, game, title, header);

    if (result < 0) {
        goto remove;
    }


    total = (unsigned long long)game->sectors * 2048ULL;

    result = hdl_stream(iso_path, part_name, total, HDL_PHASE_WRITE, buffer, other);

    if (result < 0)
        goto remove;

    /*
     * Conferencia completa (opcional): rele a origem e o HDD inteiros.
     */
    if (verify) {

        result = hdl_stream(iso_path, part_name, total, HDL_PHASE_VERIFY, buffer, other);

        if (result < 0)
            goto remove;

    } else {

        printf("FM HDL: conferencia completa desligada\n");
    }

    result = hdl_check_opl(iso_path, part_name, game, buffer, other);

    if (result < 0)
        goto remove;


    snprintf(last_error, sizeof(last_error), "%s instalado", part_name);

    printf("FM HDL: OK %s\n", part_name);

    goto end;


remove:
    /*
     * Nunca deixar um jogo incompleto (o OPL listaria).
     */
    snprintf(path, sizeof(path), "hdd0:%s", part_name);

    printf("FM HDL: removendo particao incompleta %s -> %d\n", path, fileXioRemove(path));

end:
    free(header);
    free(buffer);
    free(other);

    return result;
}


const char *hdl_error(void)
{
    return last_error;
}
