#include "emu/cpu.h"

void vec_load64(struct cpu_state *UNUSED(cpu), union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] = src->qw[0];
}

void vec_store64(struct cpu_state *UNUSED(cpu), union xmm_reg *dst, union xmm_reg *src) {
    dst->qw[0] = src->qw[0];
}
