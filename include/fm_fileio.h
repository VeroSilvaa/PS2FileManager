#ifndef FM_FILEIO_H
#define FM_FILEIO_H

#define FILEIO_COPY_OK                  0

#define FILEIO_ERROR_INVALID_PATH     -100
#define FILEIO_ERROR_SAME_PATH        -101
#define FILEIO_ERROR_DEST_EXISTS      -102
#define FILEIO_ERROR_OPEN_SOURCE      -103
#define FILEIO_ERROR_OPEN_DEST        -104
#define FILEIO_ERROR_READ             -105
#define FILEIO_ERROR_WRITE            -106
#define FILEIO_ERROR_CLOSE            -107
#define FILEIO_ERROR_VERIFY_OPEN      -108
#define FILEIO_ERROR_VERIFY_READ      -109
#define FILEIO_ERROR_VERIFY_DATA      -110
#define FILEIO_ERROR_VERIFY_SIZE      -111
#define FILEIO_ERROR_STAT             -112
#define FILEIO_ERROR_MKDIR            -113
#define FILEIO_ERROR_OPENDIR          -114
#define FILEIO_ERROR_READDIR          -115
#define FILEIO_ERROR_PATH_TOO_LONG    -116
#define FILEIO_ERROR_RECURSION        -117

typedef struct {
    int error;
    int system_result;

    int bytes_copied;
    int bytes_verified;

    int files_copied;
    int directories_created;

    char stage[32];
} FileCopyStatus;


/*
 * Copia arquivo ou diretório.
 *
 * A interface continua usando exatamente
 * a mesma função.
 */
int fileio_copy_file(
    const char *source,
    const char *destination,
    FileCopyStatus *status
);

const char *fileio_error_string(
    int error
);

#endif