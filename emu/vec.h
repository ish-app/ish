#ifndef EMU_SSE_H
#define EMU_SSE_H

#include "emu/cpu.h"

#define NO_CPU struct cpu_state *UNUSED(cpu)
void vec_compare32(NO_CPU, float *f2, float *f1);

// arguments are in src, dst order

void vec_zero128_copy128(NO_CPU, const void *src, void *dst);
void vec_zero128_copy64(NO_CPU, const void *src, void *dst);
void vec_zero128_copy32(NO_CPU, const void *src, void *dst);
void vec_zero64_copy64(NO_CPU, const void *src, void *dst);

// "merge" means don't zero the register before writing to it
void vec_merge32(NO_CPU, const void *src, void *dst);
void vec_merge64(NO_CPU, const void *src, void *dst);
void vec_merge128(NO_CPU, const void *src, void *dst);

void vec_imm_shiftr64(NO_CPU, const uint8_t amount, union xmm_reg *dst);
void vec_xor128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);

void vec_fadds64(NO_CPU, const double *src, double *dst);
void vec_fmuls64(NO_CPU, const double *src, double *dst);
void vec_fsubs64(NO_CPU, const double *src, double *dst);

#endif
