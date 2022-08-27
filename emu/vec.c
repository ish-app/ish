#include <math.h>
#include <string.h>

#include "emu/vec.h"
#include "emu/cpu.h"

static inline void zero_xmm(union xmm_reg *xmm) {
    xmm->qw[0] = 0;
    xmm->qw[1] = 0;
}

static inline int32_t satw(int32_t dw) {
    if (dw > 0xff80)
        dw &= 0xff;
    else if (dw > 0x7fff)
        dw = 0x80;
    else if (dw > 0x7f)
        dw = 0x7f;
    return dw;
}
static inline uint32_t satd(uint32_t dw) {
    if (dw > 0xffff8000)
        dw &= 0xffff;
    else if (dw > 0x7fffffff)
        dw = 0x8000;
    else if (dw > 0x7fff)
        dw = 0x7fff;
    return dw;
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

void vec_imm_shiftl_w128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 15) {
        zero_xmm(dst);
    } else {
        for (int i = 0; i < 8; i++) {
            dst->u16[i] <<= amount;
        }
    }
}
void vec_imm_shiftl_q128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 63) {
        zero_xmm(dst);
    } else {
        dst->qw[0] <<= amount;
        dst->qw[1] <<= amount;
    }
}
void vec_imm_shiftl_d128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 31) {
        zero_xmm(dst);
    } else {
        dst->u32[0] <<= amount;
        dst->u32[1] <<= amount;
        dst->u32[2] <<= amount;
        dst->u32[3] <<= amount;
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
void vec_imm_shiftr_d128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 31) {
        zero_xmm(dst);
    } else {
        dst->u32[0] >>= amount;
        dst->u32[1] >>= amount;
        dst->u32[2] >>= amount;
        dst->u32[3] >>= amount;
    }
}
void vec_imm_shiftr_w128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    if (amount > 15) {
        zero_xmm(dst);
    } else {
        for (unsigned i = 0; i < 8; i++) {
            dst->u16[i] >>= amount;
        }
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

void vec_imm_shiftr_dq128(NO_CPU, uint8_t amount, union xmm_reg *dst) {
    if (amount >= 16)
        zero_xmm(dst);
    else
        dst->u128 >>= amount * 8;
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

void vec_imm_shiftrs_w128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    for (unsigned i = 0; i < 8; i++) {
        if (amount > 15)
            dst->u16[i] = ((dst->u16[i] >> 15) & (uint16_t)1) ? 0xffff : 0;
        else
            dst->u16[i] = ((int16_t)(dst->u16[i])) >> amount;
    }
}
void vec_imm_shiftrs_d128(NO_CPU, const uint8_t amount, union xmm_reg *dst) {
    for (unsigned i = 0; i < 4; i++) {
        if (amount > 31)
            dst->u32[i] = ((dst->u32[i] >> 31) & (uint32_t)1) ? 0xffffffff : 0;
        else
            dst->u32[i] = ((int32_t)(dst->u32[i])) >> amount;
    }
}

void vec_add_b128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        dst->u8[i] += src->u8[i];
}
void vec_add_w128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u16); i++)
        dst->u16[i] += src->u16[i];
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
void vec_sub_b128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        dst->u8[i] -= src->u8[i];
}
void vec_sub_w128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u16); i++)
        dst->u16[i] -= src->u16[i];
}
void vec_sub_d128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u32); i++)
        dst->u32[i] -= src->u32[i];
}
void vec_sub_q128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] -= src->qw[0];
    dst->qw[1] -= src->qw[1];
}

void vec_madd_d128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    dst->u32[0] = (int32_t)((int16_t)dst->u16[0] * (int16_t)src->u16[0]) +
                  (int32_t)((int16_t)dst->u16[1] * (int16_t)src->u16[1]);
    dst->u32[1] = (int32_t)((int16_t)dst->u16[2] * (int16_t)src->u16[2]) +
                  (int32_t)((int16_t)dst->u16[3] * (int16_t)src->u16[3]);
    dst->u32[2] = (int32_t)((int16_t)dst->u16[4] * (int16_t)src->u16[4]) +
                  (int32_t)((int16_t)dst->u16[5] * (int16_t)src->u16[5]);
    dst->u32[3] = (int32_t)((int16_t)dst->u16[6] * (int16_t)src->u16[6]) +
                  (int32_t)((int16_t)dst->u16[7] * (int16_t)src->u16[7]);
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

void vec_max_ub128(NO_CPU, union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u8); i++)
        if (src->u8[i] > dst->u8[i])
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

#define VEC_PACKED_OP(name, op, field, size, n) \
    void vec_##name##size(NO_CPU, union xmm_reg *src, union xmm_reg *dst) { \
        for (int i = 0; i < n; ++i) { \
            dst->field[i] op##= src->field[i]; \
        } \
    }

VEC_PACKED_OP(add_p, +, f64, 64, 2)
VEC_PACKED_OP(add_p, +, f32, 32, 4)
VEC_PACKED_OP(sub_p, -, f64, 64, 2)
VEC_PACKED_OP(sub_p, -, f32, 32, 4)
VEC_PACKED_OP(mul_p, *, f64, 64, 2)
VEC_PACKED_OP(mul_p, *, f32, 32, 4)

void vec_fcmp_p64(NO_CPU, const union xmm_reg *src, union xmm_reg *dst, uint8_t type) {
    for (size_t i = 0; i < sizeof(dst->f64) / sizeof(*dst->f64); ++i) {
        dst->qw[i] = cmpd(dst->f64[i], src->f64[i], type) ? -1 : 0;
    }
}

// come to the dark side of macros
#define _ISNAN_int32_t(x) false
#define _ISNAN_float(x) isnan(x)
#define _ISNAN_double(x) isnan(x)
#define _ISNAN(x, t) _ISNAN_##t(x)
#define _VEC_CVT(src, dst, src_t, dst_t, n) \
    do { \
        for (int i = 0; i < n; ++i) { \
            if (_ISNAN(((src_t *)src)[i], src_t)) \
                ((dst_t *)dst)[i] = INT32_MIN; \
            else \
                ((dst_t *)dst)[i] = ((src_t *)src)[i]; \
        } \
    } while (0)

#define VEC_CVT(name, src_t, dst_t) \
    void vec_cvt##name(NO_CPU, const src_t *src, dst_t *dst) { \
        _VEC_CVT(src, dst, src_t, dst_t, 1); \
    }

#define PACKED_VEC_CVT(name, src_field, dst_field, src_t, dst_t, n) \
    void vec_cvt##name(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) { \
        _VEC_CVT(src->src_field, dst->dst_field, src_t, dst_t, n); \
        /* Note: this needs to be second, because src and dst may alias */ \
        memset(dst->dst_field + n, 0, sizeof(*dst) - n * sizeof(*dst->dst_field)); \
    }

VEC_CVT(si2sd32, int32_t, double)
VEC_CVT(tsd2si64, double, int32_t)
VEC_CVT(sd2ss64, double, float)
VEC_CVT(si2ss32, int32_t, float)
VEC_CVT(tss2si32, float, int32_t)
VEC_CVT(ss2sd32, float, double)

PACKED_VEC_CVT(tpd2dq64, f64, u32, double, int32_t, 2)
PACKED_VEC_CVT(tps2dq32, f32, u32, float, int32_t, 4)

void vec_unpackl_bw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 7; i >= 0; i--) {
        dst->u8[i*2 + 1] = src->u8[i];
        dst->u8[i*2] = dst->u8[i];
    }
}
void vec_unpackl_w128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 3; i >= 0; i--) {
        dst->u16[i*2 + 1] = src->u16[i];
        dst->u16[i*2] = dst->u16[i];
    }
}
void vec_unpackl_dq128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->u32[3] = src->u32[1];
    dst->u32[2] = dst->u32[1];
    dst->u32[1] = src->u32[0];
}
void vec_unpackl_dq64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    dst->dw[1] = src->dw[0];
}
void vec_unpackl_qdq128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[1] = src->qw[0];
}
void vec_unpackh_bw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 0; i < 8; i++) {
        dst->u8[2 * i + 0] = dst->u8[i + 8];
        dst->u8[2 * i + 1] = src->u8[i + 8];
    }
}
void vec_unpackh_w128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 0; i < 4; i++) {
        dst->u16[2 * i + 0] = dst->u16[i + 4];
        dst->u16[2 * i + 1] = src->u16[i + 4];
    }
}
void vec_unpackh_d128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->u32[0] = dst->u32[2];
    dst->u32[1] = src->u32[2];
    dst->u32[2] = dst->u32[3];
    dst->u32[3] = src->u32[3];
}
void vec_unpackh_dq128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->qw[0] = dst->qw[1];
    dst->qw[1] = src->qw[1];
}

void vec_packss_w128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->u32[0] = (satw(dst->u16[0]) << 0x00) | (satw(dst->u16[1]) << 0x08) |
                  (satw(dst->u16[2]) << 0x10) | (satw(dst->u16[3]) << 0x18);
    dst->u32[1] = (satw(dst->u16[4]) << 0x00) | (satw(dst->u16[5]) << 0x08) |
                  (satw(dst->u16[6]) << 0x10) | (satw(dst->u16[7]) << 0x18);
    dst->u32[2] = (satw(src->u16[0]) << 0x00) | (satw(src->u16[1]) << 0x08) |
                  (satw(src->u16[2]) << 0x10) | (satw(src->u16[3]) << 0x18);
    dst->u32[3] = (satw(src->u16[4]) << 0x00) | (satw(src->u16[5]) << 0x08) |
                  (satw(src->u16[6]) << 0x10) | (satw(src->u16[7]) << 0x18);
}
void vec_packss_d128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    dst->u32[0] = satd(dst->u32[0]) | (satd(dst->u32[1]) << 16);
    dst->u32[1] = satd(dst->u32[2]) | (satd(dst->u32[3]) << 16);
    dst->u32[2] = satd(src->u32[0]) | (satd(src->u32[1]) << 16);
    dst->u32[3] = satd(src->u32[2]) | (satd(src->u32[3]) << 16);
}

void vec_shuffle_lw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst, uint8_t encoding) {
    union xmm_reg src_copy = *src;
    for (int i = 0; i < 4; i++)
        dst->u16[i] = src_copy.u16[(encoding >> (i*2)) % 4];
    dst->qw[1] = src->qw[1];
}
void vec_shuffle_hw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst, uint8_t encoding) {
    union xmm_reg src_copy = *src;
    dst->qw[0] = src->qw[0];
    dst->u32[2] = src_copy.u16[(encoding >> 0 & 3) | 4] | src_copy.u16[(encoding >> 2 & 3) | 4] << 16;
    dst->u32[3] = src_copy.u16[(encoding >> 4 & 3) | 4] | src_copy.u16[(encoding >> 6 & 3) | 4] << 16;
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
void vec_compare_eqw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (unsigned i = 0; i < array_size(src->u16); i++)
        dst->u16[i] = dst->u16[i] == src->u16[i] ? ~0 : 0;
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

void vec_avg_b128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for(unsigned i = 0; i < 16; i++)
        dst->u8[i] = (1 + dst->u8[i] + src->u8[i]) >> 1;
}
void vec_avg_w128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for(unsigned i = 0; i < 8; i++)
        dst->u16[i] = (1 + dst->u16[i] + src->u16[i]) >> 1;
}

void vec_mull128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 0; i < 8; i++) {
        dst->u16[i] = (uint16_t)(dst->u16[i] * src->u16[i]);
    }
}
void vec_mull64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    union vec { uint64_t qw; uint16_t u16[4]; };
    union vec s = { .qw = src->qw };
    union vec d = { .qw = dst->qw };
    for (int i = 0; i < 4; i++) {
        d.u16[i] = (uint16_t)(d.u16[i] * s.u16[i]);
    }
    dst->qw = d.qw;
}

void vec_mulu128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 0; i < 8; i++) {
        uint32_t res = ((int16_t)dst->u16[i] * (int16_t)src->u16[i]);
        dst->u16[i] = ((res >> 16) & 0xffff);
    }
}
void vec_mulu64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    union vec { uint64_t qw; uint16_t u16[4]; };
    union vec s = { .qw = src->qw };
    union vec d = { .qw = dst->qw };
    for (int i = 0; i < 4; i++) {
        uint32_t res = ((int16_t)d.u16[i] * (int16_t)s.u16[i]);
        d.u16[i] = ((res >> 16) & 0xffff);
    }
    dst->qw = d.qw;
}

void vec_muluu128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst) {
    for (int i = 0; i < 8; i++) {
        uint32_t res = dst->u16[i] * src->u16[i];
        dst->u16[i] = ((res >> 16) & 0xffff);
    }
}
