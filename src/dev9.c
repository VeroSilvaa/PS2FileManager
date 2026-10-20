#include <stdio.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>

#include "dev9.h"

/*
 * O DEV9 (porta de expansao: rede e HDD) e usado pela rede
 * (network.c) e pelo HDD (hdd.c). Carregar o IRX duas vezes
 * deixa o IOP em estado invalido, entao ele fica aqui,
 * carregado uma unica vez.
 */

extern unsigned char DEV9_irx[];
extern unsigned int size_DEV9_irx;

static int dev9_loaded = 0;


int dev9_load(void)
{
    int id;
    int result = 0;


    if (dev9_loaded)
        return 0;


    id = SifExecModuleBuffer(
        DEV9_irx,
        size_DEV9_irx,
        0,
        NULL,
        &result
    );

    printf(
        "DEV9: id %d ret %d\n",
        id,
        result
    );

    if (id < 0)
        return id;


    dev9_loaded = 1;

    return 0;
}
