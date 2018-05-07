#include <assert.h>
#include "emu/modrm.h"
#include "emu/gen.h"
#include "emu/interrupt.h"

// This should stay in sync with the .irp in the definition of .gadget_array in gadgets.S
enum arg {
    arg_eax, arg_ecx, arg_edx, arg_ebx, arg_esp, arg_ebp, arg_esi, arg_edi,
    arg_ax, arg_cx, arg_dx, arg_bx, arg_sp, arg_bp, arg_si, arg_di,
    arg_imm, arg_mem32,
    arg_cnt,
    // the following should not be synced with the aforementioned .irp (no gadgets implement them)
    arg_al, arg_cl, arg_dl, arg_bl, arg_ah, arg_ch, arg_dh, arg_bh,
    arg_modrm_val, arg_modrm_reg, arg_mem_addr, arg_addr, arg_gs,
    // markers
    arg_reg32 = arg_eax, arg_reg16 = arg_ax,
};

// there are many
typedef void (*gadget_t)();
void gadget_interrupt();
void gadget_exit();
void gadget_push();
extern gadget_t load_gadgets[arg_cnt];
extern gadget_t store_gadgets[arg_cnt];
extern gadget_t sub_gadgets[arg_cnt];

#define GEN(thing) gen(state, (unsigned long) (thing))
#define g(g) GEN(gadget_##g)
#define gg(g, a) do { GEN(gadget_##g); GEN(a); } while (0)
#define UNDEFINED do { gg(interrupt, INT_UNDEFINED); return; } while (0)

static inline void gen_op(struct gen_state *state, gadget_t *gadgets, enum arg arg, struct modrm *modrm, uint64_t *imm) {
    switch (arg) {
        case arg_modrm_reg:
            // TODO find some way to assert that this won't overflow?
            arg = modrm->reg + arg_reg32; break;
        case arg_modrm_val:
            switch (modrm->type) {
                case modrm_reg:
                    arg = modrm->base + arg_reg32; break;
                default: UNDEFINED;
            }
            break;
        default: break;
    }
    if (arg >= arg_cnt || gadgets[arg] == NULL) {
        debugger;
        UNDEFINED;
    }
    GEN(gadgets[arg]);
    if (arg == arg_imm)
        GEN(*imm);
}
#define op(type, thing) gen_op(state, type##_gadgets, arg_##thing, &modrm, &imm)

#define load(thing) op(load, thing)
#define store(thing) op(store, thing)

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

#define MOV(src, dst,z) load(src); store(dst)
#define MOVZX(src, dst,zs,zd) UNDEFINED
#define MOVSX(src, dst,zs,zd) UNDEFINED
#define XCHG(src, dst,z) UNDEFINED

#define ADD(src, dst,z) UNDEFINED
#define OR(src, dst,z) UNDEFINED
#define ADC(src, dst,z) UNDEFINED
#define SBB(src, dst,z) UNDEFINED
#define AND(src, dst,z) UNDEFINED
#define SUB(src, dst,z) load(dst); op(sub, src); store(dst)
#define XOR(src, dst,z) UNDEFINED
#define CMP(src, dst,z) UNDEFINED
#define TEST(src, dst,z) UNDEFINED
#define NOT(val,z) UNDEFINED
#define NEG(val,z) UNDEFINED

#define POP(thing) UNDEFINED
#define PUSH(thing) load(thing); g(push)

#define INC(val,z) UNDEFINED
#define DEC(val,z) UNDEFINED

#define JMP(loc) UNDEFINED
#define JMP_REL(off) UNDEFINED
#define JCXZ_REL(off) UNDEFINED
#define J_REL(cc, off) UNDEFINED
#define CALL(loc) UNDEFINED
#define CALL_REL(off) UNDEFINED
#define SET(cc, dst) UNDEFINED
#define CMOV(cc, src, dst,z) UNDEFINED
#define RET_NEAR_IMM(imm) UNDEFINED
#define RET_NEAR() UNDEFINED
#define INT(code) UNDEFINED

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
