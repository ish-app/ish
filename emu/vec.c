#include <math.h>
#include <string.h>

#include "emu/vec.h"
#include "emu/cpu.h"

void vec_compare32(struct cpu_state *cpu, float *f2, float *f1) {
    if (isnan(*f1) || isnan(*f2)) {
        cpu->zf = 1;
        cpu->pf = 1;
        cpu->cf = 1;
    }
    else if (*f1 > *f2) {
        cpu->zf = 0;
        cpu->pf = 0;
        cpu->cf = 0;
    }
    else if (*f1 < *f2) {
        cpu->zf = 0;
        cpu->pf = 0;
        cpu->cf = 1;
    }
    else if (*f1 == *f2) {
        cpu->zf = 1;
        cpu->pf = 0;
        cpu->cf = 0;
    }
    else {
        printf("something's horribly wrong. err 1093281094");
    }
    cpu->zf_res = 0;
    cpu->pf_res = 0;
}

static inline void zero_xmm(union xmm_reg *xmm) {
    xmm->qw[0] = 0;
    xmm->qw[1] = 0;
}

#define VEC_ZERO_COPY(zero, copy) \
    void vec_zero##zero##_copy##copy(NO_CPU, const void *src, void *dst) { \
        memset(dst, 0, zero/8); \
        memcpy(dst, src, copy/8); \
    }
VEC_ZERO_COPY(128, 128)
VEC_ZERO_COPY(128, 64)
VEC_ZERO_COPY(128, 32)
VEC_ZERO_COPY(64, 64)

void vec_merge32(NO_CPU, const void *src, void *dst) {
    memcpy(dst, src, 4);
}
void vec_merge64(NO_CPU, const void *src, void *dst) {
    memcpy(dst, src, 8);
}
void vec_merge128(NO_CPU, const void *src, void *dst) {
    memcpy(dst, src, 16);
}

void vec_imm_shiftr64(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 63) {
        zero_xmm(dst);
    } else {
        dst->qw[0] >>= amount;
        dst->qw[1] >>= amount;
    }
}

void vec_xor128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] ^= src->qw[0];
    dst->qw[1] ^= src->qw[1];
}

void vec_fadds64(NO_CPU, const double *src, double *dst) {
    *dst += *src;
}
void vec_fmuls64(NO_CPU, const double *src, double *dst) {
    *dst *= *src;
}
void vec_fsubs64(NO_CPU, const double *src, double *dst) {
    *dst -= *src;
}
void vec_fdivs64(NO_CPU, const double *src, double *dst) {
    *dst /= *src;
}

void vec_cvtsi2sd32(NO_CPU, const uint32_t *src, double *dst) {
    *dst = *src;
}
void vec_cvtsd2si64(NO_CPU, const double *src, uint32_t *dst) {
    *dst = *src;
}
void vec_cvtsd2ss64(NO_CPU, const double *src, float *dst) {
    *dst = *src;
}
