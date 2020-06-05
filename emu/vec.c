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
VEC_ZERO_COPY(32, 32)

void vec_merge32(NO_CPU, const void *src, void *dst) {
    memcpy(dst, src, 4);
}
void vec_merge64(NO_CPU, const void *src, void *dst) {
    memcpy(dst, src, 8);
}
void vec_merge128(NO_CPU, const void *src, void *dst) {
    memcpy(dst, src, 16);
}

void vec_imm_shiftl_q128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 63) {
        zero_xmm(dst);
    } else {
        dst->qw[0] <<= amount;
        dst->qw[1] <<= amount;
    }
}

void vec_imm_shiftr_q128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 63) {
        zero_xmm(dst);
    } else {
        dst->qw[0] >>= amount;
        dst->qw[1] >>= amount;
    }
}

void vec_shiftl_q128(NO_CPU, union xmm_reg *amount, union xmm_reg *dst) {
    uint64_t amount_qw = amount->qw[0];

    if (amount_qw > 63) {
        zero_xmm(dst);
    } else {
        dst->qw[0] <<= amount_qw;
        dst->qw[1] <<= amount_qw;
    }
}

void vec_shiftr_q128(NO_CPU, union xmm_reg *amount, union xmm_reg *dst) {
    uint64_t amount_qw = amount->qw[0];

    if (amount_qw > 63) {
        zero_xmm(dst);
    } else {
        dst->qw[0] >>= amount_qw;
        dst->qw[1] >>= amount_qw;
    }
}

void vec_and128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] &= src->qw[0];
    dst->qw[1] &= src->qw[1];
}
void vec_or128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] |= src->qw[0];
    dst->qw[1] |= src->qw[1];
}
void vec_xor128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] ^= src->qw[0];
    dst->qw[1] ^= src->qw[1];
}

void vec_add_b128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        dst->u8[i] += src->u8[i];
}
void vec_add_q128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] += src->qw[0];
    dst->qw[1] += src->qw[1];
}

void vec_min_ub128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        if (src->u8[i] < dst->u8[i])
            dst->u8[i] = src->u8[i];
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

#define VEC_CVT(name, src_t, dst_t) \
    void vec_cvt##name(NO_CPU, const src_t *src, dst_t *dst) { \
        *dst = *src; \
    }
VEC_CVT(si2sd32, uint32_t, double)
VEC_CVT(si2ss32, uint32_t, float)
VEC_CVT(tsd2si64, double, uint32_t)
VEC_CVT(sd2ss64, double, float)
VEC_CVT(ss2sd32, float, double)

void vec_unpack_bw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 7; i >= 0; i--) {
        dst->u8[i*2 + 1] = src->u8[i];
        dst->u8[i*2] = dst->u8[i];
    }
}
void vec_unpack_qdq128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[1] = src->qw[0];
}

void vec_shuffle_lw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst, uint8_t encoding) {
    union xmm_reg src_copy = *src;
    for (int i = 0; i < 4; i++)
        dst->u16[i] = src_copy.u16[(encoding >> (i*2)) % 4];
    dst->qw[1] = src->qw[1];
}
void vec_shuffle_d128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst, uint8_t encoding) {
    union xmm_reg src_copy = *src;
    for (int i = 0; i < 4; i++)
        dst->u32[i] = src_copy.u32[(encoding >> (i*2)) % 4];
}

void vec_compare_eqb128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        dst->u8[i] = dst->u8[i] == src->u8[i] ? ~0 : 0;
}
void vec_compare_eqd128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u32); i++)
        dst->u32[i] = dst->u32[i] == src->u32[i] ? ~0 : 0;
}

void vec_movmask_b128(NO_CPU, const union xmm_reg *src, uint32_t *dst) {
    *dst = 0;
    for (unsigned i = 0; i < array_size(src->u8); i++) {
        if (src->u8[i] & (1 << 7))
            *dst |= 1 << i;
    }
}

void vec_extract_w128(NO_CPU, const union xmm_reg *src, uint32_t *dst, uint8_t index) {
    *dst = src->u16[index % 8];
}
