#include <assert.h>
#include "emu/modrm.h"
#include "emu/gen.h"
#include "emu/interrupt.h"

// This should stay in sync with the definition of .gadget_array in gadgets.h
enum arg {
    arg_reg_a, arg_reg_c, arg_reg_d, arg_reg_b, arg_reg_sp, arg_reg_bp, arg_reg_si, arg_reg_di,
    arg_imm, arg_mem, arg_addr,
    arg_count, arg_invalid,
    // the following should not be synced with the list mentioned above (no gadgets implement them)
    arg_modrm_val, arg_modrm_reg, arg_mem_addr, arg_gs, arg_1,
};

enum size {
    size_8, size_16, size_32,
    size_count,
};

// sync with COND_LIST in control.S
enum cond {
    cond_O, cond_B, cond_E, cond_BE, cond_S, cond_P, cond_L, cond_LE,
    cond_count,
};

typedef void (*gadget_t)();

#define GEN(thing) gen(state, (unsigned long) (thing))
#define g(g) do { extern void gadget_##g(); GEN(gadget_##g); } while (0)
#define gg(_g, a) do { g(_g); GEN(a); } while (0)
#define ggg(_g, a, b) do { g(_g); GEN(a); GEN(b); } while (0)
#define ga(g, i) do { extern gadget_t g##_gadgets[]; if (g##_gadgets[i] == NULL) UNDEFINED; GEN(g##_gadgets[i]); } while (0)
#define gag(g, i, a) do { ga(g, i); GEN(a); } while (0)
#define gagg(g, i, a, b) do { ga(g, i); GEN(a); GEN(b); } while (0)
#define gg_here(g, a) ggg(g, a, state->ip)
#define UNDEFINED do { gg_here(interrupt, INT_UNDEFINED); return; } while (0)

static inline int sz(int size) {
    switch (size) {
        case 8: return size_8;
        case 16: return size_16;
        case 32: return size_32;
        default: return -1;
    }
}

// this really wants to use all the locals of the decoder, which we can do
// really nicely in gcc using nested functions, but that won't work in clang,
// so we explicitly pass 500 arguments. sorry for the mess
static inline void gen_op(struct gen_state *state, gadget_t *gadgets, enum arg arg, struct modrm *modrm, uint64_t *imm, int size) {
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
        case arg_1:
            arg = arg_imm;
            *imm = 1;
            break;
    }
    if (arg >= arg_count || gadgets[arg] == NULL) {
        debugger;
        UNDEFINED;
    }
    switch (arg) {
        case arg_mem:
        case arg_addr:
            if (modrm->base == reg_none)
                gg(addr_none, modrm->offset);
            else
                gag(addr, modrm->base, modrm->offset);
            if (modrm->type == modrm_mem_si)
                ga(si, modrm->index * 4 + modrm->shift);
            break;
    }
    GEN(gadgets[arg]);
    if (arg == arg_imm)
        GEN(*imm);
}
#define op(type, thing, z) do { \
    extern gadget_t type##_gadgets[]; \
    gen_op(state, type##_gadgets, arg_##thing, &modrm, &imm, z); \
} while (0)

#define load(thing, z) op(load, thing, z)
#define store(thing, z) op(store, thing, z)
// load-op-store
#define los(o, src, dst, z) load(dst, z); op(o, src, z); store(dst, z)
#define lo(o, src, dst, z) load(dst, z); op(o, src, z)

#define DECLARE_LOCALS \
    dword_t addr_offset = 0;

#define RETURN(thing) (void) (thing); return

#define TRACEIP() TRACE("%d %08x\t", current->pid, state->ip);

#define _READIMM(name, size) \
    tlb_read(tlb, state->ip, &name, size/8); \
    state->ip += size/8

#define READMODRM modrm_decode32(&state->ip, tlb, &modrm)
#define READADDR _READIMM(addr_offset, 32)
#define SEG_GS() UNDEFINED

#define MOV(src, dst,z) load(src, z); store(dst, z)
#define MOVZX(src, dst,zs,zd) load(src, zs); store(dst, zd)
#define MOVSX(src, dst,zs,zd) UNDEFINED
#define XCHG(src, dst,z) UNDEFINED

#define ADD(src, dst,z) los(add, src, dst, z)
#define OR(src, dst,z) los(or, src, dst, z)
#define ADC(src, dst,z) UNDEFINED
#define SBB(src, dst,z) UNDEFINED
#define AND(src, dst,z) los(and, src, dst, z)
#define SUB(src, dst,z) los(sub, src, dst, z)
#define XOR(src, dst,z) los(xor, src, dst, z)
#define CMP(src, dst,z) lo(sub, src, dst, z)
#define TEST(src, dst,z) lo(and, src, dst, z)
#define NOT(val,z) UNDEFINED
#define NEG(val,z) load(val,z); ga(neg, sz(z)); store(val,z)

#define POP(thing,z) g(pop); store(thing, z)
#define PUSH(thing,z) load(thing, z); g(push)

#define INC(val,z) load(val, z); g(inc); store(val, z)
#define DEC(val,z) load(val, z); g(dec); store(val, z)

#define JMP(loc) load(loc, OP_SIZE); g(jmp_indir)
#define JMP_REL(off) gg(jmp, state->ip + off)
#define JCXZ_REL(off) UNDEFINED
#define J_REL(cc, off) gagg(jmp, cond_##cc, state->ip + off, state->ip)
#define JN_REL(cc, off) gagg(jmp, cond_##cc, state->ip, state->ip + off)
#define CALL(loc) load(loc, OP_SIZE); g(call_indir)
#define CALL_REL(off) gg_here(call, state->ip + off)
#define SET(cc, dst) ga(set, cond_##cc); store(dst, 8)
#define SETN(cc, dst) ga(setn, cond_##cc); store(dst, 8)
#define CMOV(cc, src, dst,z) UNDEFINED
#define RET_NEAR_IMM(imm) UNDEFINED
#define RET_NEAR() g(ret)
#define INT(code) gg_here(interrupt, (uint8_t) code)

#define PUSHF() UNDEFINED
#define POPF() UNDEFINED
#define SAHF UNDEFINED
#define CLD UNDEFINED
#define STD UNDEFINED

#define MUL18(val,z) UNDEFINED
#define MUL1(val,z) UNDEFINED
#define IMUL1(val,z) UNDEFINED
#define MUL2(val, reg,z) UNDEFINED
#define IMUL2(val, reg,z) UNDEFINED
#define MUL3(imm, src, dst,z) UNDEFINED
#define IMUL3(imm, src, dst,z) UNDEFINED
#define DIV(val, z) load(val, z); ga(div, sz(z)); store(val, z)
#define IDIV(val, z) UNDEFINED

#define CVT UNDEFINED
#define CVTE UNDEFINED

#define ROL(count, val,z) UNDEFINED
#define ROR(count, val,z) UNDEFINED
#define SHL(count, val,z) los(shl, count, val, z)
#define SHR(count, val,z) los(shr, count, val, z)
#define SAR(count, val,z) UNDEFINED

#define SHLD(count, extra, dst,z) UNDEFINED
#define SHRD(count, extra, dst,z) UNDEFINED

#define BT(bit, val,z) lo(bt, bit, val, z)
#define BTC(bit, val,z) UNDEFINED
#define BTS(bit, val,z) UNDEFINED
#define BTR(bit, val,z) UNDEFINED
#define BSF(src, dst,z) UNDEFINED
#define BSR(src, dst,z) UNDEFINED

#define BSWAP(dst) UNDEFINED

#define SCAS(z) UNDEFINED
#define MOVS(z) UNDEFINED
#define LODS(z) UNDEFINED
#define STOS(z) UNDEFINED
#define CMPS(z) UNDEFINED
#define REP(op) UNDEFINED
#define REPZ(op) UNDEFINED
#define REPNZ(op) UNDEFINED

#define CMPXCHG(src, dst,z) UNDEFINED
#define XADD(src, dst,z) UNDEFINED

#define RDTSC UNDEFINED
#define CPUID() UNDEFINED

// sse
#define XORP(src, dst) UNDEFINED
#define PSRLQ(src, dst) UNDEFINED
#define PCMPEQD(src, dst) UNDEFINED
#define PADD(src, dst) UNDEFINED
#define PSUB(src, dst) UNDEFINED
#define MOVQ(src, dst) UNDEFINED
#define MOVD(src, dst) UNDEFINED
#define CVTTSD2SI(src, dst) UNDEFINED

// fpu
#define FLD() UNDEFINED
#define FILD(val,z) UNDEFINED
#define FLDM(val,z) UNDEFINED
#define FSTM(dst,z) UNDEFINED
#define FIST(dst,z) UNDEFINED
#define FXCH() UNDEFINED
#define FUCOM() UNDEFINED
#define FUCOMI() UNDEFINED
#define FST() UNDEFINED
#define FCHS() UNDEFINED
#define FABS() UNDEFINED
#define FLDC(what) UNDEFINED
#define FPREM() UNDEFINED
#define FSTSW(dst) UNDEFINED
#define FSTCW(dst) UNDEFINED
#define FLDCW(dst) UNDEFINED
#define FPOP UNDEFINED
#define FADD(src, dst) UNDEFINED
#define FIADD(val,z) UNDEFINED
#define FADDM(val,z) UNDEFINED
#define FSUB(src, dst) UNDEFINED
#define FSUBM(val,z) UNDEFINED
#define FISUB(val,z) UNDEFINED
#define FMUL(src, dst) UNDEFINED
#define FIMUL(val,z) UNDEFINED
#define FMULM(val,z) UNDEFINED
#define FDIV(src, dst) UNDEFINED
#define FIDIV(val,z) UNDEFINED
#define FDIVM(val,z) UNDEFINED

#define DECODER_RET void
#define DECODER_NAME gen_step
#define DECODER_ARGS struct gen_state *state, struct tlb *tlb
#define DECODER_PASS_ARGS state, tlb

#define OP_SIZE 32
#include "emu/decode.h"
#undef OP_SIZE
#define OP_SIZE 16
#include "emu/decode.h"
#undef OP_SIZE
