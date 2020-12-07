#include <math.h>
#include <string.h>

#include "emu/vec.h"
#include "emu/cpu.h"

static inline void zero_xmm(union xmm_reg *xmm) {
    xmm->qw[0] = 0;
    xmm->qw[1] = 0;
}

#define VEC_ZERO_COPY(zero, copy) \
    void vec_zero##zero##_copy##copy(NO_CPU, const void *src, void *dst) { \
        memcpy(dst, src, copy/8); \
        memset((char *) dst + copy/8, 0, (zero-copy)/8); \
    }
VEC_ZERO_COPY(128, 128)
VEC_ZERO_COPY(128, 64)
VEC_ZERO_COPY(128, 32)
VEC_ZERO_COPY(64, 64)
VEC_ZERO_COPY(64, 32)
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
void vec_imm_shiftl_q64(NO_CPU, const uint8_t amount, union mm_reg *dst) {
    if (amount > 63)
        dst->qw = 0;
    else
        dst->qw <<= amount;
}

void vec_imm_shiftr_q128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 63) {
        zero_xmm(dst);
    } else {
        dst->qw[0] >>= amount;
        dst->qw[1] >>= amount;
    }
}
void vec_imm_shiftr_q64(NO_CPU, const uint8_t amount, union mm_reg *dst) {
    if (amount > 63)
        dst->qw = 0;
    else
        dst->qw >>= amount;
}

void vec_imm_shiftl_dq128(NO_CPU, uint8_t amount, union xmm_reg *dst) {
    if (amount >= 16)
        zero_xmm(dst);
    else
        dst->u128 <<= amount * 8;
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

void vec_add_b128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        dst->u8[i] += src->u8[i];
}
void vec_add_d128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u32); i++)
        dst->u32[i] += src->u32[i];
}
void vec_add_q128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] += src->qw[0];
    dst->qw[1] += src->qw[1];
}
void vec_add_q64(NO_CPU, union mm_reg *src, union mm_reg *dst) {
    dst->qw += src->qw;
}
void vec_sub_q128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] -= src->qw[0];
    dst->qw[1] -= src->qw[1];
}

void vec_mulu_dq128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] = (uint64_t) src->u32[0] * dst->u32[0];
    dst->qw[1] = (uint64_t) src->u32[2] * dst->u32[2];
}
void vec_mulu_dq64(NO_CPU, union mm_reg *src, union mm_reg *dst) {
    dst->qw = (uint64_t) src->dw[0] * dst->dw[0];
}

void vec_and128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] &= src->qw[0];
    dst->qw[1] &= src->qw[1];
}
void vec_and64(NO_CPU, union mm_reg *src, union mm_reg *dst) {
    dst->qw &= src->qw;
}
void vec_andn128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] = ~dst->qw[0] & src->qw[0];
    dst->qw[1] = ~dst->qw[1] & src->qw[1];
}
void vec_or128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] |= src->qw[0];
    dst->qw[1] |= src->qw[1];
}
void vec_xor128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] ^= src->qw[0];
    dst->qw[1] ^= src->qw[1];
}
void vec_xor64(NO_CPU, union mm_reg *src, union mm_reg *dst) {
    dst->qw ^= src->qw;
}

void vec_min_ub128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        if (src->u8[i] < dst->u8[i])
            dst->u8[i] = src->u8[i];
}

static bool cmpd(double a, double b, int type) {
    bool res;
    switch (type % 4) {
        case 0: res = a == b; break;
        case 1: res = a < b; break;
        case 2: res = a <= b; break;
        case 3: res = isnan(a) || isnan(b); break;
    }
    if (type >= 4) res = !res;
    return res;
}

void vec_single_fcmp64(NO_CPU, const double *src, union xmm_reg *dst, uint8_t type) {
    dst->qw[0] = cmpd(dst->f64[0], *src, type) ? -1 : 0;
}

void vec_single_fadd64(NO_CPU, const double *src, double *dst) { *dst += *src; }
void vec_single_fadd32(NO_CPU, const float *src, float *dst) { *dst += *src; }
void vec_single_fmul64(NO_CPU, const double *src, double *dst) { *dst *= *src; }
void vec_single_fmul32(NO_CPU, const float *src, float *dst) { *dst *= *src; }
void vec_single_fsub64(NO_CPU, const double *src, double *dst) { *dst -= *src; }
void vec_single_fsub32(NO_CPU, const float *src, float *dst) { *dst -= *src; }
void vec_single_fdiv64(NO_CPU, const double *src, double *dst) { *dst /= *src; }
void vec_single_fdiv32(NO_CPU, const float *src, float *dst) { *dst /= *src; }

void vec_single_fsqrt64(NO_CPU, const double *src, double *dst) { *dst = sqrt(*src); }

void vec_single_fmax64(NO_CPU, const double *src, double *dst) {
    if (*src > *dst || isnan(*src) || isnan(*dst)) *dst = *src;
}
void vec_single_fmin64(NO_CPU, const double *src, double *dst) {
    if (*src < *dst || isnan(*src) || isnan(*dst)) *dst = *src;
}

void vec_single_ucomi32(struct cpu_state *cpu, const float *src, const float *dst) {
    cpu->zf_res = cpu->pf_res = 0;
    cpu->zf = *src == *dst;
    cpu->cf = *src > *dst;
    cpu->pf = 0;
    if (isnan(*src) || isnan(*dst))
        cpu->zf = cpu->cf = cpu->pf = 1;
    cpu->of = cpu->sf = cpu->af = 0;
    cpu->sf_res = 0;
}

void vec_single_ucomi64(struct cpu_state *cpu, const double *src, const double *dst) {
    cpu->zf_res = cpu->pf_res = 0;
    cpu->zf = *src == *dst;
    cpu->cf = *src > *dst;
    cpu->pf = 0;
    if (isnan(*src) || isnan(*dst))
        cpu->zf = cpu->cf = cpu->pf = 1;
    cpu->of = cpu->sf = cpu->af = 0;
    cpu->sf_res = 0;
}

// come to the dark side of macros
#define _ISNAN_int32_t(x) false
#define _ISNAN_float(x) isnan(x)
#define _ISNAN_double(x) isnan(x)
#define _ISNAN(x, t) _ISNAN_##t(x)
#define VEC_CVT(name, src_t, dst_t) \
    void vec_cvt##name(NO_CPU, const src_t *src, dst_t *dst) { \
        if (_ISNAN(*src, src_t)) \
            *dst = INT32_MIN; \
        else \
            *dst = *src; \
    }
VEC_CVT(si2sd32, int32_t, double)
VEC_CVT(tsd2si64, double, int32_t)
VEC_CVT(sd2ss64, double, float)
VEC_CVT(si2ss32, int32_t, float)
VEC_CVT(tss2si32, float, int32_t)
VEC_CVT(ss2sd32, float, double)

void vec_unpack_bw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 7; i >= 0; i--) {
        dst->u8[i*2 + 1] = src->u8[i];
        dst->u8[i*2] = dst->u8[i];
    }
}
void vec_unpack_dq128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->u32[3] = src->u32[1];
    dst->u32[2] = dst->u32[1];
    dst->u32[1] = src->u32[0];
}
void vec_unpack_dq64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    dst->dw[1] = src->dw[0];
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

void vec_fmovmask_d128(NO_CPU, const union xmm_reg *src, uint32_t *dst) {
    *dst = 0;
    for (unsigned i = 0; i < array_size(src->f64); i++) {
        if (signbit(src->f64[i]))
            *dst |= 1 << i;
    }
}

void vec_extract_w128(NO_CPU, const union xmm_reg *src, uint32_t *dst, uint8_t index) {
    *dst = src->u16[index % 8];
}
