#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <libmc.h>

#include <stdio.h>
#include <string.h>

#include "memorycard.h"
#include "filemanager.h"

/*
 * Flags esperados por MCMAN/MCSERV.
 *
 * Nao usar O_RDONLY/O_WRONLY do newlib aqui:
 * os valores sao diferentes dos flags usados
 * pela API mcOpen().
 */
#define MC_O_RDONLY  0x0001
#define MC_O_WRONLY  0x0002
#define MC_O_RDWR    0x0003
#define MC_O_APPEND  0x0100
#define MC_O_CREAT   0x0200
#define MC_O_TRUNC   0x0400

#define MC_COPY_BUFFER_SIZE 1024


static sceMcTblGetDir mc_entries[FM_MAX_FILES]
    __attribute__((aligned(64)));

static sceMcTblGetDir check_entry[1]
    __attribute__((aligned(64)));

static unsigned char copy_buffer[MC_COPY_BUFFER_SIZE]
    __attribute__((aligned(64)));

static unsigned char verify_buffer[MC_COPY_BUFFER_SIZE]
    __attribute__((aligned(64)));


/* ================================================== */
/* STATUS                                             */
/* ================================================== */

static void copy_status_init(
    MemoryCardCopyStatus *status
)
{
    if (status == NULL)
        return;

    memset(
        status,
        0,
        sizeof(MemoryCardCopyStatus)
    );

    strcpy(
        status->stage,
        "NONE"
    );
}


static void copy_status_error(
    MemoryCardCopyStatus *status,
    int error,
    int mc_result,
    const char *stage
)
{
    if (status == NULL)
        return;

    status->error = error;
    status->mc_result = mc_result;

    strncpy(
        status->stage,
        stage,
        sizeof(status->stage) - 1
    );

    status->stage[
        sizeof(status->stage) - 1
    ] = '\0';
}


/* ================================================== */
/* RESULT STRING                                      */
/* ================================================== */

const char *memorycard_result_string(
    int result
)
{
    switch (result) {

        case sceMcResSucceed:
            return "SUCCESS";

        case sceMcResChangedCard:
            return "CHANGED CARD";

        case sceMcResNoFormat:
            return "NO FORMAT";

        case sceMcResFullDevice:
            return "FULL DEVICE";

        case sceMcResNoEntry:
            return "NO ENTRY";

        case sceMcResDeniedPermit:
            return "DENIED";

        case sceMcResNotEmpty:
            return "NOT EMPTY";

        case sceMcResUpLimitHandle:
            return "HANDLE LIMIT";

        case sceMcResFailReplace:
            return "REPLACE FAILED";

        case sceMcResFailResetAuth:
            return "RESET AUTH FAILED";

        case sceMcResFailDetect:
            return "DETECT FAILED";

        case sceMcResFailDetect2:
            return "DETECT2 FAILED";

        case sceMcResDeniedPS1Permit:
            return "PS1 DENIED";

        case sceMcResFailAuth:
            return "AUTH FAILED";

        default:
            return "UNKNOWN";
    }
}


/* ================================================== */
/* SYNC                                               */
/* ================================================== */

static int memorycard_sync(
    int *command_result
)
{
    int cmd = MC_FUNC_NONE;
    int result = 0;
    int sync_result;

    sync_result = mcSync(
        MC_WAIT,
        &cmd,
        &result
    );

    /*
     * mcSync:
     *
     * 1  = terminou
     * -1 = nenhuma função registrada
     *
     * Com MC_WAIT não esperamos receber 0.
     */
    if (sync_result != 1)
        return -1;

    if (command_result != NULL)
        *command_result = result;

    return 0;
}


/* ================================================== */
/* INIT                                               */
/* ================================================== */

int memorycard_init(void)
{
    int ret;

    /*
     * MCSERV do PS2SDK: tenta primeiro o protocolo XMC
     * (que confere a versao do servidor) e depois o MC.
     */
    ret = mcInit(MC_TYPE_XMC);

    printf("mcInit(MC_TYPE_XMC): %d\n", ret);

    if (ret >= 0)
        return 0;

    ret = mcInit(MC_TYPE_MC);

    printf("mcInit(MC_TYPE_MC): %d\n", ret);

    if (ret < 0)
        return -1;

    return 0;
}

/* ================================================== */
/* DISPONIBILIDADE                                    */
/* ================================================== */

int memorycard_available(
    int port
)
{
    int type = MC_TYPE_NONE;
    int free_space = 0;
    int format = MC_UNFORMATTED;

    int result;

    if (
        mcGetInfo(
            port,
            0,
            &type,
            &free_space,
            &format
        ) < 0
    ) {
        return 0;
    }

    if (
        memorycard_sync(
            &result
        ) < 0
    ) {
        return 0;
    }

    /*
     * -1 significa cartão formatado
     * recém-detectado.
     */
    if (result < -2)
        return 0;

    if (type == MC_TYPE_NONE)
        return 0;

    if (type != MC_TYPE_PS2)
        return 0;

    if (result == sceMcResChangedCard)
        return 1;

    if (result == sceMcResNoFormat)
        return 0;

    if (format != MC_FORMATTED)
        return 0;

    return 1;
}


/* ================================================== */
/* DEBUG                                              */
/* ================================================== */

int memorycard_debug_info(
    int port,
    MemoryCardDebug *info
)
{
    if (info == NULL)
        return -1;

    memset(
        info,
        0,
        sizeof(MemoryCardDebug)
    );

    info->getinfo_return =
        mcGetInfo(
            port,
            0,
            &info->type,
            &info->free_space,
            &info->format
        );

    if (
        info->getinfo_return < 0
    ) {
        return -1;
    }

    info->sync_return =
        mcSync(
            MC_WAIT,
            &info->cmd,
            &info->result
        );

    return 0;
}


/* ================================================== */
/* PATH                                               */
/* ================================================== */

static int get_port_from_path(
    const char *path
)
{
    if (path == NULL)
        return -1;

    if (
        strncmp(
            path,
            "mc0:",
            4
        ) == 0
    ) {
        return 0;
    }

    if (
        strncmp(
            path,
            "mc1:",
            4
        ) == 0
    ) {
        return 1;
    }

    return -1;
}


static const char *get_mc_relative_path(
    const char *path
)
{
    if (path == NULL)
        return NULL;

    if (
        strncmp(path, "mc0:", 4) != 0 &&
        strncmp(path, "mc1:", 4) != 0
    ) {
        return NULL;
    }

    return path + 4;
}


/* ================================================== */
/* OPEN                                               */
/* ================================================== */

static int mc_open_sync(
    int port,
    const char *path,
    int mode,
    int *mc_result
)
{
    int result;

    if (
        mcOpen(
            port,
            0,
            path,
            mode
        ) < 0
    ) {
        if (mc_result != NULL)
            *mc_result = -999;

        return -1;
    }

    if (
        memorycard_sync(
            &result
        ) < 0
    ) {
        if (mc_result != NULL)
            *mc_result = -998;

        return -1;
    }

    if (mc_result != NULL)
        *mc_result = result;

    return result;
}


/* ================================================== */
/* CLOSE                                              */
/* ================================================== */

static int mc_close_sync(
    int fd,
    int *mc_result
)
{
    int result;

    if (mcClose(fd) < 0) {

        if (mc_result != NULL)
            *mc_result = -999;

        return -1;
    }

    if (
        memorycard_sync(
            &result
        ) < 0
    ) {

        if (mc_result != NULL)
            *mc_result = -998;

        return -1;
    }

    if (mc_result != NULL)
        *mc_result = result;

    return result;
}


/* ================================================== */
/* READ                                               */
/* ================================================== */

static int mc_read_sync(
    int fd,
    void *buffer,
    int size,
    int *mc_result
)
{
    int result;

    if (
        mcRead(
            fd,
            buffer,
            size
        ) < 0
    ) {

        if (mc_result != NULL)
            *mc_result = -999;

        return -1;
    }

    if (
        memorycard_sync(
            &result
        ) < 0
    ) {

        if (mc_result != NULL)
            *mc_result = -998;

        return -1;
    }

    if (mc_result != NULL)
        *mc_result = result;

    return result;
}


/* ================================================== */
/* WRITE                                              */
/* ================================================== */

static int mc_write_sync(
    int fd,
    const void *buffer,
    int size,
    int *mc_result
)
{
    int result;

    if (
        mcWrite(
            fd,
            buffer,
            size
        ) < 0
    ) {

        if (mc_result != NULL)
            *mc_result = -999;

        return -1;
    }

    if (
        memorycard_sync(
            &result
        ) < 0
    ) {

        if (mc_result != NULL)
            *mc_result = -998;

        return -1;
    }

    if (mc_result != NULL)
        *mc_result = result;

    return result;
}


/* ================================================== */
/* FLUSH                                              */
/* ================================================== */

static int mc_flush_sync(
    int fd,
    int *mc_result
)
{
    int result;

    if (
        mcFlush(
            fd
        ) < 0
    ) {

        if (mc_result != NULL)
            *mc_result = -999;

        return -1;
    }

    if (
        memorycard_sync(
            &result
        ) < 0
    ) {

        if (mc_result != NULL)
            *mc_result = -998;

        return -1;
    }

    if (mc_result != NULL)
        *mc_result = result;

    return result;
}


/* ================================================== */
/* EXISTE?                                            */
/* ================================================== */

static int memorycard_path_exists(
    int port,
    const char *path
)
{
    int result;

    memset(
        check_entry,
        0,
        sizeof(check_entry)
    );

    if (
        mcGetDir(
            port,
            0,
            path,
            0,
            1,
            check_entry
        ) < 0
    ) {
        return -1;
    }

    if (
        memorycard_sync(
            &result
        ) < 0
    ) {
        return -1;
    }

    if (result > 0)
        return 1;

    if (
        result == 0 ||
        result == sceMcResNoEntry
    ) {
        return 0;
    }

    return -1;
}


/* ================================================== */
/* COPY + VERIFY                                      */
/* ================================================== */

int memorycard_copy_file(
    const char *source,
    const char *destination,
    MemoryCardCopyStatus *status
)
{
    int source_port;
    int destination_port;

    const char *source_path;
    const char *destination_path;

    int source_fd = -1;
    int destination_fd = -1;
    int verify_fd = -1;

    int source_result = 0;
    int destination_result = 0;

    int bytes_read;
    int bytes_written;
    int verify_read;

    int offset;
    int exists;

    copy_status_init(
        status
    );


    /* -------------------------------------------------- */
    /* PATH                                               */
    /* -------------------------------------------------- */

    if (
        source == NULL ||
        destination == NULL
    ) {

        copy_status_error(
            status,
            MC_COPY_ERROR_PATH,
            0,
            "PATH"
        );

        return MC_COPY_ERROR_PATH;
    }


    if (
        strcmp(
            source,
            destination
        ) == 0
    ) {

        copy_status_error(
            status,
            MC_COPY_ERROR_PATH,
            0,
            "SAME PATH"
        );

        return MC_COPY_ERROR_PATH;
    }


    source_port =
        get_port_from_path(
            source
        );

    destination_port =
        get_port_from_path(
            destination
        );

    source_path =
        get_mc_relative_path(
            source
        );

    destination_path =
        get_mc_relative_path(
            destination
        );


    if (
        source_port < 0 ||
        destination_port < 0 ||
        source_path == NULL ||
        destination_path == NULL
    ) {

        copy_status_error(
            status,
            MC_COPY_ERROR_PATH,
            0,
            "PATH"
        );

        return MC_COPY_ERROR_PATH;
    }


    /* -------------------------------------------------- */
    /* DESTINO JA EXISTE?                                 */
    /* -------------------------------------------------- */

    exists =
        memorycard_path_exists(
            destination_port,
            destination_path
        );


    if (exists != 0) {

        copy_status_error(
            status,
            MC_COPY_ERROR_EXISTS,
            exists,
            exists > 0
                ? "DEST EXISTS"
                : "CHECK DEST"
        );

        return MC_COPY_ERROR_EXISTS;
    }


    /* -------------------------------------------------- */
    /* OPEN SOURCE                                        */
    /* -------------------------------------------------- */

   source_fd =
    mc_open_sync(
        source_port,
        source_path,
        MC_O_RDONLY,
        &source_result
    );


    if (source_fd < 0) {

        copy_status_error(
            status,
            MC_COPY_ERROR_OPEN_SOURCE,
            source_result,
            "OPEN SOURCE"
        );

        return MC_COPY_ERROR_OPEN_SOURCE;
    }


    /* -------------------------------------------------- */
    /* OPEN DESTINATION                                   */
    /* -------------------------------------------------- */

   destination_fd =
    mc_open_sync(
        destination_port,
        destination_path,
        MC_O_WRONLY | MC_O_CREAT,
        &destination_result
    );


    if (destination_fd < 0) {

        mc_close_sync(
            source_fd,
            NULL
        );

        copy_status_error(
            status,
            MC_COPY_ERROR_OPEN_DEST,
            destination_result,
            "OPEN DEST"
        );


        return MC_COPY_ERROR_OPEN_DEST;
    }


    /* -------------------------------------------------- */
    /* COPY                                               */
    /* -------------------------------------------------- */

    while (1) {

        bytes_read =
            mc_read_sync(
                source_fd,
                copy_buffer,
                sizeof(copy_buffer),
                &source_result
            );


        if (bytes_read < 0) {

            mc_close_sync(
                source_fd,
                NULL
            );

            mc_close_sync(
                destination_fd,
                NULL
            );

            copy_status_error(
                status,
                MC_COPY_ERROR_READ,
                source_result,
                "READ"
            );

            return MC_COPY_ERROR_READ;
        }


        if (bytes_read == 0)
            break;


        offset = 0;


        while (
            offset <
            bytes_read
        ) {

            bytes_written =
                mc_write_sync(
                    destination_fd,
                    copy_buffer + offset,
                    bytes_read - offset,
                    &destination_result
                );


            if (bytes_written <= 0) {

                mc_close_sync(
                    source_fd,
                    NULL
                );

                mc_close_sync(
                    destination_fd,
                    NULL
                );

                copy_status_error(
                    status,
                    MC_COPY_ERROR_WRITE,
                    destination_result,
                    "WRITE"
                );

                return MC_COPY_ERROR_WRITE;
            }


            offset +=
                bytes_written;


            if (status != NULL)
                status->bytes_copied +=
                    bytes_written;
        }
    }


    /* -------------------------------------------------- */
    /* FLUSH                                              */
    /* -------------------------------------------------- */

    if (
        mc_flush_sync(
            destination_fd,
            &destination_result
        ) < 0
    ) {

        mc_close_sync(
            source_fd,
            NULL
        );

        mc_close_sync(
            destination_fd,
            NULL
        );

        copy_status_error(
            status,
            MC_COPY_ERROR_FLUSH,
            destination_result,
            "FLUSH"
        );

        return MC_COPY_ERROR_FLUSH;
    }


    /* -------------------------------------------------- */
    /* CLOSE                                             */
    /* -------------------------------------------------- */

    if (
        mc_close_sync(
            source_fd,
            &source_result
        ) < 0
    ) {

        mc_close_sync(
            destination_fd,
            NULL
        );

        copy_status_error(
            status,
            MC_COPY_ERROR_CLOSE,
            source_result,
            "CLOSE SOURCE"
        );

        return MC_COPY_ERROR_CLOSE;
    }


    source_fd = -1;


    if (
        mc_close_sync(
            destination_fd,
            &destination_result
        ) < 0
    ) {

        copy_status_error(
            status,
            MC_COPY_ERROR_CLOSE,
            destination_result,
            "CLOSE DEST"
        );

        return MC_COPY_ERROR_CLOSE;
    }


    destination_fd = -1;


    /* ================================================== */
    /* VERIFICACAO                                       */
    /* ================================================== */

    source_fd =
        mc_open_sync(
            source_port,
            source_path,
            MC_O_RDONLY,
            &source_result
        );


    if (source_fd < 0) {

        copy_status_error(
            status,
            MC_COPY_ERROR_VERIFY_OPEN,
            source_result,
            "VERIFY SRC OPEN"
        );

        return MC_COPY_ERROR_VERIFY_OPEN;
    }


    verify_fd =
        mc_open_sync(
            destination_port,
            destination_path,
            MC_O_RDONLY,
            &destination_result
        );


    if (verify_fd < 0) {

        mc_close_sync(
            source_fd,
            NULL
        );

        copy_status_error(
            status,
            MC_COPY_ERROR_VERIFY_OPEN,
            destination_result,
            "VERIFY DST OPEN"
        );

        return MC_COPY_ERROR_VERIFY_OPEN;
    }


    while (1) {

        bytes_read =
            mc_read_sync(
                source_fd,
                copy_buffer,
                sizeof(copy_buffer),
                &source_result
            );


        if (bytes_read < 0) {

            mc_close_sync(
                source_fd,
                NULL
            );

            mc_close_sync(
                verify_fd,
                NULL
            );

            copy_status_error(
                status,
                MC_COPY_ERROR_VERIFY_READ,
                source_result,
                "VERIFY SRC READ"
            );

            return MC_COPY_ERROR_VERIFY_READ;
        }


        verify_read =
            mc_read_sync(
                verify_fd,
                verify_buffer,
                sizeof(verify_buffer),
                &destination_result
            );


        if (verify_read < 0) {

            mc_close_sync(
                source_fd,
                NULL
            );

            mc_close_sync(
                verify_fd,
                NULL
            );

            copy_status_error(
                status,
                MC_COPY_ERROR_VERIFY_READ,
                destination_result,
                "VERIFY DST READ"
            );

            return MC_COPY_ERROR_VERIFY_READ;
        }


        if (
            bytes_read !=
            verify_read
        ) {

            mc_close_sync(
                source_fd,
                NULL
            );

            mc_close_sync(
                verify_fd,
                NULL
            );

            copy_status_error(
                status,
                MC_COPY_ERROR_VERIFY_SIZE,
                0,
                "VERIFY SIZE"
            );

            return MC_COPY_ERROR_VERIFY_SIZE;
        }


        if (bytes_read == 0)
            break;


        if (
            memcmp(
                copy_buffer,
                verify_buffer,
                bytes_read
            ) != 0
        ) {

            mc_close_sync(
                source_fd,
                NULL
            );

            mc_close_sync(
                verify_fd,
                NULL
            );

            copy_status_error(
                status,
                MC_COPY_ERROR_VERIFY_DATA,
                0,
                "VERIFY DATA"
            );

            return MC_COPY_ERROR_VERIFY_DATA;
        }


        if (status != NULL)
            status->bytes_verified +=
                bytes_read;
    }


    mc_close_sync(
        source_fd,
        NULL
    );

    mc_close_sync(
        verify_fd,
        NULL
    );


    if (status != NULL) {

        status->error =
            MC_COPY_OK;

        status->mc_result =
            sceMcResSucceed;

        strcpy(
            status->stage,
            "VERIFIED"
        );
    }


    return MC_COPY_OK;
}


/* ================================================== */
/* SEARCH PATTERN                                     */
/* ================================================== */

static int build_search_pattern(
    const char *path,
    char *pattern,
    int pattern_size
)
{
    const char *subpath;
    size_t len;


    if (
        path == NULL ||
        pattern == NULL ||
        pattern_size <= 0
    ) {
        return -1;
    }


    subpath =
        path + 4;


    if (
        *subpath == '\0' ||
        strcmp(
            subpath,
            "/"
        ) == 0
    ) {

        if (pattern_size < 3)
            return -1;

        pattern[0] = '/';
        pattern[1] = '*';
        pattern[2] = '\0';

        return 0;
    }


    len =
        strlen(
            subpath
        );


    if (
        len + 3 >
        (size_t)pattern_size
    ) {
        return -1;
    }


    memcpy(
        pattern,
        subpath,
        len
    );


    if (
        len > 0 &&
        subpath[len - 1] == '/'
    ) {

        pattern[len] = '*';
        pattern[len + 1] = '\0';

        return 0;
    }


    pattern[len] = '/';
    pattern[len + 1] = '*';
    pattern[len + 2] = '\0';


    return 0;
}


/* ================================================== */
/* LIST                                               */
/* ================================================== */

int memorycard_list(
    const char *path,
    void *output,
    int max_entries
)
{
    FMEntry *entries;

    char pattern[
        FM_MAX_PATH
    ];

    int port;
    int result;
    int count;
    int i;

    size_t path_len;


    if (
        path == NULL ||
        output == NULL ||
        max_entries <= 0
    ) {
        return -1;
    }


    entries =
        (FMEntry *)output;


    port =
        get_port_from_path(
            path
        );


    if (port < 0)
        return -1;


    if (
        build_search_pattern(
            path,
            pattern,
            sizeof(pattern)
        ) < 0
    ) {
        return -1;
    }


    memset(
        mc_entries,
        0,
        sizeof(mc_entries)
    );


    if (
        mcGetDir(
            port,
            0,
            pattern,
            0,
            FM_MAX_FILES,
            mc_entries
        ) < 0
    ) {
        return -1;
    }


    if (
        memorycard_sync(
            &result
        ) < 0
    ) {
        return -1;
    }


    if (result < 0)
        return result;


    count = result;


    if (count > max_entries)
        count = max_entries;

    if (count > FM_MAX_FILES)
        count = FM_MAX_FILES;


    path_len =
        strlen(
            path
        );


    for (
        i = 0;
        i < count;
        i++
    ) {

        size_t name_len;


        memset(
            &entries[i],
            0,
            sizeof(FMEntry)
        );


        name_len = 0;


        while (
            name_len <
            sizeof(
                mc_entries[i].EntryName
            ) &&
            mc_entries[i]
                .EntryName[name_len] != '\0'
        ) {
            name_len++;
        }


        if (
            name_len >=
            FM_MAX_NAME
        ) {
            name_len =
                FM_MAX_NAME - 1;
        }


        memcpy(
            entries[i].name,
            mc_entries[i].EntryName,
            name_len
        );


        entries[i].name[
            name_len
        ] = '\0';


        entries[i].size =
            mc_entries[i].FileSizeByte;


        entries[i].is_dir =
            (
                mc_entries[i].AttrFile &
                MC_ATTR_SUBDIR
            ) != 0;


        if (
            path_len > 0 &&
            path[path_len - 1] == '/'
        ) {

            if (
                path_len +
                name_len +
                1 >
                FM_MAX_PATH
            ) {
                continue;
            }


            memcpy(
                entries[i].path,
                path,
                path_len
            );


            memcpy(
                entries[i].path +
                    path_len,
                entries[i].name,
                name_len
            );


            entries[i].path[
                path_len +
                name_len
            ] = '\0';

        } else {

            if (
                path_len +
                1 +
                name_len +
                1 >
                FM_MAX_PATH
            ) {
                continue;
            }


            memcpy(
                entries[i].path,
                path,
                path_len
            );


            entries[i].path[
                path_len
            ] = '/';


            memcpy(
                entries[i].path +
                    path_len +
                    1,
                entries[i].name,
                name_len
            );


            entries[i].path[
                path_len +
                1 +
                name_len
            ] = '\0';
        }


        entries[i].selected = 0;
    }


    return count;
}