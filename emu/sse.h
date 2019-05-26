#ifndef EMU_SSE_H
#define EMU_SSE_H

#include "emu/cpu.h"

void vec_compare32(struct cpu_state *UNUSED(cpu), float *f2, float *f1);

void vec_load32(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src);
void vec_load64(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src);
void vec_load128(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src);

// Zeroes out the destination before loading.
// Used in some instructions like movss when the src is memory.
void vec_zload32(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src);
void vec_zload64(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src);
void vec_zload128(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src);

void vec_store32(struct cpu_state *UNUSED(cpu), union xmm_reg *src, union xmm_reg *dst);
void vec_store64(struct cpu_state *UNUSED(cpu), union xmm_reg *src, union xmm_reg *dst);
void vec_store128(struct cpu_state *UNUSED(cpu), union xmm_reg *src, union xmm_reg *dst);

void vec_xor128(struct cpu_state *cpu, union xmm_reg *src, union xmm_reg *dst);

#endif
