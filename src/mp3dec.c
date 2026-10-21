/*
 * Implementacao do minimp3 (third_party/minimp3, dominio publico CC0).
 *
 * Compilado com -fsingle-precision-constant (Makefile): o minimp3
 * compara amostras com constantes double (32766.5), e o EE so tem
 * FPU de precisao simples (double seria emulado por software).
 */

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD

#include "minimp3.h"
