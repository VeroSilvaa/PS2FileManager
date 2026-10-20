#ifndef _MODULE_DEBUG_H
#define _MODULE_DEBUG_H

/*
 * Macros usadas pelo ff.c do fork do PS2SDK (sem saida de debug).
 */
#define M_PRINTF(format, args...) printf("fatfmt: " format, ##args)
#define M_DEBUG(format, args...)

#define DEBUG_U64_2XU32(val) do { } while (0)

#endif
