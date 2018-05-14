#include <assert.h>
#include "emu/modrm.h"
#include "emu/gen.h"
#include "emu/interrupt.h"

// This should stay in sync with the definition of .gadget_array in gadgets.h
enum arg {
    arg_eax, arg_ecx, arg_edx, arg_ebx, arg_esp, arg_ebp, arg_esi, arg_edi,
    arg_ax, arg_cx, arg_dx, arg_bx, arg_sp, arg_bp, arg_si, arg_di,
    arg_imm, arg_mem32, arg_addr,
    arg_cnt,
    // the following should not be synced with the list mentioned above (no gadgets implement them)
    arg_al, arg_cl, arg_dl, arg_bl, arg_ah, arg_ch, arg_dh, arg_bh,
    arg_modrm_val, arg_modrm_reg, arg_mem_addr, arg_gs,
    // markers
    arg_reg32 = arg_eax, arg_reg16 = arg_ax,
};

// sync with COND_LIST in control.S
enum cond {
    cond_O, cond_B, cond_E, cond_BE, cond_S, cond_P, cond_L, cond_LE,
    cond_cnt,
};

// there are many
typedef void (*gadget_t)();
void gadget_interrupt();
void gadget_exit();
void gadget_push();
void gadget_inc();
void gadget_dec();
extern gadget_t load_gadgets[arg_cnt];
extern gadget_t store_gadgets[arg_cnt];
extern gadget_t add_gadgets[arg_cnt];
extern gadget_t and_gadgets[arg_cnt];
extern gadget_t sub_gadgets[arg_cnt];
extern gadget_t xor_gadgets[arg_cnt];

void gadget_call();
void gadget_ret();
void gadget_jmp();
void gadget_jmp_indir();
extern gadget_t jmp_gadgets[cond_cnt];

extern gadget_t addr_gadgets[reg_cnt];
extern gadget_t si_gadgets[reg_cnt * 3];

#define GEN(thing) gen(state, (unsigned long) (thing))
#define g(g) GEN(gadget_##g)
#define gg(g, a) do { GEN(gadget_##g); GEN(a); } while (0)
#define ggg(g, a, b) do { GEN(gadget_##g); GEN(a); GEN(b); } while (0)
#define ga(g, i) do { if (g##_gadgets[i] == NULL) UNDEFINED; GEN(g##_gadgets[i]); } while (0)
#define gag(g, i, a) do { ga(g, i); GEN(a); } while (0)
#define gagg(g, i, a, b) do { ga(g, i); GEN(a); GEN(b); } while (0)
#define gg_here(g, a) ggg(g, a, state->ip)
#define UNDEFINED do { gg_here(interrupt, INT_UNDEFINED); return; } while (0)

// this really wants to use all the locals of the decoder, which we can do
// really nicely in gcc using nested functions, but that won't work in clang,
// so we explicitly pass 500 arguments. sorry for the mess
static inline void gen_op(struct gen_state *state, gadget_t *gadgets, enum arg arg, struct modrm *modrm, uint64_t *imm, int size) {
    if (size != 32)
        UNDEFINED;
    switch (arg) {
        case arg_modrm_reg:
            // TODO find some way to assert that this won't overflow?
            arg = modrm->reg + arg_reg32; break;
        case arg_modrm_val:
            if (modrm->type == modrm_reg)
                arg = modrm->base + arg_reg32;
            else
                arg = arg_mem32;
            break;
        default: break;
    }
    if (arg >= arg_cnt || gadgets[arg] == NULL) {
        debugger;
        UNDEFINED;
    }
    if (arg == arg_mem32 || arg == arg_addr) {
        gag(addr, modrm->base, modrm->offset);
        if (modrm->type == modrm_mem_si)
            ga(si, modrm->index * 3 + modrm->shift);
    }
    GEN(gadgets[arg]);
    if (arg == arg_imm)
        GEN(*imm);
}
#define op(type, thing, z) gen_op(state, type##_gadgets, arg_##thing, &modrm, &imm, sz(z))

#define load(thing,z) op(load, thing,z)
#define store(thing,z) op(store, thing,z)
// load-op-store
#define los(o, src, dst,z) load(dst,z); op(o, src,z); store(dst,z)
#define lo(o, src, dst,z) load(dst,z); op(o, src,z)

#define sz(x) sz_##x
#define sz_ OP_SIZE
#define sz_8 8
#define sz_16 16
#define sz_128 128

#define DECLARE_LOCALS \
    dword_t addr_offset = 0;

#define RETURN(thing) (void) (thing)

#define TRACEIP() TRACE("%d %08x\t", current->pid, state->ip);

#define _READIMM(name, size) \
    tlb_read(tlb, state->ip, &name, size/8); \
    state->ip += size/8

#define READMODRM modrm_decode32(&state->ip, tlb, &modrm)
#define READADDR _READIMM(addr_offset, 32)
#define SEG_GS() UNDEFINED

#define MOV(src, dst,z) load(src,z); store(dst,z)
#define MOVZX(src, dst,zs,zd) UNDEFINED
#define MOVSX(src, dst,zs,zd) UNDEFINED
#define XCHG(src, dst,z) UNDEFINED

#define ADD(src, dst,z) los(add, src, dst,z)
#define OR(src, dst,z) UNDEFINED
#define ADC(src, dst,z) UNDEFINED
#define SBB(src, dst,z) UNDEFINED
#define AND(src, dst,z) los(and, src, dst,z)
#define SUB(src, dst,z) los(sub, src, dst,z)
#define XOR(src, dst,z) los(xor, src, dst,z)
#define CMP(src, dst,z) lo(sub, src, dst,z)
#define TEST(src, dst,z) lo(and, src, dst,z)
#define NOT(val,z) UNDEFINED
#define NEG(val,z) UNDEFINED

#define POP(thing) UNDEFINED
#define PUSH(thing) load(thing,); g(push)

#define INC(val,z) load(val,z); g(inc); store(val,z)
#define DEC(val,z) load(val,z); g(dec); store(val,z)

#define JMP(loc) load(loc,); g(jmp_indir)
#define JMP_REL(off) gg(jmp, state->ip + off)
#define JCXZ_REL(off) UNDEFINED
#define J_REL(cc, off) gagg(jmp, cond_##cc, state->ip + off, state->ip)
#define JN_REL(cc, off) gagg(jmp, cond_##cc, state->ip, state->ip + off)
#define CALL(loc) UNDEFINED
#define CALL_REL(off) gg_here(call, state->ip + off)
#define SET(cc, dst) UNDEFINED
#define CMOV(cc, src, dst,z) UNDEFINED
#define RET_NEAR_IMM(imm) UNDEFINED
#define RET_NEAR() g(ret)
#define INT(code) gg_here(interrupt, (uint8_t) code)

#define PUSHF() UNDEFINED
#define POPF() UNDEFINED
#define SAHF UNDEFINED
#define CLD UNDEFINED
#define STD UNDEFINED

#define MUL18(val) UNDEFINED
#define MUL1(val,z) UNDEFINED
#define IMUL1(val,z) UNDEFINED
#define MUL2(val, reg) UNDEFINED
#define IMUL2(val, reg,z) UNDEFINED
#define MUL3(imm, src, dst) UNDEFINED
#define IMUL3(imm, src, dst,z) UNDEFINED
#define DIV(reg, val, rem,z) UNDEFINED
#define IDIV(reg, val, rem,z) UNDEFINED

#define CVT UNDEFINED
#define CVTE UNDEFINED

#define ROL(count, val,z) UNDEFINED
#define ROR(count, val,z) UNDEFINED
#define SHL(count, val,z) UNDEFINED
#define SHR(count, val,z) UNDEFINED
#define SAR(count, val,z) UNDEFINED

#define SHLD(count, extra, dst,z) UNDEFINED
#define SHRD(count, extra, dst,z) UNDEFINED

#define BT(bit, val,z) UNDEFINED
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
