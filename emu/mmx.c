//
//  mmx.c
//  libISH_emu
//
//  Created by Jason Conway on 02/02/23.
//

#include <math.h>
#include <string.h>

#include "emu/vec.h"
#include "emu/cpu.h"

union vec {
    uint64_t qw;
    uint8_t u8[8];
    uint16_t u16[4];
    uint32_t u32[2];
    uint64_t u64[1];
};

#define VEC_MMX_OP(name, suffix, op, size) \
    void vec_##name##_##suffix##64(NO_CPU, const union mm_reg *src, union mm_reg *dst) { \
        union vec s = { .qw = src->qw }, d = { .qw = dst->qw }; \
        for (unsigned i = 0; i < array_size(s.u##size); i++) \
            d.u##size[i] op##= s.u##size[i]; \
        dst->qw = d.qw; \
    }

#define _VEC_MMX_CMP(sgn, usgn, suffix, relop, size) \
    void vec_compare##sgn##_##suffix##64(NO_CPU, const union mm_reg *src, union mm_reg *dst) { \
        union vec s = { .qw = src->qw }, d = { .qw = dst->qw }; \
        for (unsigned i = 0; i < array_size(s.u##size); i++) \
            d.u##size[i] = (usgn##int##size##_t)d.u##size[i] relop (usgn##int##size##_t)s.u##size[i] ? ~0 : 0;\
        dst->qw = d.qw; \
    }

#define _SHIFT(op, size) \
    do { \
        if (unlikely(amount > (size)-1)) { \
            dst->qw = 0; \
        } else { \
            union vec d = { .qw = dst->qw }; \
            for (unsigned i = 0; i < array_size(d.u##size); i++) \
                d.u##size[i] op##= amount; \
            dst->qw = d.qw; \
        } \
    } while (0)

#define VEC_MMX_SHIFT(dir, suffix, op, size) \
    void vec_shift##dir##_##suffix##64(NO_CPU, const union mm_reg *src, union mm_reg *dst) { \
        const uint8_t amount = src->qw; \
        _SHIFT(op, size); \
    } \
    void vec_imm_shift##dir##_##suffix##64(NO_CPU, const uint8_t amount, union mm_reg *dst) { \
        _SHIFT(op, size); \
    }

#define VEC_MMX_CMPD(suffix, relop, size) \
    _VEC_MMX_CMP(, u, suffix, relop, size)
#define VEC_MMX_CMPS(suffix, relop, size) \
    _VEC_MMX_CMP(s,, suffix, relop, size)

VEC_MMX_OP(add, b, +, 8)
VEC_MMX_OP(add, w, +, 16)
VEC_MMX_OP(add, d, +, 32)
VEC_MMX_OP(add, q, +, 64)

VEC_MMX_OP(sub, b, -, 8)
VEC_MMX_OP(sub, w, -, 16)
VEC_MMX_OP(sub, d, -, 32)
VEC_MMX_OP(sub, q, -, 64)

VEC_MMX_OP(and, q, &, 64)
VEC_MMX_OP(or,  q, |, 64)
VEC_MMX_OP(xor, q, ^, 64)

VEC_MMX_CMPD(eqb, ==,  8)
VEC_MMX_CMPD(eqw, ==, 16)
VEC_MMX_CMPD(eqd, ==, 32)

VEC_MMX_CMPS(gtb, >,  8)
VEC_MMX_CMPS(gtw, >, 16)
VEC_MMX_CMPS(gtd, >, 32)

VEC_MMX_SHIFT(r, w, >>, 16)
VEC_MMX_SHIFT(r, d, >>, 32)
VEC_MMX_SHIFT(r, q, >>, 64)

VEC_MMX_SHIFT(l, w, <<, 16)
VEC_MMX_SHIFT(l, d, <<, 32)
VEC_MMX_SHIFT(l, q, <<, 64)

void vec_shiftrs_w64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    union vec d = { .qw = dst->qw };
    const uint8_t amount = src->qw;
    for (unsigned i = 0; i < 4; i++) {
        if (amount > 15)
            d.u16[i] = ((d.u16[i] >> 15) & (uint16_t)1) ? 0xffff : 0;
        else
            d.u16[i] = ((int16_t)(d.u16[i])) >> amount;
    }
    dst->qw = d.qw;
}
void vec_shiftrs_d64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    union vec d = { .qw = dst->qw };
    const uint8_t amount = src->qw;
    for (unsigned i = 0; i < 2; i++) {
        if (amount > 31)
            d.u32[i] = ((d.u32[i] >> 31) & (uint32_t)1) ? 0xffffffff : 0;
        else
            d.u32[i] = ((int32_t)(d.u32[i])) >> amount;
    }
    dst->qw = d.qw;
}
void vec_imm_shiftrs_w64(NO_CPU, const uint8_t amount, union mm_reg *dst) {
    union vec d = { .qw = dst->qw };
    for (unsigned i = 0; i < 4; i++) {
        if (amount > 15)
            d.u16[i] = ((d.u16[i] >> 15) & (uint16_t)1) ? 0xffff : 0;
        else
            d.u16[i] = ((int16_t)(d.u16[i])) >> amount;
    }
    dst->qw = d.qw;
}
void vec_imm_shiftrs_d64(NO_CPU, const uint8_t amount, union mm_reg *dst) {
    union vec d = { .qw = dst->qw };
    for (unsigned i = 0; i < 2; i++) {
        if (amount > 31)
            d.u32[i] = ((d.u32[i] >> 31) & (uint32_t)1) ? 0xffffffff : 0;
        else
            d.u32[i] = ((int32_t)(d.u32[i])) >> amount;
    }
    dst->qw = d.qw;
}

void vec_mulu64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    union vec s = { .qw = src->qw }, d = { .qw = dst->qw };
    for (unsigned i = 0; i < 4; i++) {
        uint32_t res = ((int16_t)d.u16[i] * (int16_t)s.u16[i]);
        d.u16[i] = ((res >> 16) & 0xffff);
    }
    dst->qw = d.qw;
}
void vec_mull64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    union vec s = { .qw = src->qw }, d = { .qw = dst->qw };
    for (int i = 0; i < 4; i++) {
        d.u16[i] = (uint16_t)(d.u16[i] * s.u16[i]);
    }
    dst->qw = d.qw;
}
void vec_mulu_dq64(NO_CPU, union mm_reg *src, union mm_reg *dst) {
    dst->qw = (uint64_t) src->dw[0] * dst->dw[0];
}

void vec_unpackl_dq64(NO_CPU, const union mm_reg *src, union mm_reg *dst) {
    dst->dw[1] = src->dw[0];
}

void vec_shuffle_w64(NO_CPU, const union mm_reg *src, union mm_reg *dst, uint8_t encoding) {
    union vec s = { .qw = src->qw }, d = { .qw = dst->qw };
    for (unsigned i = 0; i < 4; i++)
        d.u16[i] = s.u16[(encoding >> (2 * i)) % 4];
    dst->qw = d.qw;
}

void vec_movmask_b64(NO_CPU, const union mm_reg *src, uint32_t *dst) {
    union vec s = { .qw = src->qw };
    *dst = 0;
    for (unsigned i = 0; i < array_size(s.u8); i++) {
        if (s.u8[i] & (1 << 7))
            *dst |= 1 << i;
    }
}

void vec_insert_w64(NO_CPU, const uint32_t *src, union mm_reg *dst, uint8_t index) {
    union vec d = { .qw = dst->qw };
    d.u16[index % 4] = (uint16_t)*src;
    dst->qw = d.qw;
}
