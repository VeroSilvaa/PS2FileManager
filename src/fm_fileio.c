#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "fm_fileio.h"


#define FMIO_BUFFER_SIZE    4096
#define FMIO_MAX_PATH       1024
#define FMIO_MAX_RECURSION  16
#define FMIO_NAME_MAX       256


static unsigned char copy_buffer[
    FMIO_BUFFER_SIZE
] __attribute__((aligned(64)));

static unsigned char verify_source_buffer[
    FMIO_BUFFER_SIZE
] __attribute__((aligned(64)));

static unsigned char verify_destination_buffer[
    FMIO_BUFFER_SIZE
] __attribute__((aligned(64)));


/* ============================================================
 * STATUS
 * ============================================================ */

static void fmio_status_init(
    FileCopyStatus *status)
{
    if (!status)
        return;

    memset(
        status,
        0,
        sizeof(FileCopyStatus)
    );

    status->error = FILEIO_COPY_OK;
    status->system_result = 0;

    strcpy(
        status->stage,
        "INIT"
    );
}


static void fmio_set_error(
    FileCopyStatus *status,
    int error,
    int system_result,
    const char *stage)
{
    if (!status)
        return;

    status->error = error;
    status->system_result = system_result;

    strncpy(
        status->stage,
        stage,
        sizeof(status->stage) - 1
    );

    status->stage[
        sizeof(status->stage) - 1
    ] = '\0';
}


/* ============================================================
 * PATH
 * ============================================================ */

static int fmio_join_path(
    const char *directory,
    const char *name,
    char *output,
    int output_size)
{
    int length;
    int result;

    if (!directory ||
        !name ||
        !output ||
        output_size <= 0)
        return 0;

    length = strlen(directory);

    if (length <= 0)
        return 0;

    if (directory[length - 1] == '/') {

        result = snprintf(
            output,
            output_size,
            "%s%s",
            directory,
            name
        );

    } else {

        result = snprintf(
            output,
            output_size,
            "%s/%s",
            directory,
            name
        );
    }

    if (result < 0 ||
        result >= output_size)
        return 0;

    output[output_size - 1] = '\0';

    return 1;
}


/* ============================================================
 * STAT
 * ============================================================ */

static int fmio_get_type(
    const char *path,
    int *is_directory)
{
    struct stat st;
    DIR *dir;
    int fd;
    int exists;
    int opened_as_file;

    if (!path ||
        !is_directory)
        return 0;

    /*
     * Comportamento medido no Memory Card (log do PCSX2, Etapa 2):
     *
     * - stat() retorna 0 MESMO para caminhos inexistentes,
     *   com st_mode lixo. So o st_mode de itens existentes
     *   e confiavel.
     * - open() e opendir() falham para inexistentes, mas
     *   abrem tanto arquivo quanto pasta.
     *
     * Portanto: existencia por open()/opendir() (como o
     * wLaunchELF) e tipo pelo stat() somente depois disso.
     */
    exists = 0;
    opened_as_file = 0;

    errno = 0;

    fd = open(
        path,
        O_RDONLY,
        0
    );

    if (fd >= 0) {
        close(fd);
        exists = 1;
        opened_as_file = 1;
    } else {

        errno = 0;

        dir = opendir(path);

        if (dir) {
            closedir(dir);
            exists = 1;
        }
    }

    if (!exists)
        return 0;

    memset(&st, 0, sizeof(st));

    if (stat(path, &st) == 0 &&
        (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {

        *is_directory =
            S_ISDIR(st.st_mode) ? 1 : 0;

        return 1;
    }

    /*
     * Sem stat() confiavel: se so abriu como pasta, e pasta.
     */
    *is_directory =
        opened_as_file ? 0 : 1;

    return 1;
}

static int fmio_path_exists(
    const char *path)
{
    int is_directory;

    return fmio_get_type(
        path,
        &is_directory
    );
}

/* ============================================================
 * WRAPPERS
 *
 * Mesmo princípio do genOpen/genRead/genWrite/genClose
 * do wLaunchELF.
 * ============================================================ */

static int fmio_open(
    const char *path,
    int flags,
    int mode)
{
    return open(
        path,
        flags,
        mode
    );
}


static int fmio_close(
    int fd)
{
    return close(fd);
}


static int fmio_read(
    int fd,
    void *buffer,
    int size)
{
    return read(
        fd,
        buffer,
        size
    );
}


static int fmio_write(
    int fd,
    const void *buffer,
    int size)
{
    return write(
        fd,
        buffer,
        size
    );
}


/* ============================================================
 * VERIFICAÇÃO
 * ============================================================ */

static int fmio_verify_file(
    const char *source,
    const char *destination,
    FileCopyStatus *status)
{
    int source_fd;
    int destination_fd;

    int source_read;
    int destination_read;


    source_fd = -1;
    destination_fd = -1;


    errno = 0;

    source_fd = fmio_open(
        source,
        O_RDONLY,
        0
    );

    if (source_fd < 0) {

        fmio_set_error(
            status,
            FILEIO_ERROR_VERIFY_OPEN,
            errno,
            "VERIFY SRC OPEN"
        );

        return FILEIO_ERROR_VERIFY_OPEN;
    }


    errno = 0;

    destination_fd = fmio_open(
        destination,
        O_RDONLY,
        0
    );

    if (destination_fd < 0) {

        int saved_errno = errno;

        fmio_close(source_fd);

        fmio_set_error(
            status,
            FILEIO_ERROR_VERIFY_OPEN,
            saved_errno,
            "VERIFY DST OPEN"
        );

        return FILEIO_ERROR_VERIFY_OPEN;
    }


    while (1) {

        errno = 0;

        source_read = fmio_read(
            source_fd,
            verify_source_buffer,
            FMIO_BUFFER_SIZE
        );

        if (source_read < 0) {

            int saved_errno = errno;

            fmio_close(source_fd);
            fmio_close(destination_fd);

            fmio_set_error(
                status,
                FILEIO_ERROR_VERIFY_READ,
                saved_errno,
                "VERIFY SRC READ"
            );

            return FILEIO_ERROR_VERIFY_READ;
        }


        errno = 0;

        destination_read = fmio_read(
            destination_fd,
            verify_destination_buffer,
            FMIO_BUFFER_SIZE
        );

        if (destination_read < 0) {

            int saved_errno = errno;

            fmio_close(source_fd);
            fmio_close(destination_fd);

            fmio_set_error(
                status,
                FILEIO_ERROR_VERIFY_READ,
                saved_errno,
                "VERIFY DST READ"
            );

            return FILEIO_ERROR_VERIFY_READ;
        }


        if (source_read != destination_read) {

            fmio_close(source_fd);
            fmio_close(destination_fd);

            fmio_set_error(
                status,
                FILEIO_ERROR_VERIFY_SIZE,
                0,
                "VERIFY SIZE"
            );

            return FILEIO_ERROR_VERIFY_SIZE;
        }


        if (source_read == 0)
            break;


        if (memcmp(
                verify_source_buffer,
                verify_destination_buffer,
                source_read
            ) != 0) {

            fmio_close(source_fd);
            fmio_close(destination_fd);

            fmio_set_error(
                status,
                FILEIO_ERROR_VERIFY_DATA,
                0,
                "VERIFY DATA"
            );

            return FILEIO_ERROR_VERIFY_DATA;
        }


        if (status)
            status->bytes_verified +=
                source_read;
    }


    fmio_close(source_fd);
    fmio_close(destination_fd);

    return FILEIO_COPY_OK;
}


/* ============================================================
 * COPIA ARQUIVO
 * ============================================================ */

static int fmio_copy_regular_file(
    const char *source,
    const char *destination,
    FileCopyStatus *status)
{
    int source_fd;
    int destination_fd;

    int bytes_read;
    int bytes_written;

    int write_offset;
    int result;


    source_fd = -1;
    destination_fd = -1;


    /*
     * Arquivo de destino já existente:
     *
     * NÃO sobrescrever.
     */
    if (fmio_path_exists(destination)) {

        fmio_set_error(
            status,
            FILEIO_ERROR_DEST_EXISTS,
            0,
            "FILE EXISTS"
        );

        return FILEIO_ERROR_DEST_EXISTS;
    }


    errno = 0;

    source_fd = fmio_open(
        source,
        O_RDONLY,
        0
    );

    if (source_fd < 0) {

        fmio_set_error(
            status,
            FILEIO_ERROR_OPEN_SOURCE,
            errno,
            "OPEN SOURCE"
        );

        return FILEIO_ERROR_OPEN_SOURCE;
    }


    errno = 0;

    destination_fd = fmio_open(
        destination,
        O_WRONLY | O_CREAT,
        0666
    );

    if (destination_fd < 0) {

        int saved_errno = errno;

        fmio_close(source_fd);

        fmio_set_error(
            status,
            FILEIO_ERROR_OPEN_DEST,
            saved_errno,
            "OPEN DEST"
        );

        return FILEIO_ERROR_OPEN_DEST;
    }


    while (1) {

        errno = 0;

        bytes_read = fmio_read(
            source_fd,
            copy_buffer,
            FMIO_BUFFER_SIZE
        );


        if (bytes_read < 0) {

            int saved_errno = errno;

            fmio_close(source_fd);
            fmio_close(destination_fd);

            fmio_set_error(
                status,
                FILEIO_ERROR_READ,
                saved_errno,
                "READ"
            );

            return FILEIO_ERROR_READ;
        }


        if (bytes_read == 0)
            break;


        write_offset = 0;


        while (write_offset <
               bytes_read) {

            errno = 0;

            bytes_written = fmio_write(
                destination_fd,
                copy_buffer +
                    write_offset,
                bytes_read -
                    write_offset
            );


            if (bytes_written <= 0) {

                int saved_errno = errno;

                fmio_close(source_fd);
                fmio_close(destination_fd);

                fmio_set_error(
                    status,
                    FILEIO_ERROR_WRITE,
                    saved_errno,
                    "WRITE"
                );

                return FILEIO_ERROR_WRITE;
            }


            write_offset +=
                bytes_written;


            if (status)
                status->bytes_copied +=
                    bytes_written;
        }
    }


    errno = 0;

    if (fmio_close(source_fd) < 0) {

        int saved_errno = errno;

        fmio_close(destination_fd);

        fmio_set_error(
            status,
            FILEIO_ERROR_CLOSE,
            saved_errno,
            "CLOSE SOURCE"
        );

        return FILEIO_ERROR_CLOSE;
    }


    source_fd = -1;


    errno = 0;

    if (fmio_close(destination_fd) < 0) {

        fmio_set_error(
            status,
            FILEIO_ERROR_CLOSE,
            errno,
            "CLOSE DEST"
        );

        return FILEIO_ERROR_CLOSE;
    }


    destination_fd = -1;


    /*
     * Verifica antes de considerar
     * o arquivo concluído.
     */
    result = fmio_verify_file(
        source,
        destination,
        status
    );


    if (result != FILEIO_COPY_OK)
        return result;


    if (status)
        status->files_copied++;


    return FILEIO_COPY_OK;
}


/* ============================================================
 * COPIA DIRETÓRIO
 * ============================================================ */

static int fmio_copy_directory(
    const char *source,
    const char *destination,
    FileCopyStatus *status,
    int recursion)
{
    DIR *dir;
    struct dirent *entry;

    char source_child[
        FMIO_MAX_PATH
    ];

    char destination_child[
        FMIO_MAX_PATH
    ];

    int result;
    int child_is_directory;


    if (recursion >
        FMIO_MAX_RECURSION) {

        fmio_set_error(
            status,
            FILEIO_ERROR_RECURSION,
            0,
            "RECURSION"
        );

        return FILEIO_ERROR_RECURSION;
    }


    /*
     * IMPORTANTE:
     *
     * Se chegamos aqui, sabemos que source
     * é uma pasta.
     *
     * Se destination não existe, criamos.
     *
     * Se destination já existe, NÃO retornamos
     * imediatamente -102.
     *
     * Isso permite continuar uma árvore que
     * tenha sido criada durante uma tentativa
     * anterior.
     *
     * Entretanto arquivos existentes continuam
     * protegidos e NÃO serão sobrescritos.
     */
    if (!fmio_path_exists(destination)) {

        errno = 0;

        if (mkdir(
                destination,
                0777
            ) < 0) {

            fmio_set_error(
                status,
                FILEIO_ERROR_MKDIR,
                errno,
                "MKDIR"
            );

            return FILEIO_ERROR_MKDIR;
        }


        if (status)
            status->directories_created++;
    }


    /*
     * Abre origem.
     */
    errno = 0;

    dir = opendir(source);


    if (!dir) {

        fmio_set_error(
            status,
            FILEIO_ERROR_OPENDIR,
            errno,
            "OPENDIR"
        );

        return FILEIO_ERROR_OPENDIR;
    }


    while (1) {

        errno = 0;

        entry = readdir(dir);


        if (!entry) {

            if (errno != 0) {

                int saved_errno = errno;

                closedir(dir);

                fmio_set_error(
                    status,
                    FILEIO_ERROR_READDIR,
                    saved_errno,
                    "READDIR"
                );

                return FILEIO_ERROR_READDIR;
            }

            break;
        }


        /*
         * Ignora "." e "..".
         */
        if (strcmp(
                entry->d_name,
                "."
            ) == 0)
            continue;


        if (strcmp(
                entry->d_name,
                ".."
            ) == 0)
            continue;


        /*
         * Caminho da origem.
         */
        if (!fmio_join_path(
                source,
                entry->d_name,
                source_child,
                sizeof(source_child))) {

            closedir(dir);

            fmio_set_error(
                status,
                FILEIO_ERROR_PATH_TOO_LONG,
                0,
                "SOURCE PATH"
            );

            return FILEIO_ERROR_PATH_TOO_LONG;
        }


        /*
         * Caminho do destino.
         */
        if (!fmio_join_path(
                destination,
                entry->d_name,
                destination_child,
                sizeof(destination_child))) {

            closedir(dir);

            fmio_set_error(
                status,
                FILEIO_ERROR_PATH_TOO_LONG,
                0,
                "DEST PATH"
            );

            return FILEIO_ERROR_PATH_TOO_LONG;
        }


        /*
         * Descobre se o filho é pasta
         * ou arquivo.
         */
        errno = 0;

        if (!fmio_get_type(
                source_child,
                &child_is_directory)) {

            int saved_errno = errno;

            closedir(dir);

            fmio_set_error(
                status,
                FILEIO_ERROR_STAT,
                saved_errno,
                "CHILD STAT"
            );

            return FILEIO_ERROR_STAT;
        }


        /*
         * Pasta -> recursão.
         */
        if (child_is_directory) {

            result =
                fmio_copy_directory(
                    source_child,
                    destination_child,
                    status,
                    recursion + 1
                );

        /*
         * Arquivo -> cópia + verificação.
         */
        } else {

            result =
                fmio_copy_regular_file(
                    source_child,
                    destination_child,
                    status
                );
        }


        if (result != FILEIO_COPY_OK) {

            closedir(dir);

            return result;
        }
    }


    closedir(dir);


    return FILEIO_COPY_OK;
}


/* ============================================================
 * API PÚBLICA
 *
 * NÃO ALTERAMOS A INTERFACE.
 * ============================================================ */

int fileio_copy_file(
    const char *source,
    const char *destination,
    FileCopyStatus *status)
{
    int source_is_directory;
    int result;


    fmio_status_init(status);


    /*
     * Caminhos válidos.
     */
    if (!source ||
        !destination ||
        source[0] == '\0' ||
        destination[0] == '\0') {

        fmio_set_error(
            status,
            FILEIO_ERROR_INVALID_PATH,
            0,
            "PATH"
        );

        return FILEIO_ERROR_INVALID_PATH;
    }


    /*
     * Origem == destino.
     */
    if (strcmp(
            source,
            destination
        ) == 0) {

        fmio_set_error(
            status,
            FILEIO_ERROR_SAME_PATH,
            0,
            "SAME PATH"
        );

        return FILEIO_ERROR_SAME_PATH;
    }


    /*
     * Descobre o tipo do objeto raiz.
     */
    errno = 0;

    if (!fmio_get_type(
            source,
            &source_is_directory)) {

        fmio_set_error(
            status,
            FILEIO_ERROR_STAT,
            errno,
            "SOURCE STAT"
        );

        return FILEIO_ERROR_STAT;
    }


    /*
     * Pasta.
     */
    if (source_is_directory) {

        result =
            fmio_copy_directory(
                source,
                destination,
                status,
                0
            );

    /*
     * Arquivo.
     */
    } else {

        result =
            fmio_copy_regular_file(
                source,
                destination,
                status
            );
    }


    if (result != FILEIO_COPY_OK)
        return result;


    if (status) {

        status->error =
            FILEIO_COPY_OK;

        status->system_result =
            0;

        strcpy(
            status->stage,
            "VERIFIED"
        );
    }


    return FILEIO_COPY_OK;
}


/* ============================================================
 * EXCLUSAO
 * ============================================================ */

static void fmio_delete_error(
    FileDeleteStatus *status,
    int error,
    int system_result,
    const char *stage)
{
    if (!status)
        return;

    status->error = error;
    status->system_result = system_result;

    strncpy(
        status->stage,
        stage,
        sizeof(status->stage) - 1
    );

    status->stage[
        sizeof(status->stage) - 1
    ] = '\0';
}


static int fmio_delete_path(
    const char *path,
    FileDeleteStatus *status,
    int recursion)
{
    DIR *dir;
    struct dirent *entry;

    char child[
        FMIO_MAX_PATH
    ];

    char *names;
    char *grown;

    int capacity;
    int count;
    int i;
    int result;
    int is_directory;


    if (recursion >
        FMIO_MAX_RECURSION) {

        fmio_delete_error(
            status,
            FILEIO_ERROR_RECURSION,
            0,
            "RECURSION"
        );

        return FILEIO_ERROR_RECURSION;
    }


    if (!fmio_get_type(
            path,
            &is_directory)) {

        fmio_delete_error(
            status,
            FILEIO_ERROR_NOT_FOUND,
            errno,
            "NOT FOUND"
        );

        return FILEIO_ERROR_NOT_FOUND;
    }


    /*
     * Arquivo.
     */
    if (!is_directory) {

        errno = 0;

        if (remove(path) < 0) {

            fmio_delete_error(
                status,
                FILEIO_ERROR_REMOVE,
                errno,
                "REMOVE"
            );

            return FILEIO_ERROR_REMOVE;
        }

        if (status)
            status->files_deleted++;

        return FILEIO_COPY_OK;
    }


    /*
     * Pasta: primeiro lemos TODOS os nomes e fechamos o
     * diretorio; so depois apagamos. Apagar durante o
     * readdir() pode pular entradas.
     */
    errno = 0;

    dir = opendir(path);

    if (!dir) {

        fmio_delete_error(
            status,
            FILEIO_ERROR_OPENDIR,
            errno,
            "OPENDIR"
        );

        return FILEIO_ERROR_OPENDIR;
    }


    names = NULL;
    capacity = 0;
    count = 0;


    while ((entry = readdir(dir)) != NULL) {

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;


        if (count >= capacity) {

            capacity =
                capacity ? capacity * 2 : 16;

            grown = realloc(
                names,
                capacity * FMIO_NAME_MAX
            );

            if (!grown) {

                free(names);
                closedir(dir);

                fmio_delete_error(
                    status,
                    FILEIO_ERROR_NO_MEMORY,
                    0,
                    "NO MEMORY"
                );

                return FILEIO_ERROR_NO_MEMORY;
            }

            names = grown;
        }


        strncpy(
            names + count * FMIO_NAME_MAX,
            entry->d_name,
            FMIO_NAME_MAX - 1
        );

        names[
            count * FMIO_NAME_MAX +
            FMIO_NAME_MAX - 1
        ] = '\0';

        count++;
    }


    closedir(dir);


    for (i = 0; i < count; i++) {

        if (!fmio_join_path(
                path,
                names + i * FMIO_NAME_MAX,
                child,
                sizeof(child))) {

            free(names);

            fmio_delete_error(
                status,
                FILEIO_ERROR_PATH_TOO_LONG,
                0,
                "CHILD PATH"
            );

            return FILEIO_ERROR_PATH_TOO_LONG;
        }


        result =
            fmio_delete_path(
                child,
                status,
                recursion + 1
            );


        if (result != FILEIO_COPY_OK) {

            free(names);

            return result;
        }
    }


    free(names);


    errno = 0;

    if (rmdir(path) < 0) {

        fmio_delete_error(
            status,
            FILEIO_ERROR_RMDIR,
            errno,
            "RMDIR"
        );

        return FILEIO_ERROR_RMDIR;
    }


    if (status)
        status->directories_deleted++;


    return FILEIO_COPY_OK;
}


int fileio_delete(
    const char *path,
    FileDeleteStatus *status)
{
    const char *colon;
    int result;


    if (status) {

        memset(
            status,
            0,
            sizeof(FileDeleteStatus)
        );

        strcpy(
            status->stage,
            "INIT"
        );
    }


    if (!path ||
        path[0] == '\0') {

        fmio_delete_error(
            status,
            FILEIO_ERROR_INVALID_PATH,
            0,
            "PATH"
        );

        return FILEIO_ERROR_INVALID_PATH;
    }


    /*
     * Nunca apagar a raiz de um dispositivo
     * ("mc0:", "mc0:/", "mass:/" ...).
     */
    colon = strchr(path, ':');

    if (!colon ||
        colon[1] == '\0' ||
        (colon[1] == '/' && colon[2] == '\0')) {

        fmio_delete_error(
            status,
            FILEIO_ERROR_PROTECTED,
            0,
            "PROTECTED ROOT"
        );

        return FILEIO_ERROR_PROTECTED;
    }


    result =
        fmio_delete_path(
            path,
            status,
            0
        );


    if (result != FILEIO_COPY_OK)
        return result;


    /*
     * Confirma que o caminho deixou de existir.
     */
    if (fmio_path_exists(path)) {

        fmio_delete_error(
            status,
            FILEIO_ERROR_DELETE_VERIFY,
            0,
            "STILL EXISTS"
        );

        return FILEIO_ERROR_DELETE_VERIFY;
    }


    if (status) {

        status->error = FILEIO_COPY_OK;
        status->system_result = 0;

        strcpy(
            status->stage,
            "DELETED"
        );
    }


    return FILEIO_COPY_OK;
}


/* ============================================================
 * ERROS
 * ============================================================ */

const char *fileio_error_string(
    int error)
{
    switch (error) {

        case FILEIO_COPY_OK:
            return "OK";

        case FILEIO_ERROR_INVALID_PATH:
            return "caminho invalido";

        case FILEIO_ERROR_SAME_PATH:
            return "origem e destino iguais";

        case FILEIO_ERROR_DEST_EXISTS:
            return "arquivo destino ja existe";

        case FILEIO_ERROR_OPEN_SOURCE:
            return "falha ao abrir origem";

        case FILEIO_ERROR_OPEN_DEST:
            return "falha ao criar destino";

        case FILEIO_ERROR_READ:
            return "falha de leitura";

        case FILEIO_ERROR_WRITE:
            return "falha de escrita";

        case FILEIO_ERROR_CLOSE:
            return "falha ao fechar arquivo";

        case FILEIO_ERROR_VERIFY_OPEN:
            return "falha ao abrir verificacao";

        case FILEIO_ERROR_VERIFY_READ:
            return "falha ao ler verificacao";

        case FILEIO_ERROR_VERIFY_DATA:
            return "conteudo diferente";

        case FILEIO_ERROR_VERIFY_SIZE:
            return "tamanho diferente";

        case FILEIO_ERROR_STAT:
            return "falha ao consultar objeto";

        case FILEIO_ERROR_MKDIR:
            return "falha ao criar diretorio";

        case FILEIO_ERROR_OPENDIR:
            return "falha ao abrir diretorio";

        case FILEIO_ERROR_READDIR:
            return "falha ao ler diretorio";

        case FILEIO_ERROR_PATH_TOO_LONG:
            return "caminho muito longo";

        case FILEIO_ERROR_RECURSION:
            return "limite de recursao";

        case FILEIO_ERROR_REMOVE:
            return "falha ao excluir arquivo";

        case FILEIO_ERROR_RMDIR:
            return "falha ao excluir pasta";

        case FILEIO_ERROR_DELETE_VERIFY:
            return "item ainda existe";

        case FILEIO_ERROR_PROTECTED:
            return "raiz do dispositivo protegida";

        case FILEIO_ERROR_NOT_FOUND:
            return "item nao encontrado";

        case FILEIO_ERROR_NO_MEMORY:
            return "memoria insuficiente";

        default:
            return "erro desconhecido";
    }
}