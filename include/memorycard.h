#ifndef MEMORYCARD_H
#define MEMORYCARD_H

#define MC_COPY_OK                  0

#define MC_COPY_ERROR_PATH         -100
#define MC_COPY_ERROR_EXISTS       -101
#define MC_COPY_ERROR_OPEN_SOURCE  -102
#define MC_COPY_ERROR_OPEN_DEST    -103
#define MC_COPY_ERROR_READ         -104
#define MC_COPY_ERROR_WRITE        -105
#define MC_COPY_ERROR_FLUSH        -106
#define MC_COPY_ERROR_CLOSE        -107
#define MC_COPY_ERROR_VERIFY_OPEN  -108
#define MC_COPY_ERROR_VERIFY_READ  -109
#define MC_COPY_ERROR_VERIFY_DATA  -110
#define MC_COPY_ERROR_VERIFY_SIZE  -111

typedef struct {
    int getinfo_return;
    int sync_return;
    int cmd;
    int result;
    int type;
    int free_space;
    int format;
} MemoryCardDebug;

typedef struct {
    int error;
    int mc_result;
    int bytes_copied;
    int bytes_verified;
    char stage[32];
} MemoryCardCopyStatus;

int memorycard_init(void);

int memorycard_available(int port);

int memorycard_list(
    const char *path,
    void *entries,
    int max_entries
);

int memorycard_debug_info(
    int port,
    MemoryCardDebug *info
);

int memorycard_copy_file(
    const char *source,
    const char *destination,
    MemoryCardCopyStatus *status
);

const char *memorycard_result_string(
    int result
);

#endif