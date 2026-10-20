#include <stdio.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>

#include "bdmcore.h"

extern unsigned char BDM_irx[];
extern unsigned int size_BDM_irx;

extern unsigned char BDMFS_FATFS_irx[];
extern unsigned int size_BDMFS_FATFS_irx;

extern unsigned char FATFMT_irx[];
extern unsigned int size_FATFMT_irx;

static int core_loaded = 0;
static int fatfmt_loaded = 0;


static int bdm_load(const char *name, void *buffer, unsigned int size)
{
    int id;
    int result = 0;

    id = SifExecModuleBuffer(buffer, size, 0, NULL, &result);

    printf("BDMCORE: %s id %d ret %d\n", name, id, result);

    if (id < 0)
        return id;

    /*
     * ret 1 = NO_RESIDENT_END: o modulo descarregou.
     */
    if (result == 1)
        return -1;

    return 0;
}


int bdm_core_load(void)
{
    int result;

    if (core_loaded)
        return 0;

    result = bdm_load("BDM", BDM_irx, size_BDM_irx);

    if (result < 0)
        return result;

    result = bdm_load("BDMFS_FATFS", BDMFS_FATFS_irx, size_BDMFS_FATFS_irx);

    if (result < 0)
        return result;

    core_loaded = 1;

    return 0;
}


int bdm_fatfmt_load(void)
{
    if (!fatfmt_loaded && bdm_core_load() == 0)
        fatfmt_loaded = bdm_load("FATFMT", FATFMT_irx, size_FATFMT_irx) == 0;

    return fatfmt_loaded;
}
