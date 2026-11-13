// Specialized handlers for interp64.c (included there).
//
// Every decoded instruction carries a handler pointer. A handler does its
// work and tail-calls the handler of the next instruction in the block, so a
// block runs without returning to the dispatch loop (threaded code, the same
// idea as asbestos's gadgets, but in C). Instructions without a specialized
// handler go through h_generic, which calls the big switch in exec().
// A handler that leaves the block (taken branch, interrupt) returns.

#if defined(__clang__)
#define MUSTTAIL __attribute__((musttail))
#else
#define MUSTTAIL
#endif

#define HANDLER(name) __attribute__((unused)) static int name(struct ictx *c, const struct insn *in, struct cpu_state *cpu)
#define NEXT do { \
    cpu->rip += in->len; \
    in++; \
    MUSTTAIL return in->fn(c, in, cpu); \
} while (0)

HANDLER(h_end) {
    (void) c; (void) in; (void) cpu;
    return INT_NONE;
}

HANDLER(h_generic) {
    if (unlikely(stats_on))
        stats_generic[in->op]++;
    uint64_t seq = cpu->rip + in->len;
    int r = exec(c, in);
    if (unlikely(r != INT_NONE))
        return r;
    if (cpu->rip != seq)
        return INT_NONE;
    in++;
    MUSTTAIL return in->fn(c, in, cpu);
}

// effective address when no address-size prefix is present (a segment base,
// if any, was folded into base or index by the decoder)
static forceinline uint64_t ea_fast(struct cpu_state *cpu, const struct insn *in) {
    const uint64_t *R = (const uint64_t *) ((const char *) cpu + offsetof(struct cpu_state, regs));
    return (uint64_t) in->disp + R[in->base] + (R[in->index] << in->scale);
}

// x87: straight to its own executor instead of through exec()'s switch
HANDLER(h_x87) {
    int r = exec_x87(c, in);
    if (unlikely(r != INT_NONE))
        return r;
    NEXT;
}

HANDLER(h_nop) {
    (void) c;
    NEXT;
}

// ---------------------------------------------------------------- mov, lea

#define DEF_MOV(SZ) \
HANDLER(h_mov_rm_r##SZ) { /* 89 reg form: rm <- reg */ \
    (void) c; \
    rset(cpu, in, in->rm, SZ, rget(cpu, in, in->reg, SZ)); \
    NEXT; \
} \
HANDLER(h_mov_r_rm##SZ) { /* 8B reg form: reg <- rm */ \
    (void) c; \
    rset(cpu, in, in->reg, SZ, rget(cpu, in, in->rm, SZ)); \
    NEXT; \
} \
HANDLER(h_mov_r_m##SZ) { \
    rset(cpu, in, in->reg, SZ, mrd(c, ea_fast(cpu, in), SZ)); \
    NEXT; \
} \
HANDLER(h_mov_m_r##SZ) { \
    mwr(c, ea_fast(cpu, in), SZ, rget(cpu, in, in->reg, SZ)); \
    NEXT; \
} \
HANDLER(h_mov_m_i##SZ) { \
    mwr(c, ea_fast(cpu, in), SZ, (uint64_t) in->imm); \
    NEXT; \
} \
HANDLER(h_mov_r_i##SZ) { /* B0-BF, C6/C7 reg form: register in rm */ \
    (void) c; \
    rset(cpu, in, in->rm, SZ, (uint64_t) in->imm); \
    NEXT; \
}
DEF_MOV(1)
DEF_MOV(2)
DEF_MOV(4)
DEF_MOV(8)

HANDLER(h_lea4) {
    (void) c;
    cpu->regs[in->reg] = (uint32_t) ea_fast(cpu, in);
    NEXT;
}
HANDLER(h_lea8) {
    (void) c;
    cpu->regs[in->reg] = ea_fast(cpu, in);
    NEXT;
}

// movzx / movsx / movsxd: destination is always written as 32 or 64 bits
#define DEF_EXT(name, SSZ, CAST) \
HANDLER(h_##name##_r4) { (void) c; cpu->regs[in->reg] = (uint32_t) (CAST) rget(cpu, in, in->rm, SSZ); NEXT; } \
HANDLER(h_##name##_r8) { (void) c; cpu->regs[in->reg] = (uint64_t) (int64_t) (CAST) rget(cpu, in, in->rm, SSZ); NEXT; } \
HANDLER(h_##name##_m4) { cpu->regs[in->reg] = (uint32_t) (CAST) mrd(c, ea_fast(cpu, in), SSZ); NEXT; } \
HANDLER(h_##name##_m8) { cpu->regs[in->reg] = (uint64_t) (int64_t) (CAST) mrd(c, ea_fast(cpu, in), SSZ); NEXT; }
DEF_EXT(movzx8, 1, uint8_t)
DEF_EXT(movzx16, 2, uint16_t)
DEF_EXT(movsx8, 1, int8_t)
DEF_EXT(movsx16, 2, int16_t)
DEF_EXT(movsx32, 4, int32_t)

// ---------------------------------------------------------------- arithmetic

#define ALU_TEST 8
#define ALU_STORES(OP) ((OP) != ALU_CMP && (OP) != ALU_TEST)
#define ALU_REAL(OP) ((OP) == ALU_TEST ? ALU_AND : (OP))

#define DEF_ALU(name, OP, SZ) \
HANDLER(h_##name##_rr##SZ) { /* rm op= reg */ \
    uint64_t r = alu(c, ALU_REAL(OP), SZ, rget(cpu, in, in->rm, SZ), rget(cpu, in, in->reg, SZ), 0); \
    if (ALU_STORES(OP)) rset(cpu, in, in->rm, SZ, r); \
    NEXT; \
} \
HANDLER(h_##name##_gr##SZ) { /* reg op= rm */ \
    uint64_t r = alu(c, ALU_REAL(OP), SZ, rget(cpu, in, in->reg, SZ), rget(cpu, in, in->rm, SZ), 0); \
    if (ALU_STORES(OP)) rset(cpu, in, in->reg, SZ, r); \
    NEXT; \
} \
HANDLER(h_##name##_gm##SZ) { /* reg op= [mem] */ \
    uint64_t b = mrd(c, ea_fast(cpu, in), SZ); \
    uint64_t r = alu(c, ALU_REAL(OP), SZ, rget(cpu, in, in->reg, SZ), b, 0); \
    if (ALU_STORES(OP)) rset(cpu, in, in->reg, SZ, r); \
    NEXT; \
} \
HANDLER(h_##name##_mg##SZ) { /* [mem] op= reg */ \
    uint64_t addr = ea_fast(cpu, in), b = rget(cpu, in, in->reg, SZ); \
    if (!ALU_STORES(OP)) { \
        alu(c, ALU_REAL(OP), SZ, mrd(c, addr, SZ), b, 0); \
    } else { \
        void *p = wptr(c, addr, SZ); \
        if (likely(p != NULL)) \
            st(p, SZ, alu(c, ALU_REAL(OP), SZ, ld(p, SZ), b, 0)); \
        else \
            mwr(c, addr, SZ, alu(c, ALU_REAL(OP), SZ, mrd(c, addr, SZ), b, 0)); \
    } \
    NEXT; \
} \
HANDLER(h_##name##_ri##SZ) { /* rm op= imm */ \
    uint64_t r = alu(c, ALU_REAL(OP), SZ, rget(cpu, in, in->rm, SZ), (uint64_t) in->imm, 0); \
    if (ALU_STORES(OP)) rset(cpu, in, in->rm, SZ, r); \
    NEXT; \
} \
HANDLER(h_##name##_mi##SZ) { /* [mem] op= imm */ \
    uint64_t addr = ea_fast(cpu, in), b = (uint64_t) in->imm; \
    if (!ALU_STORES(OP)) { \
        alu(c, ALU_REAL(OP), SZ, mrd(c, addr, SZ), b, 0); \
    } else { \
        void *p = wptr(c, addr, SZ); \
        if (likely(p != NULL)) \
            st(p, SZ, alu(c, ALU_REAL(OP), SZ, ld(p, SZ), b, 0)); \
        else \
            mwr(c, addr, SZ, alu(c, ALU_REAL(OP), SZ, mrd(c, addr, SZ), b, 0)); \
    } \
    NEXT; \
}

#define DEF_ALU_SIZES(name, OP) DEF_ALU(name, OP, 1) DEF_ALU(name, OP, 4) DEF_ALU(name, OP, 8)
DEF_ALU_SIZES(add, ALU_ADD)
DEF_ALU_SIZES(or, ALU_OR)
DEF_ALU_SIZES(and, ALU_AND)
DEF_ALU_SIZES(sub, ALU_SUB)
DEF_ALU_SIZES(xor, ALU_XOR)
DEF_ALU_SIZES(cmp, ALU_CMP)
DEF_ALU_SIZES(test, ALU_TEST)

enum { F_RR, F_GR, F_GM, F_MG, F_RI, F_MI, F_COUNT };
#define ALU_ROW(name, SZ) {h_##name##_rr##SZ, h_##name##_gr##SZ, h_##name##_gm##SZ, h_##name##_mg##SZ, h_##name##_ri##SZ, h_##name##_mi##SZ}
#define ALU_ROWS(name) {ALU_ROW(name, 1), ALU_ROW(name, 4), ALU_ROW(name, 8)}
typedef insn_fn handler_t;
static const handler_t alu_handlers[9][3][F_COUNT] = {
    [ALU_ADD] = ALU_ROWS(add),
    [ALU_OR] = ALU_ROWS(or),
    [ALU_AND] = ALU_ROWS(and),
    [ALU_SUB] = ALU_ROWS(sub),
    [ALU_XOR] = ALU_ROWS(xor),
    [ALU_CMP] = ALU_ROWS(cmp),
    [ALU_TEST] = ALU_ROWS(test),
};

#define DEF_INCDEC(SZ) \
HANDLER(h_inc_r##SZ) { rset(cpu, in, in->rm, SZ, incdec(c, false, SZ, rget(cpu, in, in->rm, SZ))); NEXT; } \
HANDLER(h_dec_r##SZ) { rset(cpu, in, in->rm, SZ, incdec(c, true, SZ, rget(cpu, in, in->rm, SZ))); NEXT; }
DEF_INCDEC(4)
DEF_INCDEC(8)

// shifts by a constant, register operand
#define DEF_SHIFT(SZ) \
HANDLER(h_shl_ri##SZ) { rset(cpu, in, in->rm, SZ, shift_op(c, 4, SZ, rget(cpu, in, in->rm, SZ), (unsigned) in->imm)); NEXT; } \
HANDLER(h_shr_ri##SZ) { rset(cpu, in, in->rm, SZ, shift_op(c, 5, SZ, rget(cpu, in, in->rm, SZ), (unsigned) in->imm)); NEXT; } \
HANDLER(h_sar_ri##SZ) { rset(cpu, in, in->rm, SZ, shift_op(c, 7, SZ, rget(cpu, in, in->rm, SZ), (unsigned) in->imm)); NEXT; } \
HANDLER(h_rol_ri##SZ) { rset(cpu, in, in->rm, SZ, shift_op(c, 0, SZ, rget(cpu, in, in->rm, SZ), (unsigned) in->imm)); NEXT; } \
HANDLER(h_ror_ri##SZ) { rset(cpu, in, in->rm, SZ, shift_op(c, 1, SZ, rget(cpu, in, in->rm, SZ), (unsigned) in->imm)); NEXT; }
DEF_SHIFT(4)
DEF_SHIFT(8)

#define DEF_IMUL(SZ) \
HANDLER(h_imul_gr##SZ) { rset(cpu, in, in->reg, SZ, imul_op(c, SZ, rget(cpu, in, in->reg, SZ), rget(cpu, in, in->rm, SZ))); NEXT; } \
HANDLER(h_imul_gm##SZ) { \
    uint64_t b = mrd(c, ea_fast(cpu, in), SZ); \
    rset(cpu, in, in->reg, SZ, imul_op(c, SZ, rget(cpu, in, in->reg, SZ), b)); \
    NEXT; \
} \
HANDLER(h_imul_gri##SZ) { rset(cpu, in, in->reg, SZ, imul_op(c, SZ, rget(cpu, in, in->rm, SZ), (uint64_t) in->imm)); NEXT; }
DEF_IMUL(4)
DEF_IMUL(8)

// ---------------------------------------------------------------- conditions

#define DEF_CC(cc) \
HANDLER(h_jcc##cc) { \
    if (cond(c, cc)) { \
        cpu->rip += (uint64_t) in->len + (uint64_t) in->imm; \
        return INT_NONE; \
    } \
    NEXT; \
} \
HANDLER(h_cmov##cc##_r4) { \
    if (cond(c, cc)) cpu->regs[in->reg] = (uint32_t) cpu->regs[in->rm]; \
    else cpu->regs[in->reg] = (uint32_t) cpu->regs[in->reg]; \
    NEXT; \
} \
HANDLER(h_cmov##cc##_r8) { \
    if (cond(c, cc)) cpu->regs[in->reg] = cpu->regs[in->rm]; \
    NEXT; \
} \
HANDLER(h_cmov##cc##_m4) { \
    uint64_t v = mrd(c, ea_fast(cpu, in), 4); \
    cpu->regs[in->reg] = cond(c, cc) ? v : (uint32_t) cpu->regs[in->reg]; \
    NEXT; \
} \
HANDLER(h_cmov##cc##_m8) { \
    uint64_t v = mrd(c, ea_fast(cpu, in), 8); \
    if (cond(c, cc)) cpu->regs[in->reg] = v; \
    NEXT; \
} \
HANDLER(h_set##cc##_r) { \
    *r8p(cpu, in, in->rm) = cond(c, cc); \
    NEXT; \
}
DEF_CC(0) DEF_CC(1) DEF_CC(2) DEF_CC(3) DEF_CC(4) DEF_CC(5) DEF_CC(6) DEF_CC(7)
DEF_CC(8) DEF_CC(9) DEF_CC(10) DEF_CC(11) DEF_CC(12) DEF_CC(13) DEF_CC(14) DEF_CC(15)

#define CC_ROW(prefix, suffix) { \
    prefix##0##suffix, prefix##1##suffix, prefix##2##suffix, prefix##3##suffix, \
    prefix##4##suffix, prefix##5##suffix, prefix##6##suffix, prefix##7##suffix, \
    prefix##8##suffix, prefix##9##suffix, prefix##10##suffix, prefix##11##suffix, \
    prefix##12##suffix, prefix##13##suffix, prefix##14##suffix, prefix##15##suffix}
static const handler_t jcc_handlers[16] = CC_ROW(h_jcc, );
static const handler_t cmov_handlers[4][16] = {
    CC_ROW(h_cmov, _r4), CC_ROW(h_cmov, _r8), CC_ROW(h_cmov, _m4), CC_ROW(h_cmov, _m8),
};
static const handler_t setcc_handlers[16] = CC_ROW(h_set, _r);

// ---------------------------------------------------------------- control flow, stack

HANDLER(h_jmp) {
    (void) c;
    cpu->rip += (uint64_t) in->len + (uint64_t) in->imm;
    return INT_NONE;
}
HANDLER(h_call) {
    uint64_t ret = cpu->rip + in->len;
    push64(c, ret);
    cpu->rip = ret + (uint64_t) in->imm;
    return INT_NONE;
}
HANDLER(h_ret) {
    (void) in;
    uint64_t target = mrd(c, cpu->rsp, 8);
    cpu->rsp += 8;
    cpu->rip = target;
    return INT_NONE;
}
HANDLER(h_jmp_r) {
    (void) c;
    cpu->rip = cpu->regs[in->rm];
    return INT_NONE;
}
HANDLER(h_jmp_m) {
    cpu->rip = mrd(c, ea_fast(cpu, in), 8);
    return INT_NONE;
}
HANDLER(h_call_r) {
    uint64_t target = cpu->regs[in->rm];
    push64(c, cpu->rip + in->len);
    cpu->rip = target;
    return INT_NONE;
}
HANDLER(h_call_m) {
    uint64_t target = mrd(c, ea_fast(cpu, in), 8);
    push64(c, cpu->rip + in->len);
    cpu->rip = target;
    return INT_NONE;
}
HANDLER(h_push_r) {
    push64(c, cpu->regs[in->reg]);
    NEXT;
}
HANDLER(h_pop_r) {
    uint64_t v = mrd(c, cpu->rsp, 8);
    cpu->rsp += 8;
    cpu->regs[in->reg] = v;
    NEXT;
}
HANDLER(h_push_i) {
    push64(c, (uint64_t) in->imm);
    NEXT;
}
HANDLER(h_leave) {
    uint64_t v = mrd(c, cpu->rbp, 8);
    cpu->rsp = cpu->rbp + 8;
    cpu->rbp = v;
    NEXT;
}
HANDLER(h_cdqe) {
    (void) c;
    cpu->rax = (uint64_t) (int64_t) (int32_t) cpu->eax;
    NEXT;
}
HANDLER(h_cdq) {
    (void) c;
    cpu->rdx = (int32_t) cpu->eax < 0 ? 0xffffffffu : 0;
    NEXT;
}
HANDLER(h_cqo) {
    (void) c;
    cpu->rdx = (int64_t) cpu->rax < 0 ? ~0ULL : 0;
    NEXT;
}

// ---------------------------------------------------------------- common SSE moves

HANDLER(h_xmov_x_x) { // any full-register xmm <- xmm move
    (void) c;
    cpu->xmm[in->reg] = cpu->xmm[in->rm];
    NEXT;
}
HANDLER(h_xmov_x_m16) {
    uint64_t addr = ea_fast(cpu, in);
    union xmm_reg t;
    if (likely((addr & (PAGE_SIZE - 1)) <= PAGE_SIZE - 16))
        memcpy(&t, xlat_r(c, addr), 16);
    else
        mread_n(c, addr, &t, 16);
    cpu->xmm[in->reg] = t;
    NEXT;
}
HANDLER(h_xmov_m_x16) {
    uint64_t addr = ea_fast(cpu, in);
    if (likely((addr & (PAGE_SIZE - 1)) <= PAGE_SIZE - 16 && !c->log))
        memcpy(xlat_w(c, addr), &cpu->xmm[in->reg], 16);
    else
        mwrite_n(c, addr, &cpu->xmm[in->reg], 16);
    NEXT;
}
HANDLER(h_xmov_x_m8) { // movsd/movq load: zero the upper half
    uint64_t v = mrd(c, ea_fast(cpu, in), 8);
    cpu->xmm[in->reg].qw[0] = v;
    cpu->xmm[in->reg].qw[1] = 0;
    NEXT;
}
HANDLER(h_xmov_m_x8) {
    mwr(c, ea_fast(cpu, in), 8, cpu->xmm[in->reg].qw[0]);
    NEXT;
}
HANDLER(h_xmov_x_m4) {
    uint64_t v = mrd(c, ea_fast(cpu, in), 4);
    cpu->xmm[in->reg].qw[0] = v;
    cpu->xmm[in->reg].qw[1] = 0;
    NEXT;
}
HANDLER(h_xmov_m_x4) {
    mwr(c, ea_fast(cpu, in), 4, cpu->xmm[in->reg].u32[0]);
    NEXT;
}
HANDLER(h_pxor_x_x) {
    (void) c;
    cpu->xmm[in->reg].qw[0] ^= cpu->xmm[in->rm].qw[0];
    cpu->xmm[in->reg].qw[1] ^= cpu->xmm[in->rm].qw[1];
    NEXT;
}

// ---------------------------------------------------------------- handler selection

static handler_t pick_handler(struct insn *in) {
    unsigned op = in->op;
    bool mem = in->mod != 3;
    int sz = in->opsz;
    // handlers use ea_fast and non-atomic memory updates
    if (in->pfx & (P_ADDR32 | P_LOCK | P_FS | P_GS))
        return h_generic;
    int szi = sz == 8 ? 2 : sz == 4 ? 1 : -1;
    int pfx_simd = in->pfx & (P_66 | P_F2 | P_F3);

    if (op < 0x40 && (op & 7) < 6) {
        int alu = (op >> 3) & 7, form = op & 7;
        if (alu == ALU_ADC || alu == ALU_SBB)
            return h_generic;
        int si = (form & 1) ? szi : 0;
        if (si < 0)
            return h_generic;
        switch (form >> 1) {
            case 0: return alu_handlers[alu][si][mem ? F_MG : F_RR];
            case 1: return alu_handlers[alu][si][mem ? F_GM : F_GR];
            default: return alu_handlers[alu][si][F_RI]; // accumulator: rm is 0
        }
    }
    if (op >= 0xd8 && op <= 0xdf)
        return h_x87;
    switch (op) {
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            return h_push_r;
        case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            return h_pop_r;
        case 0x63:
            if (sz == 8) return mem ? h_movsx32_m8 : h_movsx32_r8;
            break;
        case 0x68: case 0x6a:
            return h_push_i;
        case 0x69: case 0x6b:
            if (!mem && szi >= 0) return sz == 4 ? h_imul_gri4 : h_imul_gri8;
            break;
        case 0x80: case 0x81: case 0x83: {
            int alu = in->reg & 7;
            int si = op == 0x80 ? 0 : szi;
            if (alu == ALU_ADC || alu == ALU_SBB || si < 0)
                break;
            return alu_handlers[alu][si][mem ? F_MI : F_RI];
        }
        case 0x84: case 0x85: {
            int si = op == 0x84 ? 0 : szi;
            if (si < 0) break;
            return alu_handlers[ALU_TEST][si][mem ? F_MG : F_RR];
        }
        case 0xa8: case 0xa9: {
            int si = op == 0xa8 ? 0 : szi;
            if (si < 0) break;
            return alu_handlers[ALU_TEST][si][F_RI];
        }
        case 0xf6: case 0xf7: {
            int si = op == 0xf6 ? 0 : szi;
            if ((in->reg & 7) > 1 || si < 0) break;
            return alu_handlers[ALU_TEST][si][mem ? F_MI : F_RI];
        }
        case 0x88:
            return mem ? h_mov_m_r1 : h_mov_rm_r1;
        case 0x89:
            if (sz == 8) return mem ? h_mov_m_r8 : h_mov_rm_r8;
            if (sz == 4) return mem ? h_mov_m_r4 : h_mov_rm_r4;
            return mem ? h_mov_m_r2 : h_mov_rm_r2;
        case 0x8a:
            return mem ? h_mov_r_m1 : h_mov_r_rm1;
        case 0x8b:
            if (sz == 8) return mem ? h_mov_r_m8 : h_mov_r_rm8;
            if (sz == 4) return mem ? h_mov_r_m4 : h_mov_r_rm4;
            return mem ? h_mov_r_m2 : h_mov_r_rm2;
        case 0x8d:
            if (!mem) break;
            if (sz == 8) return h_lea8;
            if (sz == 4) return h_lea4;
            break;
        case 0x90:
            if (in->reg == 0) return h_nop;
            break;
        case 0x98:
            if (sz == 8) return h_cdqe;
            break;
        case 0x99:
            if (sz == 8) return h_cqo;
            if (sz == 4) return h_cdq;
            break;
        case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
            in->rm = in->reg;
            return h_mov_r_i1;
        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
            in->rm = in->reg;
            return sz == 8 ? h_mov_r_i8 : sz == 4 ? h_mov_r_i4 : h_mov_r_i2;
        case 0xc6:
            if ((in->reg & 7) != 0) break;
            return mem ? h_mov_m_i1 : h_mov_r_i1;
        case 0xc7:
            if ((in->reg & 7) != 0) break;
            if (sz == 8) return mem ? h_mov_m_i8 : h_mov_r_i8;
            if (sz == 4) return mem ? h_mov_m_i4 : h_mov_r_i4;
            return mem ? h_mov_m_i2 : h_mov_r_i2;
        case 0xc1: case 0xd1:
            if (mem || szi < 0) break;
            if (op == 0xd1) in->imm = 1;
            switch (in->reg & 7) {
                case 0: return sz == 4 ? h_rol_ri4 : h_rol_ri8;
                case 1: return sz == 4 ? h_ror_ri4 : h_ror_ri8;
                case 4: case 6: return sz == 4 ? h_shl_ri4 : h_shl_ri8;
                case 5: return sz == 4 ? h_shr_ri4 : h_shr_ri8;
                case 7: return sz == 4 ? h_sar_ri4 : h_sar_ri8;
            }
            break;
        case 0xc3: return h_ret;
        case 0xc9: return h_leave;
        case 0xe8: return h_call;
        case 0xe9: case 0xeb: return h_jmp;
        case 0xff:
            switch (in->reg & 7) {
                case 0: if (!mem && szi >= 0) return sz == 4 ? h_inc_r4 : h_inc_r8; break;
                case 1: if (!mem && szi >= 0) return sz == 4 ? h_dec_r4 : h_dec_r8; break;
                case 2: return mem ? h_call_m : h_call_r;
                case 4: return mem ? h_jmp_m : h_jmp_r;
            }
            break;
        case 0x10d: case 0x118: case 0x119: case 0x11a: case 0x11b: case 0x11c: case 0x11d: case 0x11e: case 0x11f:
            return h_nop;
        case 0x1af:
            if (szi < 0) break;
            if (sz == 4) return mem ? h_imul_gm4 : h_imul_gr4;
            return mem ? h_imul_gm8 : h_imul_gr8;
        case 0x1b6:
            if (szi < 0) break;
            return mem ? (sz == 4 ? h_movzx8_m4 : h_movzx8_m8) : (sz == 4 ? h_movzx8_r4 : h_movzx8_r8);
        case 0x1b7:
            if (szi < 0) break;
            return mem ? (sz == 4 ? h_movzx16_m4 : h_movzx16_m8) : (sz == 4 ? h_movzx16_r4 : h_movzx16_r8);
        case 0x1be:
            if (szi < 0) break;
            return mem ? (sz == 4 ? h_movsx8_m4 : h_movsx8_m8) : (sz == 4 ? h_movsx8_r4 : h_movsx8_r8);
        case 0x1bf:
            if (szi < 0) break;
            return mem ? (sz == 4 ? h_movsx16_m4 : h_movsx16_m8) : (sz == 4 ? h_movsx16_r4 : h_movsx16_r8);

        // SSE moves
        case 0x128: // movaps/movapd
            if (pfx_simd & (P_F2 | P_F3)) break;
            return mem ? h_xmov_x_m16 : h_xmov_x_x;
        case 0x129:
            if (pfx_simd & (P_F2 | P_F3)) break;
            if (mem) return h_xmov_m_x16;
            break;
        case 0x110: // movups/movupd/movss/movsd load
            if (!mem) break;
            if (pfx_simd & P_F2) return h_xmov_x_m8;
            if (pfx_simd & P_F3) return h_xmov_x_m4;
            return h_xmov_x_m16;
        case 0x111:
            if (!mem) break;
            if (pfx_simd & P_F2) return h_xmov_m_x8;
            if (pfx_simd & P_F3) return h_xmov_m_x4;
            return h_xmov_m_x16;
        case 0x16f: // movdqa/movdqu
            if (!(pfx_simd & (P_66 | P_F3)) || (pfx_simd & P_F2)) break;
            return mem ? h_xmov_x_m16 : h_xmov_x_x;
        case 0x17f:
            if (!(pfx_simd & (P_66 | P_F3)) || (pfx_simd & P_F2)) break;
            if (mem) return h_xmov_m_x16;
            break;
        case 0x17e:
            if (pfx_simd == P_F3 && mem) return h_xmov_x_m8;
            break;
        case 0x1d6:
            if (pfx_simd == P_66 && mem) return h_xmov_m_x8;
            break;
        case 0x1ef: case 0x157:
            if (op == 0x1ef && pfx_simd != P_66) break;
            if (op == 0x157 && (pfx_simd & (P_F2 | P_F3))) break;
            if (!mem) return h_pxor_x_x;
            break;
    }
    if (op >= 0x70 && op < 0x80)
        return jcc_handlers[op & 0xf];
    if (op >= 0x180 && op < 0x190)
        return jcc_handlers[op & 0xf];
    if (op >= 0x140 && op < 0x150 && szi >= 0)
        return cmov_handlers[(mem ? 2 : 0) + (sz == 8)][op & 0xf];
    if (op >= 0x190 && op < 0x1a0 && !mem)
        return setcc_handlers[op & 0xf];
    return h_generic;
}
