#ifndef EMU_SSE_H
#define EMU_SSE_H

#include "emu/cpu.h"

#define NO_CPU struct cpu_state *UNUSED(cpu)

// arguments are in src, dst order

void vec_zero128_copy128(NO_CPU, const void *src, void *dst);
void vec_zero128_copy64(NO_CPU, const void *src, void *dst);
void vec_zero128_copy32(NO_CPU, const void *src, void *dst);
void vec_zero64_copy64(NO_CPU, const void *src, void *dst);
void vec_zero64_copy32(NO_CPU, const void *src, void *dst);
void vec_zero32_copy32(NO_CPU, const void *src, void *dst);
// "merge" means don't zero the register before writing to it
void vec_merge32(NO_CPU, const void *src, void *dst);
void vec_merge64(NO_CPU, const void *src, void *dst);
void vec_merge128(NO_CPU, const void *src, void *dst);

void vec_imm_shiftl_q128(NO_CPU, const uint8_t amount, union xmm_reg *dst);
void vec_imm_shiftl_q64(NO_CPU, const uint8_t amount, union mm_reg *dst);
void vec_imm_shiftr_q128(NO_CPU, const uint8_t amount, union xmm_reg *dst);
void vec_imm_shiftr_q64(NO_CPU, const uint8_t amount, union mm_reg *dst);
void vec_imm_shiftl_dq128(NO_CPU, const uint8_t amount, union xmm_reg *dst);
void vec_shiftl_q128(NO_CPU, union xmm_reg *amount, union xmm_reg *dst);
void vec_shiftr_q128(NO_CPU, union xmm_reg *amount, union xmm_reg *dst);
void vec_add_b128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_add_d128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_add_q128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_add_q64(NO_CPU, union mm_reg *src, union mm_reg *dst);
void vec_sub_q128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_mulu_dq128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_mulu_dq64(NO_CPU, union mm_reg *src, union mm_reg *dst);

void vec_and128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_and64(NO_CPU, union mm_reg *src, union mm_reg *dst);
void vec_andn128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_or128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_xor128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);
void vec_xor64(NO_CPU, union mm_reg *src, union mm_reg *dst);

void vec_min_ub128(NO_CPU, union xmm_reg *src, union xmm_reg *dst);

void vec_single_fadd64(NO_CPU, const double *src, double *dst);
void vec_single_fadd32(NO_CPU, const float *src, float *dst);
void vec_single_fmul64(NO_CPU, const double *src, double *dst);
void vec_single_fmul32(NO_CPU, const float *src, float *dst);
void vec_single_fsub64(NO_CPU, const double *src, double *dst);
void vec_single_fsub32(NO_CPU, const float *src, float *dst);
void vec_single_fdiv64(NO_CPU, const double *src, double *dst);
void vec_single_fdiv32(NO_CPU, const float *src, float *dst);
void vec_single_fsqrt64(NO_CPU, const double *src, double *dst);

void vec_single_fmax64(NO_CPU, const double *src, double *dst);
void vec_single_fmin64(NO_CPU, const double *src, double *dst);
void vec_single_ucomi32(struct cpu_state *cpu, const float *src, const float *dst);
void vec_single_ucomi64(struct cpu_state *cpu, const double *src, const double *dst);
void vec_single_fcmp64(NO_CPU, const double *src, union xmm_reg *dst, uint8_t type);

void vec_cvtsi2sd32(NO_CPU, const int32_t *src, double *dst);
void vec_cvttsd2si64(NO_CPU, const double *src, int32_t *dst);
void vec_cvtsd2ss64(NO_CPU, const double *src, float *dst);
void vec_cvtsi2ss32(NO_CPU, const int32_t *src, float *dst);
void vec_cvttss2si32(NO_CPU, const float *src, int32_t *dst);
void vec_cvtss2sd32(NO_CPU, const float *src, double *dst);

// TODO organize
void vec_unpack_bw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst);
void vec_unpack_dq128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst);
void vec_unpack_dq64(NO_CPU, const union mm_reg *src, union mm_reg *dst);
void vec_unpack_qdq128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst);
void vec_shuffle_lw128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst, uint8_t encoding);
void vec_shuffle_d128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst, uint8_t encoding);
void vec_compare_eqb128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst);
void vec_compare_eqd128(NO_CPU, const union xmm_reg *src, union xmm_reg *dst);
void vec_movmask_b128(NO_CPU, const union xmm_reg *src, uint32_t *dst);
void vec_fmovmask_d128(NO_CPU, const union xmm_reg *src, uint32_t *dst);
void vec_extract_w128(NO_CPU, const union xmm_reg *src, uint32_t *dst, uint8_t index);

#endif
