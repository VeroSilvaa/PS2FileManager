#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>

/*
 * Criar particao, subparticoes (ioctl2) e remover particao nao tem
 * equivalente POSIX.
 */
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>

#include "hdd.h"
#include "hdl.h"

extern unsigned char HDLWRITE_irx[];
extern unsigned int size_HDLWRITE_irx;

#define HDL_CHUNK (32 * 1024)

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
 * Autoteste
 * ============================================================ */

#define TEST_PART       "PP.TESTE-00000.HDL.TESTE"
#define TEST_BYTES      (130u * 1024 * 1024)

/*
 * Byte esperado em cada posicao do fluxo: muda com o setor e
 * com a posicao no setor (detecta setor trocado ou deslocado).
 */
static unsigned char test_byte(unsigned int offset)
{
    return (unsigned char)(((offset >> 9) * 31u + (offset & 511u)) ^ (offset >> 17));
}


static void test_fill(unsigned char *buffer, unsigned int offset, unsigned int size)
{
    unsigned int i;

    for (i = 0; i < size; i++)
        buffer[i] = test_byte(offset + i);
}


static int test_create(void)
{
    int fd;
    int result;

    /*
     * Sobra de um teste anterior interrompido.
     */
    fileXioRemove("hdd0:" TEST_PART);

    fd = fileXioOpen(
        "hdd0:" TEST_PART ",,,128M,HDL",
        FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC,
        0644
    );

    printf("FM HDL: criar " TEST_PART " (128M,HDL) -> %d\n", fd);

    if (fd < 0) {
        hdl_set_error("criar particao HDL", fd);
        return fd;
    }

    result = fileXioIoctl2(fd, HIOCADDSUB, "128M", 5, NULL, 0);

    printf("FM HDL: subparticao 128M -> %d\n", result);

    fileXioClose(fd);

    if (result < 0) {
        hdl_set_error("criar subparticao", result);
        fileXioRemove("hdd0:" TEST_PART);
        return result;
    }

    return 0;
}


int hdl_selftest(void)
{
    unsigned char *buffer;
    unsigned char *expected;
    unsigned int offset;
    unsigned int size;
    clock_t start;
    int fd;
    int result;
    int ok;


    if (hdl_init() < 0)
        return -1;


    buffer = memalign(64, HDL_CHUNK);
    expected = memalign(64, HDL_CHUNK);

    if (!buffer || !expected) {
        free(buffer);
        free(expected);
        hdl_set_error("sem memoria", 0);
        return -1;
    }


    result = test_create();

    if (result < 0)
        goto end;


    /*
     * Gravacao: primeiro pedaco de 1000 bytes (desalinhado),
     * depois blocos de 32 KB.
     */
    start = clock();

    fd = fileXioOpen("hdlw:" TEST_PART, FIO_O_WRONLY, 0);

    if (fd < 0) {
        hdl_set_error("abrir hdlw: para gravar", fd);
        result = fd;
        goto remove;
    }

    hdl_progress(HDL_PHASE_WRITE, 0, TEST_BYTES);

    for (offset = 0; offset < TEST_BYTES; offset += size) {

        if (offset > 0)
            hdl_progress(HDL_PHASE_WRITE, offset, TEST_BYTES);

        size = offset == 0 ? 1000 : HDL_CHUNK;

        if (size > TEST_BYTES - offset)
            size = TEST_BYTES - offset;

        test_fill(buffer, offset, size);

        result = fileXioWrite(fd, buffer, size);

        if (result != (int)size) {
            printf("FM HDL: write em %u -> %d\n", offset, result);
            hdl_set_error("gravar", result);
            fileXioClose(fd);
            result = -1;
            goto remove;
        }
    }

    result = fileXioClose(fd);

    hdl_progress(HDL_PHASE_WRITE, TEST_BYTES, TEST_BYTES);

    printf(
        "FM HDL: gravados %u bytes em %d ms (close %d)\n",
        TEST_BYTES,
        (int)((clock() - start) * 1000 / CLOCKS_PER_SEC),
        result
    );

    if (result < 0) {
        hdl_set_error("fechar apos gravar", result);
        goto remove;
    }


    /*
     * Conferencia.
     */
    start = clock();

    fd = fileXioOpen("hdlw:" TEST_PART, FIO_O_RDONLY, 0);

    if (fd < 0) {
        hdl_set_error("abrir hdlw: para ler", fd);
        result = fd;
        goto remove;
    }

    ok = 1;

    hdl_progress(HDL_PHASE_VERIFY, 0, TEST_BYTES);

    for (offset = 0; offset < TEST_BYTES; offset += size) {

        if (offset > 0)
            hdl_progress(HDL_PHASE_VERIFY, offset, TEST_BYTES);

        size = HDL_CHUNK;

        if (size > TEST_BYTES - offset)
            size = TEST_BYTES - offset;

        result = fileXioRead(fd, buffer, size);

        if (result != (int)size) {
            printf("FM HDL: read em %u -> %d\n", offset, result);
            hdl_set_error("ler", result);
            ok = 0;
            break;
        }

        test_fill(expected, offset, size);

        if (memcmp(buffer, expected, size) != 0) {

            unsigned int i;

            for (i = 0; i < size && buffer[i] == expected[i]; i++)
                ;

            printf(
                "FM HDL: DIFERENTE no byte %u (setor %u): lido %02x esperado %02x\n",
                offset + i,
                (offset + i) / 512,
                buffer[i],
                expected[i]
            );

            hdl_set_error("dados diferentes", (int)((offset + i) / 512));
            ok = 0;
            break;
        }
    }

    fileXioClose(fd);

    if (ok)
        hdl_progress(HDL_PHASE_VERIFY, TEST_BYTES, TEST_BYTES);

    printf(
        "FM HDL: conferencia %s em %d ms\n",
        ok ? "OK" : "FALHOU",
        (int)((clock() - start) * 1000 / CLOCKS_PER_SEC)
    );

    result = ok ? 0 : -1;

    if (ok)
        snprintf(last_error, sizeof(last_error), "130 MB gravados e conferidos");


remove:
    printf(
        "FM HDL: remover " TEST_PART " -> %d\n",
        fileXioRemove("hdd0:" TEST_PART)
    );

end:
    free(buffer);
    free(expected);

    return result;
}


const char *hdl_error(void)
{
    return last_error;
}
