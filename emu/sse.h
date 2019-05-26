#ifndef EMU_SSE_H
#define EMU_SSE_H

#include "emu/cpu.h"

void vec_load64(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src);
void vec_store64(struct cpu_state *UNUSED(cpu), union xmm_reg *src, union xmm_reg *dst);

#endif
