#ifndef EMU_FPU_H
#define EMU_FPU_H
#include "emu/float80.h"

typedef float float32;
typedef double float64;

enum fpu_const {
    fconst_one, fconst_zero,
};

void fpu_pop(struct cpu_state *cpu);
void fpu_xch(struct cpu_state *cpu, int i);

void fpu_st(struct cpu_state *cpu, int i);
void fpu_ist16(struct cpu_state *cpu, int16_t *i);
void fpu_ist32(struct cpu_state *cpu, int32_t *i);
void fpu_ist64(struct cpu_state *cpu, int64_t *i);
void fpu_stm32(struct cpu_state *cpu, float *f);
void fpu_stm64(struct cpu_state *cpu, double *f);
void fpu_stm80(struct cpu_state *cpu, float80 *f);

void fpu_ld(struct cpu_state *cpu, int i);
void fpu_ldc(struct cpu_state *cpu, enum fpu_const c);
void fpu_ild32(struct cpu_state *cpu, int32_t *i);
void fpu_ild64(struct cpu_state *cpu, int64_t *i);
void fpu_ldm32(struct cpu_state *cpu, float *f);
void fpu_ldm64(struct cpu_state *cpu, double *f);
void fpu_ldm80(struct cpu_state *cpu, float80 *f);

void fpu_prem(struct cpu_state *cpu);
void fpu_ucom(struct cpu_state *cpu, int i);

void fpu_add(struct cpu_state *cpu, int srci, int dsti);
void fpu_sub(struct cpu_state *cpu, int srci, int dsti);
void fpu_subr(struct cpu_state *cpu, int srci, int dsti);
void fpu_mul(struct cpu_state *cpu, int srci, int dsti);
void fpu_div(struct cpu_state *cpu, int srci, int dsti);
void fpu_iadd16(struct cpu_state *cpu, int16_t *i);
void fpu_isub16(struct cpu_state *cpu, int16_t *i);
void fpu_isubr16(struct cpu_state *cpu, int16_t *i);
void fpu_imul16(struct cpu_state *cpu, int16_t *i);
void fpu_idiv16(struct cpu_state *cpu, int16_t *i);
void fpu_idivr16(struct cpu_state *cpu, int16_t *i);
void fpu_iadd32(struct cpu_state *cpu, int32_t *i);
void fpu_isub32(struct cpu_state *cpu, int32_t *i);
void fpu_isubr32(struct cpu_state *cpu, int32_t *i);
void fpu_imul32(struct cpu_state *cpu, int32_t *i);
void fpu_idiv32(struct cpu_state *cpu, int32_t *i);
void fpu_idivr32(struct cpu_state *cpu, int32_t *i);
void fpu_addm32(struct cpu_state *cpu, float *f);
void fpu_subm32(struct cpu_state *cpu, float *f);
void fpu_subrm32(struct cpu_state *cpu, float *f);
void fpu_mulm32(struct cpu_state *cpu, float *f);
void fpu_divm32(struct cpu_state *cpu, float *f);
void fpu_divrm32(struct cpu_state *cpu, float *f);
void fpu_addm64(struct cpu_state *cpu, double *f);
void fpu_subm64(struct cpu_state *cpu, double *f);
void fpu_subrm64(struct cpu_state *cpu, double *f);
void fpu_mulm64(struct cpu_state *cpu, double *f);
void fpu_divm64(struct cpu_state *cpu, double *f);
void fpu_divrm64(struct cpu_state *cpu, double *f);

void fpu_stcw16(struct cpu_state *cpu, uint16_t *i);
void fpu_ldcw16(struct cpu_state *cpu, uint16_t *i);

#endif
