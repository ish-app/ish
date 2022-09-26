#include <assert.h>
#include <stdint.h>
#include "jit/gen.h"
#include "emu/modrm.h"
#include "emu/cpuid.h"
#include "emu/fpu.h"
#include "emu/vec.h"
#include "emu/interrupt.h"

static int gen_step32(struct gen_state *state, struct tlb *tlb);
static int gen_step16(struct gen_state *state, struct tlb *tlb);

int gen_step(struct gen_state *state, struct tlb *tlb) {
    state->orig_ip = state->ip;
    state->orig_ip_extra = 0;
    return gen_step32(state, tlb);
}

static void gen(struct gen_state *state, unsigned long thing) {
    assert(state->size <= state->capacity);
    if (state->size >= state->capacity) {
        state->capacity *= 2;
        struct jit_block *bigger_block = realloc(state->block,
                sizeof(struct jit_block) + state->capacity * sizeof(unsigned long));
        if (bigger_block == NULL) {
            die("out of memory while jitting");
        }
        state->block = bigger_block;
    }
    assert(state->size < state->capacity);
    state->block->code[state->size++] = thing;
}

void gen_start(addr_t addr, struct gen_state *state) {
    state->capacity = JIT_BLOCK_INITIAL_CAPACITY;
    state->size = 0;
    state->ip = addr;
    for (int i = 0; i <= 1; i++) {
        state->jump_ip[i] = 0;
    }
    state->block_patch_ip = 0;

    struct jit_block *block = malloc(sizeof(struct jit_block) + state->capacity * sizeof(unsigned long));
    state->block = block;
    block->addr = addr;
}

void gen_end(struct gen_state *state) {
    struct jit_block *block = state->block;
    for (int i = 0; i <= 1; i++) {
        if (state->jump_ip[i] != 0) {
            block->jump_ip[i] = &block->code[state->jump_ip[i]];
            block->old_jump_ip[i] = *block->jump_ip[i];
        } else {
            block->jump_ip[i] = NULL;
        }

        list_init(&block->jumps_from[i]);
        list_init(&block->jumps_from_links[i]);
    }
    if (state->block_patch_ip != 0) {
        block->code[state->block_patch_ip] = (unsigned long) block;
    }
    if (block->addr != state->ip)
        block->end_addr = state->ip - 1;
    else
        block->end_addr = block->addr;
    list_init(&block->chain);
    block->is_jetsam = false;
    for (int i = 0; i <= 1; i++) {
        list_init(&block->page[i]);
    }
}

void gen_exit(struct gen_state *state) {
    extern void gadget_exit(void);
    // in case the last instruction didn't end the block
    gen(state, (unsigned long) gadget_exit);
    gen(state, state->ip);
}

#define DECLARE_LOCALS \
    dword_t addr_offset = 0; \
    bool end_block = false; \
    bool seg_gs = false

#define FINISH \
    return !end_block

#define RESTORE_IP state->ip = state->orig_ip
#define _READIMM(name, size) do {\
    state->ip += size/8; \
    if (!tlb_read(tlb, state->ip - size/8, &name, size/8)) SEGFAULT; \
} while (0)

#define READMODRM if (!modrm_decode32(&state->ip, tlb, &modrm)) SEGFAULT
#define READADDR _READIMM(addr_offset, 32)
#define SEG_GS() seg_gs = true

// This should stay in sync with the definition of .gadget_array in gadgets.h
enum arg {
    arg_reg_a, arg_reg_c, arg_reg_d, arg_reg_b, arg_reg_sp, arg_reg_bp, arg_reg_si, arg_reg_di,
    arg_imm, arg_mem, arg_addr, arg_gs,
    arg_count, arg_invalid,
    // the following should not be synced with the list mentioned above (no gadgets implement them)
    arg_modrm_val, arg_modrm_reg,
    arg_xmm_modrm_val, arg_xmm_modrm_reg,
    arg_mm_modrm_val, arg_mm_modrm_reg,
    arg_mem_addr, arg_1,
};

enum size {
    size_8, size_16, size_32,
    size_count,
    size_64, size_80, size_128, // bonus sizes
};

// sync with COND_LIST in control.S
enum cond {
    cond_O, cond_B, cond_E, cond_BE, cond_S, cond_P, cond_L, cond_LE,
    cond_count,
};

enum repeat {
    rep_once, rep_repz, rep_repnz,
    rep_count,
    rep_rep = rep_repz,
};

typedef void (*gadget_t)(void);

#define GEN(thing) gen(state, (unsigned long) (thing))
#define g(g) do { extern void gadget_##g(void); GEN(gadget_##g); } while (0)
#define gg(_g, a) do { g(_g); GEN(a); } while (0)
#define ggg(_g, a, b) do { g(_g); GEN(a); GEN(b); } while (0)
#define gggg(_g, a, b, c) do { g(_g); GEN(a); GEN(b); GEN(c); } while (0)
#define ggggg(_g, a, b, c, d) do { g(_g); GEN(a); GEN(b); GEN(c); GEN(d); } while (0)
#define gggggg(_g, a, b, c, d, e) do { g(_g); GEN(a); GEN(b); GEN(c); GEN(d); GEN(e); } while (0)
#define ga(g, i) do { extern gadget_t g##_gadgets[]; if (g##_gadgets[i] == NULL) UNDEFINED; GEN(g##_gadgets[i]); } while (0)
#define gag(g, i, a) do { ga(g, i); GEN(a); } while (0)
#define gagg(g, i, a, b) do { ga(g, i); GEN(a); GEN(b); } while (0)
#define gz(g, z) ga(g, sz(z))
#define h(h) gg(helper_0, h)
#define hh(h, a) ggg(helper_1, h, a)
#define hhh(h, a, b) gggg(helper_2, h, a, b)
#define h_read(h, z) do { g_addr(); ggg(helper_read##z, state->orig_ip, h##z); } while (0)
#define h_write(h, z) do { g_addr(); ggg(helper_write##z, state->orig_ip, h##z); } while (0)
#define UNDEFINED do { gggg(interrupt, INT_UNDEFINED, state->orig_ip, state->orig_ip); return false; } while (0)
#define SEGFAULT do { gggg(interrupt, INT_GPF, state->orig_ip, tlb->segfault_addr); return false; } while (0)

static inline int sz(int size) {
    switch (size) {
        case 8: return size_8;
        case 16: return size_16;
        case 32: return size_32;
        default: return -1;
    }
}

bool gen_addr(struct gen_state *state, struct modrm *modrm, bool seg_gs) {
    if (modrm->base == reg_none)
        gg(addr_none, modrm->offset);
    else
        gag(addr, modrm->base, modrm->offset);
    if (modrm->type == modrm_mem_si)
        ga(si, modrm->index * 4 + modrm->shift);
    if (seg_gs)
        g(seg_gs);
    return true;
}
#define g_addr() gen_addr(state, &modrm, seg_gs)

// this really wants to use all the locals of the decoder, which we can do
// really nicely in gcc using nested functions, but that won't work in clang,
// so we explicitly pass 500 arguments. sorry for the mess
static inline bool gen_op(struct gen_state *state, gadget_t *gadgets, enum arg arg, struct modrm *modrm, uint64_t *imm, int size, bool seg_gs, dword_t addr_offset) {
    size = sz(size);
    gadgets = gadgets + size * arg_count;

    switch (arg) {
        case arg_modrm_reg:
            // TODO find some way to assert that this won't overflow?
            arg = modrm->reg + arg_reg_a; break;
        case arg_modrm_val:
            if (modrm->type == modrm_reg)
                arg = modrm->base + arg_reg_a;
            else
                arg = arg_mem;
            break;
        case arg_mem_addr:
            arg = arg_mem;
            modrm->type = modrm_mem;
            modrm->base = reg_none;
            modrm->offset = addr_offset;
            break;
        case arg_1:
            arg = arg_imm;
            *imm = 1;
            break;
    }
    if (arg >= arg_count || gadgets[arg] == NULL) {
        UNDEFINED;
    }
    if (arg == arg_mem || arg == arg_addr) {
        if (!gen_addr(state, modrm, seg_gs))
            return false;
    }
    GEN(gadgets[arg]);
    if (arg == arg_imm)
        GEN(*imm);
    else if (arg == arg_mem)
        GEN(state->orig_ip | state->orig_ip_extra);
    return true;
}
#define op(type, thing, z) do { \
    extern gadget_t type##_gadgets[]; \
    if (!gen_op(state, type##_gadgets, arg_##thing, &modrm, &imm, z, seg_gs, addr_offset)) return false; \
} while (0)

#define load(thing, z) op(load, thing, z)
#define store(thing, z) op(store, thing, z)
// load-op-store
#define los(o, src, dst, z) load(dst, z); op(o, src, z); store(dst, z)
#define lo(o, src, dst, z) load(dst, z); op(o, src, z)

#define MOV(src, dst,z) load(src, z); store(dst, z)
#define MOVZX(src, dst,zs,zd) load(src, zs); gz(zero_extend, zs); store(dst, zd)
#define MOVSX(src, dst,zs,zd) load(src, zs); gz(sign_extend, zs); store(dst, zd)
// xchg must generate in this order to be atomic
#define XCHG(src, dst,z) load(src, z); op(xchg, dst, z); store(src, z)

#define ADD(src, dst,z) los(add, src, dst, z)
#define OR(src, dst,z) los(or, src, dst, z)
#define ADC(src, dst,z) los(adc, src, dst, z)
#define SBB(src, dst,z) los(sbb, src, dst, z)
#define AND(src, dst,z) los(and, src, dst, z)
#define SUB(src, dst,z) los(sub, src, dst, z)
#define XOR(src, dst,z) los(xor, src, dst, z)
#define CMP(src, dst,z) lo(sub, src, dst, z)
#define TEST(src, dst,z) lo(and, src, dst, z)
#define NOT(val,z) load(val,z); gz(not, z); store(val,z)
#define NEG(val,z) imm = 0; load(imm,z); op(sub, val,z); store(val,z)

#define POP(thing,z) \
    gg(pop, state->orig_ip); \
    state->orig_ip_extra = 1ul << 62; /* marks that on segfault the stack pointer should be adjusted */\
    store(thing, z)
#define PUSH(thing,z) load(thing, z); gg(push, state->orig_ip)

#define INC(val,z) load(val, z); gz(inc, z); store(val, z)
#define DEC(val,z) load(val, z); gz(dec, z); store(val, z)

#define fake_ip (state->ip | (1ul << 63))

#define jump_ips(off1, off2) \
    state->jump_ip[0] = state->size + off1; \
    if (off2 != 0) \
        state->jump_ip[1] = state->size + off2
#define JMP(loc) load(loc, OP_SIZE); g(jmp_indir); end_block = true
#define JMP_REL(off) gg(jmp, fake_ip + off); jump_ips(-1, 0); end_block = true
#define JCXZ_REL(off) ggg(jcxz, fake_ip + off, fake_ip); jump_ips(-2, -1); end_block = true
#define jcc(cc, to, else) gagg(jmp, cond_##cc, to, else); jump_ips(-2, -1); end_block = true
#define J_REL(cc, off)  jcc(cc, fake_ip + off, fake_ip)
#define JN_REL(cc, off) jcc(cc, fake_ip, fake_ip + off)

// state->orig_ip: for use with page fault handler;
// -1: will be patched to block address in gen_end();
// fake_ip: the first one is the return address, used for saving to stack and verifying the cached ip in return cache is correct;
// fake_ip: the second one is the return target, patchable by return chaining.
#define CALL(loc) do { \
    load(loc, OP_SIZE); \
    ggggg(call_indir, state->orig_ip, -1, fake_ip, fake_ip); \
    state->block_patch_ip = state->size - 3; \
    jump_ips(-1, 0); \
    end_block = true; \
} while (0)
// the first four arguments are the same with CALL,
// the last one is the call target, patchable by return chaining.
#define CALL_REL(off) do { \
    gggggg(call, state->orig_ip, -1, fake_ip, fake_ip, fake_ip + off); \
    state->block_patch_ip = state->size - 4; \
    jump_ips(-2, -1); \
    end_block = true; \
} while (0)
#define RET_NEAR(imm) ggg(ret, state->orig_ip, 4 + imm); end_block = true
#define INT(code) gggg(interrupt, (uint8_t) code, state->ip, 0); end_block = true

#define SET(cc, dst) ga(set, cond_##cc); store(dst, 8)
#define SETN(cc, dst) ga(setn, cond_##cc); store(dst, 8)
// wins the prize for the most annoying instruction to generate
#define CMOV(cc, src, dst,z) do { \
    gag(skipn, cond_##cc, 0); \
    int start = state->size; \
    load(src, z); store(dst, z); \
    state->block->code[start - 1] = (state->size - start) * sizeof(long); \
} while (0)
#define CMOVN(cc, src, dst,z) do { \
    gag(skip, cond_##cc, 0); \
    int start = state->size; \
    load(src, z); store(dst, z); \
    state->block->code[start - 1] = (state->size - start) * sizeof(long); \
} while (0)

#define PUSHF() g(pushf)
#define POPF() g(popf)
#define SAHF g(sahf)
#define CLD g(cld)
#define STD g(std)

#define MUL18(val,z) MUL1(val,z)
#define MUL1(val,z) load(val, z); gz(mul, z)
#define IMUL1(val,z) load(val, z); gz(imul1, z)
#define DIV(val, z) load(val, z); gz(div, z)
#define IDIV(val, z) load(val, z); gz(idiv, z)
#define IMUL3(times, src, dst,z) load(src, z); op(imul, times, z); store(dst, z)
#define IMUL2(val, reg,z) IMUL3(val, reg, reg, z)

#define CVT ga(cvt, sz(oz))
#define CVTE ga(cvte, sz(oz))

#define ROL(count, val,z) los(rol, count, val, z)
#define ROR(count, val,z) los(ror, count, val, z)
#define RCL(count, val,z) los(rcl, count, val, z)
#define RCR(count, val,z) los(rcr, count, val, z)
#define SHL(count, val,z) los(shl, count, val, z)
#define SHR(count, val,z) los(shr, count, val, z)
#define SAR(count, val,z) los(sar, count, val, z)

#define SHLD(count, extra, dst,z) \
    load(dst,z); \
    if (arg_##count == arg_reg_c) op(shld_cl, extra,z); \
    else { op(shld_imm, extra,z); GEN(imm); } \
    store(dst,z)
#define SHRD(count, extra, dst,z) \
    load(dst,z); \
    if (arg_##count == arg_reg_c) op(shrd_cl, extra,z); \
    else { op(shrd_imm, extra,z); GEN(imm); } \
    store(dst,z)

#define BT(bit, val,z) lo(bt, val, bit, z)
#define BTC(bit, val,z) lo(btc, val, bit, z)
#define BTS(bit, val,z) lo(bts, val, bit, z)
#define BTR(bit, val,z) lo(btr, val, bit, z)
#define BSF(src, dst,z) los(bsf, src, dst, z)
#define BSR(src, dst,z) los(bsr, src, dst, z)

#define BSWAP(dst) ga(bswap, arg_##dst)

#define strop(op, rep, z) gag(op, sz(z) * size_count + rep_##rep, state->orig_ip)
#define STR(op, z) strop(op, once, z)
#define REP(op, z) strop(op, rep, z)
#define REPZ(op, z) strop(op, repz, z)
#define REPNZ(op, z) strop(op, repnz, z)

#define CMPXCHG(src, dst,z) load(src, z); op(cmpxchg, dst, z)
#define CMPXCHG8B(dst,z) g_addr(); gg(cmpxchg8b, state->orig_ip)
#define XADD(src, dst,z) XCHG(src, dst,z); ADD(src, dst,z)

void helper_rdtsc(struct cpu_state *cpu);
#define RDTSC h(helper_rdtsc)
#define CPUID() g(cpuid)

// atomic
#define atomic_op(type, src, dst,z) load(src, z); op(atomic_##type, dst, z)
#define ATOMIC_ADD(src, dst,z) atomic_op(add, src, dst, z)
#define ATOMIC_OR(src, dst,z) atomic_op(or, src, dst, z)
#define ATOMIC_ADC(src, dst,z) atomic_op(adc, src, dst, z)
#define ATOMIC_SBB(src, dst,z) atomic_op(sbb, src, dst, z)
#define ATOMIC_AND(src, dst,z) atomic_op(and, src, dst, z)
#define ATOMIC_SUB(src, dst,z) atomic_op(sub, src, dst, z)
#define ATOMIC_XOR(src, dst,z) atomic_op(xor, src, dst, z)
#define ATOMIC_INC(val,z) op(atomic_inc, val, z)
#define ATOMIC_DEC(val,z) op(atomic_dec, val, z)
#define ATOMIC_CMPXCHG(src, dst,z) atomic_op(cmpxchg, src, dst, z)
#define ATOMIC_XADD(src, dst,z) load(src, z); op(atomic_xadd, dst, z); store(src, z)
#define ATOMIC_BTC(bit, val,z) lo(atomic_btc, val, bit, z)
#define ATOMIC_BTS(bit, val,z) lo(atomic_bts, val, bit, z)
#define ATOMIC_BTR(bit, val,z) lo(atomic_btr, val, bit, z)
#define ATOMIC_CMPXCHG8B(dst,z) g_addr(); gg(atomic_cmpxchg8b, state->orig_ip)

// fpu
#define st_0 0
#define st_i modrm.rm_opcode
#define FLD() hh(fpu_ld, st_i);
#define FILD(val,z) h_read(fpu_ild, z)
#define FLDM(val,z) h_read(fpu_ldm, z)
#define FSTM(dst,z) h_write(fpu_stm, z)
#define FIST(dst,z) h_write(fpu_ist, z)
#define FXCH() hh(fpu_xch, st_i)
#define FCOM() hh(fpu_com, st_i)
#define FCOMM(val,z) h_read(fpu_comm, z)
#define FICOM(val,z) h_read(fpu_icom, z)
#define FUCOM() hh(fpu_ucom, st_i)
#define FUCOMI() hh(fpu_ucomi, st_i)
#define FCOMI() hh(fpu_comi, st_i)
#define FTST() h(fpu_tst)
#define FXAM() h(fpu_xam)
#define FST() hh(fpu_st, st_i)
#define FCHS() h(fpu_chs)
#define FABS() h(fpu_abs)
#define FLDC(what) hh(fpu_ldc, fconst_##what)
#define FPREM() h(fpu_prem)
#define FRNDINT() h(fpu_rndint)
#define FSCALE() h(fpu_scale)
#define FSQRT() h(fpu_sqrt)
#define FYL2X() h(fpu_yl2x)
#define F2XM1() h(fpu_2xm1)
#define FSTSW(dst) if (arg_##dst == arg_reg_a) g(fstsw_ax); else UNDEFINED
#define FSTCW(dst) if (arg_##dst == arg_reg_a) UNDEFINED; else h_write(fpu_stcw, 16)
#define FLDCW(dst) if (arg_##dst == arg_reg_a) UNDEFINED; else h_read(fpu_ldcw, 16)
#define FSTENV(val,z) h_write(fpu_stenv, z)
#define FLDENV(val,z) h_write(fpu_ldenv, z)
#define FSAVE(val,z) h_write(fpu_save, z)
#define FRESTORE(val,z) h_write(fpu_restore, z)
#define FCLEX() h(fpu_clex)
#define FPOP h(fpu_pop)
#define FINCSTP() h(fpu_incstp)
#define FADD(src, dst) hhh(fpu_add, src, dst)
#define FIADD(val,z) h_read(fpu_iadd, z)
#define FADDM(val,z) h_read(fpu_addm, z)
#define FSUB(src, dst) hhh(fpu_sub, src, dst)
#define FSUBM(val,z) h_read(fpu_subm, z)
#define FISUB(val,z) h_read(fpu_isub, z)
#define FISUBR(val,z) h_read(fpu_isubr, z)
#define FSUBR(src, dst) hhh(fpu_subr, src, dst)
#define FSUBRM(val,z) h_read(fpu_subrm, z)
#define FMUL(src, dst) hhh(fpu_mul, src, dst)
#define FIMUL(val,z) h_read(fpu_imul, z)
#define FMULM(val,z) h_read(fpu_mulm, z)
#define FDIV(src, dst) hhh(fpu_div, src, dst)
#define FIDIV(val,z) h_read(fpu_idiv, z)
#define FDIVM(val,z) h_read(fpu_divm, z)
#define FDIVR(src, dst) hhh(fpu_divr, src, dst)
#define FIDIVR(val,z) h_read(fpu_idivr, z)
#define FDIVRM(val,z) h_read(fpu_divrm, z)
#define FPATAN() h(fpu_patan)
#define FSIN() h(fpu_sin)
#define FCOS() h(fpu_cos)
#define FXTRACT() h(fpu_xtract)
#define FCMOVB(src) hh(fpu_cmovb, src)
#define FCMOVE(src) hh(fpu_cmove, src)
#define FCMOVBE(src) hh(fpu_cmovbe, src)
#define FCMOVU(src) hh(fpu_cmovu, src)
#define FCMOVNB(src) hh(fpu_cmovnb, src)
#define FCMOVNE(src) hh(fpu_cmovne, src)
#define FCMOVNBE(src) hh(fpu_cmovnbe, src)
#define FCMOVNU(src) hh(fpu_cmovnu, src)

// vector

static inline bool could_be_memory(enum arg arg) {
    return arg == arg_modrm_val || arg == arg_mm_modrm_val || arg == arg_xmm_modrm_val;
}

static inline uint16_t cpu_reg_offset(enum arg arg, int index) {
    if (arg == arg_xmm_modrm_reg || arg == arg_xmm_modrm_val)
        return CPU_OFFSET(xmm[index]);
    if (arg == arg_mm_modrm_reg || arg == arg_mm_modrm_val)
        return CPU_OFFSET(mm[index]);
    if (arg == arg_modrm_reg || arg == arg_modrm_val)
        return CPU_OFFSET(regs[index]);
    return 0;
}

static inline bool gen_vec(enum arg src, enum arg dst, void (*helper)(), gadget_t read_mem_gadget, gadget_t write_mem_gadget, struct gen_state *state, struct modrm *modrm, uint8_t imm, bool seg_gs, bool has_imm) {
    bool rm_is_src = !could_be_memory(dst);
    enum arg rm = rm_is_src ? src : dst;
    enum arg reg = rm_is_src ? dst : src;

    uint16_t reg_offset = cpu_reg_offset(reg, modrm->opcode);
    uint16_t rm_reg_offset = cpu_reg_offset(rm, modrm->rm_opcode);
    assert(reg_offset != 0);

    if (could_be_memory(rm) && modrm->type != modrm_reg)
        rm = arg_mem;

    uint64_t imm_arg = 0;
    if (has_imm)
        imm_arg = (uint64_t) imm << 32;

    switch (rm) {
        case arg_xmm_modrm_val:
        case arg_mm_modrm_val:
        case arg_modrm_val:
            assert(rm_reg_offset != 0);
            if (!has_imm)
                g(vec_helper_reg);
            else
                g(vec_helper_reg_imm);
            GEN(helper);
            // first byte is src, second byte is dst
            uint64_t arg;
            if (rm_is_src)
                arg = rm_reg_offset | (reg_offset << 16);
            else
                arg = reg_offset | (rm_reg_offset << 16);
            GEN(arg | imm_arg);
            break;

        case arg_mem:
            gen_addr(state, modrm, seg_gs);
            GEN(rm_is_src ? read_mem_gadget : write_mem_gadget);
            GEN(state->orig_ip);
            GEN(helper);
            GEN(reg_offset | imm_arg);
            break;

        case arg_imm:
            // TODO: support immediates and opcode
            g(vec_helper_imm);
            GEN(helper);
            // This is rm_opcode instead of opcode because PSRLQ is weird like that
            GEN(((uint16_t) imm) | (cpu_reg_offset(reg, modrm->rm_opcode) << 16));
            break;

        default: die("unimplemented vecarg");
    }
    return true;
}

#define has_imm_ false
#define has_imm__imm true
#define _v(src, dst, helper, _imm, z) do { \
    extern void gadget_vec_helper_read##z##_imm(void); \
    extern void gadget_vec_helper_write##z##_imm(void); \
    if (!gen_vec(src, dst, (void (*)()) helper, gadget_vec_helper_read##z##_imm, gadget_vec_helper_write##z##_imm, state, &modrm, imm, seg_gs, has_imm_##_imm)) return false; \
} while (0)
#define v_(op, src, dst, _imm,z) _v(arg_##src, arg_##dst, vec_##op##z, _imm,z)
#define v(op, src, dst,z) v_(op, src, dst,,z)
#define v_imm(op, src, dst,z) v_(op, src, dst, _imm,z)

#define vec_dst_size_modrm_val 32
#define vec_dst_size_mm_modrm_val 64
#define vec_dst_size_mm_modrm_reg 64
#define vec_dst_size_xmm_modrm_val 128
#define vec_dst_size_xmm_modrm_reg 128
// you always want to merge when storing to memory
// default is to never merge otherwise
#define VMOV(src, dst, z) \
    if (could_be_memory(arg_##dst) && modrm.type != modrm_reg) { \
        v(merge, src, dst,z); \
    } else { \
        v(glue3(zero, vec_dst_size_##dst, _copy), src, dst,z); \
    }
// this will additionally merge if both src and dst are registers, e.g. movss
#define VMOV_MERGE_REG(src, dst, z) \
    if (modrm.type == modrm_reg || could_be_memory(arg_##dst)) { \
        v(merge, src, dst,z); \
    } else { \
        v(glue3(zero, vec_dst_size_##dst, _copy), src, dst,z); \
    }

#define VCOMPARE(src, dst,z) v(compare, src, dst,z)
#define V_OP(op, src, dst, z) v(op, src, dst, z)
#define V_OP_IMM(op, src, dst, z) v_imm(op, src, dst, z)

#define DECODER_RET static int
#define DECODER_NAME gen_step
#define DECODER_ARGS struct gen_state *state, struct tlb *tlb
#define DECODER_PASS_ARGS state, tlb

#define OP_SIZE 32
#include "emu/decode.h"
#undef OP_SIZE
#define OP_SIZE 16
#include "emu/decode.h"
#undef OP_SIZE
