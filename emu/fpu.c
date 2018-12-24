// I don't remember if the interpreter was supposed to use this in addition to the jit
#include <math.h>
#include <string.h>
#include "emu/cpu.h"
#include "emu/float80.h"
#include "emu/fpu.h"

#define ST(i) cpu->fp[cpu->top + i]

static void fpu_push(struct cpu_state *cpu, float80 f) {
    cpu->top--;
    ST(0) = f;
}
#define fpush(f) fpu_push(cpu, f)
void fpu_pop(struct cpu_state *cpu) {
    cpu->top++;
}

void fpu_xch(struct cpu_state *cpu, int i) {
    float80 tmp = ST(0);
    ST(0) = ST(i);
    ST(i) = tmp;
}

// loads

void fpu_ld(struct cpu_state *cpu, int i) {
    fpush(ST(i));
}

void fpu_ldc(struct cpu_state *cpu, enum fpu_const c) {
    fpush(fpu_consts[c]);
}

void fpu_ild16(struct cpu_state *cpu, int16_t *i) {
    fpush(f80_from_int(*i));
}
void fpu_ild32(struct cpu_state *cpu, int32_t *i) {
    fpush(f80_from_int(*i));
}
void fpu_ild64(struct cpu_state *cpu, int64_t *i) {
    fpush(f80_from_int(*i));
}

void fpu_ldm32(struct cpu_state *cpu, float32 *f) {
    fpush(f80_from_double(*f));
}
void fpu_ldm64(struct cpu_state *cpu, float64 *f) {
    fpush(f80_from_double(*f));
}
void fpu_ldm80(struct cpu_state *cpu, float80 *f) {
    fpush(*f);
}

// stores

void fpu_st(struct cpu_state *cpu, int i) {
    ST(i) = ST(0);
}

void fpu_ist16(struct cpu_state *cpu, int16_t *i) {
    *i = (int16_t) f80_to_int(ST(0));
}
void fpu_ist32(struct cpu_state *cpu, int32_t *i) {
    *i = f80_to_int(ST(0));
}
void fpu_ist64(struct cpu_state *cpu, int64_t *i) {
    *i = f80_to_int(ST(0));
}

void fpu_stm32(struct cpu_state *cpu, float32 *f) {
    *f = f80_to_double(ST(0));
}
void fpu_stm64(struct cpu_state *cpu, float64 *f) {
    *f = f80_to_double(ST(0));
}
void fpu_stm80(struct cpu_state *cpu, float80 *f) {
    // intel guarantees this will only write 10 bytes, not 12 or anything weird like that
    memcpy(f, &ST(0), 10);
}

// math

void fpu_prem(struct cpu_state *cpu) {
    ST(0) = f80_mod(ST(0), ST(1));
    cpu->c2 = 0; // say we finished the entire remainder
}

void fpu_scale(struct cpu_state *cpu) {
    enum f80_rounding_mode old_mode = f80_rounding_mode;
    f80_rounding_mode = round_chop;
    int scale = f80_to_int(ST(1));
    f80_rounding_mode = old_mode;
    ST(0) = f80_scale(ST(0), scale);
}

void fpu_rndint(struct cpu_state *cpu) {
    if (f80_isinf(ST(0)) || f80_isnan(ST(0)))
        return;
    ST(0) = f80_from_int(f80_to_int(ST(0)));
}

void fpu_sqrt(struct cpu_state *cpu) {
    ST(0) = f80_sqrt(ST(0));
}

void fpu_yl2x(struct cpu_state *cpu) {
    ST(1) = f80_mul(ST(1), f80_log2(ST(0)));
    fpu_pop(cpu);
}

void fpu_2xm1(struct cpu_state *cpu) {
    // an example of the ancient chinese art of chi ting
    ST(0) = f80_from_double(pow(2, f80_to_double(ST(0))) - 1);
}

static void fpu_comparei(struct cpu_state *cpu, float80 x) {
    cpu->zf_res = cpu->pf_res = 0;
    cpu->zf = cpu->pf = cpu->cf = 0;
    cpu->cf = f80_lt(ST(0), x);
    cpu->zf = f80_eq(ST(0), x);
    if (f80_uncomparable(ST(0), x))
        cpu->zf = cpu->pf = cpu->cf = 1;
}
static void fpu_compare(struct cpu_state *cpu, float80 x) {
    cpu->c2 = cpu->c1 = 0;
    cpu->c0 = f80_lt(ST(0), x);
    cpu->c3 = f80_eq(ST(0), x);
    if (f80_uncomparable(ST(0), x))
        cpu->c0 = cpu->c2 = cpu->c3 = 1;
}
void fpu_com(struct cpu_state *cpu, int i) {
    fpu_compare(cpu, ST(i));
}
void fpu_comi(struct cpu_state *cpu, int i) {
    fpu_comparei(cpu, ST(i));
}
void fpu_comm32(struct cpu_state *cpu, float *f) {
    fpu_compare(cpu, f80_from_double(*f));
}
void fpu_comm64(struct cpu_state *cpu, double *f) {
    fpu_compare(cpu, f80_from_double(*f));
}
void fpu_tst(struct cpu_state *cpu) {
    fpu_compare(cpu, fpu_consts[fconst_zero]);
}

void fpu_abs(struct cpu_state *cpu) {
    ST(0) = f80_abs(ST(0));
}

void fpu_chs(struct cpu_state *cpu) {
    ST(0) = f80_neg(ST(0));
}

void fpu_add(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_add(ST(dsti), ST(srci));
}
void fpu_sub(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_sub(ST(dsti), ST(srci));
}
void fpu_subr(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_sub(ST(srci), ST(dsti));
}
void fpu_mul(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_mul(ST(dsti), ST(srci));
}
void fpu_div(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_div(ST(dsti), ST(srci));
}
void fpu_divr(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_div(ST(srci), ST(dsti));
}

void fpu_iadd16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_add(ST(0), f80_from_int(*i));
}
void fpu_isub16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_sub(ST(0), f80_from_int(*i));
}
void fpu_isubr16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_sub(f80_from_int(*i), ST(0));
}
void fpu_imul16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_mul(ST(0), f80_from_int(*i));
}
void fpu_idiv16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_div(ST(0), f80_from_int(*i));
}
void fpu_idivr16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_div(f80_from_int(*i), ST(0));
}

void fpu_iadd32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_add(ST(0), f80_from_int(*i));
}
void fpu_isub32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_sub(ST(0), f80_from_int(*i));
}
void fpu_isubr32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_sub(f80_from_int(*i), ST(0));
}
void fpu_imul32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_mul(ST(0), f80_from_int(*i));
}
void fpu_idiv32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_div(ST(0), f80_from_int(*i));
}
void fpu_idivr32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_div(f80_from_int(*i), ST(0));
}

void fpu_addm32(struct cpu_state *cpu, float32 *f) {
    ST(0) = f80_add(ST(0), f80_from_double(*f));
}
void fpu_subm32(struct cpu_state *cpu, float32 *f) {
    ST(0) = f80_sub(ST(0), f80_from_double(*f));
}
void fpu_subrm32(struct cpu_state *cpu, float32 *f) {
    ST(0) = f80_sub(f80_from_double(*f), ST(0));
}
void fpu_mulm32(struct cpu_state *cpu, float32 *f) {
    ST(0) = f80_mul(ST(0), f80_from_double(*f));
}
void fpu_divm32(struct cpu_state *cpu, float32 *f) {
    ST(0) = f80_div(ST(0), f80_from_double(*f));
}
void fpu_divrm32(struct cpu_state *cpu, float32 *f) {
    ST(0) = f80_div(f80_from_double(*f), ST(0));
}

void fpu_addm64(struct cpu_state *cpu, float64 *f) {
    ST(0) = f80_add(ST(0), f80_from_double(*f));
}
void fpu_subm64(struct cpu_state *cpu, float64 *f) {
    ST(0) = f80_sub(ST(0), f80_from_double(*f));
}
void fpu_subrm64(struct cpu_state *cpu, float64 *f) {
    ST(0) = f80_sub(f80_from_double(*f), ST(0));
}
void fpu_mulm64(struct cpu_state *cpu, float64 *f) {
    ST(0) = f80_mul(ST(0), f80_from_double(*f));
}
void fpu_divm64(struct cpu_state *cpu, float64 *f) {
    ST(0) = f80_div(ST(0), f80_from_double(*f));
}
void fpu_divrm64(struct cpu_state *cpu, float64 *f) {
    ST(0) = f80_div(f80_from_double(*f), ST(0));
}

void fpu_stcw16(struct cpu_state *cpu, uint16_t *i) {
    *i = cpu->fcw;
}
void fpu_ldcw16(struct cpu_state *cpu, uint16_t *i) {
    cpu->fcw = *i;
    f80_rounding_mode = cpu->rc;
}

void fpu_patan(struct cpu_state *cpu) {
    // there's no native atan2 for 80-bit float yet.
    ST(1) = f80_from_double(atan2(f80_to_double(ST(1)), f80_to_double(ST(0))));
    fpu_pop(cpu);
}

void fpu_xam(struct cpu_state *cpu) {
    float80 f = ST(0);
    int outflags = 0;
    if (!f80_is_supported(f)) {
        outflags = 0b000;
    } else if (f80_isnan(f)) {
        outflags = 0b001;
    } else if (f80_isinf(f)) {
        outflags = 0b011;
    } else if (f80_iszero(f)) {
        outflags = 0b100;
    } else if (f80_isdenormal(f)) {
        outflags = 0b110;
    } else {
        // normal.
        // todo: empty
        outflags = 0b010;
    }
    cpu->c1 = f.sign;
    cpu->c0 = outflags & 1;
    cpu->c2 = (outflags >> 1) & 1;
    cpu->c3 = (outflags >> 2) & 1;
}
