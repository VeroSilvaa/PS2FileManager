/*
 * hdlwrite.irx - dados de jogo HDLoader numa particao APA.
 *
 * Dispositivo "hdlw:". Abrir "hdlw:<particao>" e fazer write()/read()
 * sequencial: os dados vao para as fatias da particao (principal +
 * subparticoes) pelo HIOCTRANSFER do ps2hdd, que so aceita buffer
 * na memoria do IOP (por isso este modulo existe).
 *
 * Disposicao das fatias (a mesma que o cabecalho HDL descreve e que
 * o OPL espera):
 *   - particao principal: dados depois dos 4 MB reservados (0x2000 setores)
 *   - subparticoes:       dados depois de 4 setores
 *   - cada fatia guarda um numero inteiro de setores de CD (2048 bytes)
 *
 * O cabecalho HDL e a criacao das particoes ficam no EE (hdl.c).
 * Um arquivo aberto por vez.
 *
 * PS2FileManager - licenca MIT.
 */

#include <errno.h>
#include <irx.h>
#include <loadcore.h>
#include <iomanX.h>
#include <io_common.h>
#include <hdd-ioctl.h>
#include <stdio.h>
#include <sysclib.h>
#include <sysmem.h>

#define MODNAME "hdlwrite"
IRX_ID(MODNAME, 1, 1);

#define HDLW_MAX_PARTS     65
#define HDLW_MAIN_RESERVED 0x2000
#define HDLW_SUB_RESERVED  4

/*
 * Buffer intermediario: o fileXio pode entregar pedacos que nao
 * sao multiplos de 512 (inicio/fim desalinhados do buffer do EE).
 */
#define HDLW_BUF_SECTORS   64
#define HDLW_BUF_SIZE      (HDLW_BUF_SECTORS * 512)

typedef struct {
    int in_use;
    int hdd_fd;
    int writing;

    int nparts;
    unsigned int capacity[HDLW_MAX_PARTS];  /* setores de 512 por fatia */

    int part;                               /* fatia atual */
    unsigned int rel;                       /* setor dentro da fatia */

    unsigned int pending;                   /* bytes no buffer */
    unsigned char *buf;
} hdlw_file_t;

static hdlw_file_t hdlw;

IOMANX_RETURN_VALUE_IMPL(0);
IOMANX_RETURN_VALUE_IMPL(EIO);


static unsigned int hdlw_reserved(int part)
{
    return part == 0 ? HDLW_MAIN_RESERVED : HDLW_SUB_RESERVED;
}


/*
 * Transfere setores na posicao atual, avancando entre as fatias.
 */
static int hdlw_transfer(unsigned char *buffer, unsigned int sectors, int mode)
{
    hddIoctl2Transfer_t cmd;
    unsigned int count;
    int result;

    while (sectors > 0) {

        if (hdlw.rel >= hdlw.capacity[hdlw.part]) {
            hdlw.part++;
            hdlw.rel = 0;
        }

        if (hdlw.part >= hdlw.nparts)
            return -ENOSPC;

        count = hdlw.capacity[hdlw.part] - hdlw.rel;

        if (count > sectors)
            count = sectors;

        cmd.sub = hdlw.part;
        cmd.sector = hdlw_reserved(hdlw.part) + hdlw.rel;
        cmd.size = count;
        cmd.mode = mode;
        cmd.buffer = buffer;

        result = iomanX_ioctl2(hdlw.hdd_fd, HIOCTRANSFER, &cmd, 0, NULL, 0);

        if (result < 0) {
            printf(MODNAME ": transfer fatia %d setor 0x%x (%u) -> %d\n",
                   hdlw.part, cmd.sector, count, result);
            return result;
        }

        hdlw.rel += count;
        buffer += count * 512;
        sectors -= count;
    }

    return 0;
}


static int hdlw_flush(void)
{
    unsigned int sectors;
    int result;

    if (hdlw.pending == 0)
        return 0;

    /*
     * Final parcial: completa o setor com zeros.
     */
    if (hdlw.pending % 512) {
        memset(hdlw.buf + hdlw.pending, 0, 512 - (hdlw.pending % 512));
        hdlw.pending += 512 - (hdlw.pending % 512);
    }

    sectors = hdlw.pending / 512;
    hdlw.pending = 0;

    result = hdlw_transfer(hdlw.buf, sectors, APA_IO_MODE_WRITE);

    return result;
}


static int hdlw_open(iomanX_iop_file_t *f, const char *name, int flags, int mode)
{
    char path[64];
    int result;
    int i;

    (void)mode;

    if (hdlw.in_use)
        return -EBUSY;

    while (*name == '/')
        name++;

    if (strlen(name) + 6 > sizeof(path))
        return -ENAMETOOLONG;

    strcpy(path, "hdd0:");
    strcat(path, name);

    hdlw.hdd_fd = iomanX_open(path, FIO_O_RDWR);

    if (hdlw.hdd_fd < 0) {
        printf(MODNAME ": open %s -> %d\n", path, hdlw.hdd_fd);
        return hdlw.hdd_fd;
    }

    hdlw.nparts = iomanX_ioctl2(hdlw.hdd_fd, HIOCNSUB, NULL, 0, NULL, 0) + 1;

    if (hdlw.nparts < 1 || hdlw.nparts > HDLW_MAX_PARTS) {
        iomanX_close(hdlw.hdd_fd);
        return -EINVAL;
    }

    for (i = 0; i < hdlw.nparts; i++) {

        result = iomanX_ioctl2(hdlw.hdd_fd, HIOCGETSIZE, &i, sizeof(i), NULL, 0);

        if (result <= (int)hdlw_reserved(i)) {
            iomanX_close(hdlw.hdd_fd);
            return result < 0 ? result : -EINVAL;
        }

        /*
         * Inteiro de setores de CD (4 x 512).
         */
        hdlw.capacity[i] = ((result - hdlw_reserved(i)) / 4) * 4;

        printf(MODNAME ": fatia %d: %d setores, %u para dados\n",
               i, result, hdlw.capacity[i]);
    }

    hdlw.writing = (flags & FIO_O_WRONLY) ? 1 : 0;
    hdlw.part = 0;
    hdlw.rel = 0;
    hdlw.pending = 0;
    hdlw.in_use = 1;

    f->privdata = &hdlw;

    return 0;
}


static int hdlw_close(iomanX_iop_file_t *f)
{
    int result = 0;

    (void)f;

    if (!hdlw.in_use)
        return -EBADF;

    if (hdlw.writing) {
        result = hdlw_flush();
        iomanX_ioctl2(hdlw.hdd_fd, HIOCFLUSH, NULL, 0, NULL, 0);
    }

    iomanX_close(hdlw.hdd_fd);

    hdlw.in_use = 0;

    return result;
}


static int hdlw_write(iomanX_iop_file_t *f, void *ptr, int size)
{
    unsigned char *data = ptr;
    unsigned int count;
    int done = 0;
    int result;

    (void)f;

    if (!hdlw.in_use || !hdlw.writing)
        return -EBADF;

    while (done < size) {

        count = HDLW_BUF_SIZE - hdlw.pending;

        if (count > (unsigned int)(size - done))
            count = size - done;

        memcpy(hdlw.buf + hdlw.pending, data + done, count);

        hdlw.pending += count;
        done += count;

        if (hdlw.pending == HDLW_BUF_SIZE) {

            result = hdlw_flush();

            if (result < 0)
                return result;
        }
    }

    return size;
}


/*
 * Leitura para conferencia: tamanho multiplo de 512.
 */
static int hdlw_read(iomanX_iop_file_t *f, void *ptr, int size)
{
    unsigned char *data = ptr;
    unsigned int sectors;
    int done = 0;
    int result;

    (void)f;

    if (!hdlw.in_use || hdlw.writing)
        return -EBADF;

    if (size % 512)
        return -EINVAL;

    while (done < size) {

        sectors = (size - done) / 512;

        if (sectors > HDLW_BUF_SECTORS)
            sectors = HDLW_BUF_SECTORS;

        result = hdlw_transfer(hdlw.buf, sectors, APA_IO_MODE_READ);

        if (result < 0)
            return result;

        memcpy(data + done, hdlw.buf, sectors * 512);

        done += sectors * 512;
    }

    return size;
}


static iomanX_iop_device_ops_t hdlw_ops = {
    IOMANX_RETURN_VALUE(0),     /* init */
    IOMANX_RETURN_VALUE(0),     /* deinit */
    IOMANX_RETURN_VALUE(EIO),   /* format */
    &hdlw_open,
    &hdlw_close,
    &hdlw_read,
    &hdlw_write,
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
    IOMANX_RETURN_VALUE(EIO),   /* devctl */
    IOMANX_RETURN_VALUE(EIO),   /* symlink */
    IOMANX_RETURN_VALUE(EIO),   /* readlink */
    IOMANX_RETURN_VALUE(EIO),   /* ioctl2 */
};

static iomanX_iop_device_t hdlw_device = {
    "hdlw",
    IOP_DT_FS | IOP_DT_FSEXT,
    1,
    "HDLoader data writer",
    &hdlw_ops,
};


int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    memset(&hdlw, 0, sizeof(hdlw));

    hdlw.buf = AllocSysMemory(ALLOC_FIRST, HDLW_BUF_SIZE, NULL);

    if (hdlw.buf == NULL) {
        printf(MODNAME ": sem memoria\n");
        return MODULE_NO_RESIDENT_END;
    }

    if (iomanX_AddDrv(&hdlw_device) != 0) {
        printf(MODNAME ": AddDrv falhou\n");
        return MODULE_NO_RESIDENT_END;
    }

    printf(MODNAME ": pronto (hdlw:)\n");

    return MODULE_RESIDENT_END;
}
