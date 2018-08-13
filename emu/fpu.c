// I don't remember if the interpreter was supposed to use this in addition to the jit
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
    switch (c) {
        case fconst_zero:
            fpush(f80_from_int(0)); break;
        case fconst_one:
            fpush(f80_from_int(1)); break;
    }
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
}

void fpu_ucom(struct cpu_state *cpu, int i) {
    cpu->c0 = f80_lt(ST(0), ST(i));
    cpu->c1 = 0;
    cpu->c2 = 0; /* fuck nans */
    cpu->c3 = f80_eq(ST(0), ST(i));
}

void fpu_add(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_add(ST(srci), ST(dsti));
}
void fpu_sub(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_sub(ST(srci), ST(dsti));
}
void fpu_subr(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_sub(ST(dsti), ST(srci));
}
void fpu_mul(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_mul(ST(srci), ST(dsti));
}
void fpu_div(struct cpu_state *cpu, int srci, int dsti) {
    ST(dsti) = f80_div(ST(srci), ST(dsti));
}

void fpu_iadd16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_add(ST(0), f80_from_int(*i));
}
void fpu_isub16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_add(ST(0), f80_from_int(*i));
}
void fpu_isubr16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_add(f80_from_int(*i), ST(0));
}
void fpu_imul16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_mul(ST(0), f80_from_int(*i));
}
void fpu_idiv16(struct cpu_state *cpu, int16_t *i) {
    ST(0) = f80_div(ST(0), f80_from_int(*i));
}

void fpu_iadd32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_add(ST(0), f80_from_int(*i));
}
void fpu_isub32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_add(ST(0), f80_from_int(*i));
}
void fpu_isubr32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_add(f80_from_int(*i), ST(0));
}
void fpu_imul32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_mul(ST(0), f80_from_int(*i));
}
void fpu_idiv32(struct cpu_state *cpu, int32_t *i) {
    ST(0) = f80_div(ST(0), f80_from_int(*i));
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

void fpu_stcw16(struct cpu_state *cpu, uint16_t *i) {
    *i = cpu->fcw;
}
void fpu_ldcw16(struct cpu_state *cpu, uint16_t *i) {
    cpu->fcw = *i;
}
