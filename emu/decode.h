#include "misc.h"
#include "emu/cpu.h"
#include "emu/modrm.h"
#include "emu/interrupt.h"

#undef oz
#define oz OP_SIZE
#define reg_ah reg_sp
#define reg_ch reg_bp
#define reg_dh reg_si
#define reg_bh reg_di

#undef DEFAULT_CHANNEL
#define DEFAULT_CHANNEL instr
#define TRACEI(msg, ...) TRACE(msg "\t", ##__VA_ARGS__)
extern int current_pid(void);
#define TRACEIP() TRACE("%d %08x\t", current_pid(), state->ip)

// this will be the next PyEval_EvalFrameEx
__no_instrument DECODER_RET glue(DECODER_NAME, OP_SIZE)(DECODER_ARGS) {
    DECLARE_LOCALS;

    byte_t insn;
    uint64_t imm = 0;
    struct modrm modrm;
#define READIMM_(name, size) _READIMM(name, size); TRACE("imm %llx ", (long long) name)
#define READINSN _READIMM(insn, 8); TRACE("%02x ", insn)
#define READIMM READIMM_(imm, OP_SIZE)
#define READIMMoz READIMM // there's nothing more permanent than a temporary hack
#define READIMM8 READIMM_(imm, 8); imm = (int8_t) (uint8_t) imm
#define READIMM16 READIMM_(imm, 16)
#define READMODRM_MEM READMODRM; if (modrm.type == modrm_reg) UNDEFINED

restart:
    TRACEIP();
    READINSN;
    switch (insn) {
#define MAKE_OP(x, OP, op) \
        case x+0x0: TRACEI(op " reg8, modrm8"); \
                   READMODRM; OP(modrm_reg, modrm_val,8); break; \
        case x+0x1: TRACEI(op " reg, modrm"); \
                   READMODRM; OP(modrm_reg, modrm_val,oz); break; \
        case x+0x2: TRACEI(op " modrm8, reg8"); \
                   READMODRM; OP(modrm_val, modrm_reg,8); break; \
        case x+0x3: TRACEI(op " modrm, reg"); \
                   READMODRM; OP(modrm_val, modrm_reg,oz); break; \
        case x+0x4: TRACEI(op " imm8, al\t"); \
                   READIMM8; OP(imm, reg_a,8); break; \
        case x+0x5: TRACEI(op " imm, oax\t"); \
                   READIMM; OP(imm, reg_a,oz); break

        MAKE_OP(0x00, ADD, "add");
        MAKE_OP(0x08, OR, "or");

        case 0x0f:
            // 2-byte opcode prefix
            READINSN;
            switch (insn) {
                case 0x18 ... 0x1f: TRACEI("nop modrm\t"); READMODRM; break;

                case 0x28: TRACEI("movp modrm, reg");
                           READMODRM; MOV(modrm_val, modrm_reg,128); break;
                case 0x29: TRACEI("movp reg, modrm");
                           READMODRM; MOV(modrm_reg, modrm_val,128); break;

                case 0x31: TRACEI("rdtsc");
                           RDTSC; break;

                case 0x40: TRACEI("cmovo modrm, reg");
                           READMODRM; CMOV(O, modrm_val, modrm_reg,oz); break;
                case 0x41: TRACEI("cmovno modrm, reg");
                           READMODRM; CMOVN(O, modrm_val, modrm_reg,oz); break;
                case 0x42: TRACEI("cmovb modrm, reg");
                           READMODRM; CMOV(B, modrm_val, modrm_reg,oz); break;
                case 0x43: TRACEI("cmovnb modrm, reg");
                           READMODRM; CMOVN(B, modrm_val, modrm_reg,oz); break;
                case 0x44: TRACEI("cmove modrm, reg");
                           READMODRM; CMOV(E, modrm_val, modrm_reg,oz); break;
                case 0x45: TRACEI("cmovne modrm, reg");
                           READMODRM; CMOVN(E, modrm_val, modrm_reg,oz); break;
                case 0x46: TRACEI("cmovbe modrm, reg");
                           READMODRM; CMOV(BE, modrm_val, modrm_reg,oz); break;
                case 0x47: TRACEI("cmova modrm, reg");
                           READMODRM; CMOVN(BE, modrm_val, modrm_reg,oz); break;
                case 0x48: TRACEI("cmovs modrm, reg");
                           READMODRM; CMOV(S, modrm_val, modrm_reg,oz); break;
                case 0x49: TRACEI("cmovns modrm, reg");
                           READMODRM; CMOVN(S, modrm_val, modrm_reg,oz); break;
                case 0x4a: TRACEI("cmovp modrm, reg");
                           READMODRM; CMOV(P, modrm_val, modrm_reg,oz); break;
                case 0x4b: TRACEI("cmovnp modrm, reg");
                           READMODRM; CMOVN(P, modrm_val, modrm_reg,oz); break;
                case 0x4c: TRACEI("cmovl modrm, reg");
                           READMODRM; CMOV(L, modrm_val, modrm_reg,oz); break;
                case 0x4d: TRACEI("cmovnl modrm, reg");
                           READMODRM; CMOVN(L, modrm_val, modrm_reg,oz); break;
                case 0x4e: TRACEI("cmovle modrm, reg");
                           READMODRM; CMOV(LE, modrm_val, modrm_reg,oz); break;
                case 0x4f: TRACEI("cmovnle modrm, reg");
                           READMODRM; CMOVN(LE, modrm_val, modrm_reg,oz); break;

                case 0x80: TRACEI("jo rel\t");
                           READIMM; J_REL(O, imm); break;
                case 0x81: TRACEI("jno rel\t");
                           READIMM; JN_REL(O, imm); break;
                case 0x82: TRACEI("jb rel\t");
                           READIMM; J_REL(B, imm); break;
                case 0x83: TRACEI("jnb rel\t");
                           READIMM; JN_REL(B, imm); break;
                case 0x84: TRACEI("je rel\t");
                           READIMM; J_REL(E, imm); break;
                case 0x85: TRACEI("jne rel\t");
                           READIMM; JN_REL(E, imm); break;
                case 0x86: TRACEI("jbe rel\t");
                           READIMM; J_REL(BE, imm); break;
                case 0x87: TRACEI("ja rel\t");
                           READIMM; JN_REL(BE, imm); break;
                case 0x88: TRACEI("js rel\t");
                           READIMM; J_REL(S, imm); break;
                case 0x89: TRACEI("jns rel\t");
                           READIMM; JN_REL(S, imm); break;
                case 0x8a: TRACEI("jp rel\t");
                           READIMM; J_REL(P, imm); break;
                case 0x8b: TRACEI("jnp rel\t");
                           READIMM; JN_REL(P, imm); break;
                case 0x8c: TRACEI("jl rel\t");
                           READIMM; J_REL(L, imm); break;
                case 0x8d: TRACEI("jnl rel\t");
                           READIMM; JN_REL(L, imm); break;
                case 0x8e: TRACEI("jle rel\t");
                           READIMM; J_REL(LE, imm); break;
                case 0x8f: TRACEI("jnle rel\t");
                           READIMM; JN_REL(LE, imm); break;

                case 0x90: TRACEI("seto\t");
                           READMODRM; SET(O, modrm_val); break;
                case 0x91: TRACEI("setno\t");
                           READMODRM; SETN(O, modrm_val); break;
                case 0x92: TRACEI("setb\t");
                           READMODRM; SET(B, modrm_val); break;
                case 0x93: TRACEI("setnb\t");
                           READMODRM; SETN(B, modrm_val); break;
                case 0x94: TRACEI("sete\t");
                           READMODRM; SET(E, modrm_val); break;
                case 0x95: TRACEI("setne\t");
                           READMODRM; SETN(E, modrm_val); break;
                case 0x96: TRACEI("setbe\t");
                           READMODRM; SET(BE, modrm_val); break;
                case 0x97: TRACEI("setnbe\t");
                           READMODRM; SETN(BE, modrm_val); break;
                case 0x98: TRACEI("sets\t");
                           READMODRM; SET(S, modrm_val); break;
                case 0x99: TRACEI("setns\t");
                           READMODRM; SETN(S, modrm_val); break;
                case 0x9a: TRACEI("setp\t");
                           READMODRM; SET(P, modrm_val); break;
                case 0x9b: TRACEI("setnp\t");
                           READMODRM; SETN(P, modrm_val); break;
                case 0x9c: TRACEI("setl\t");
                           READMODRM; SET(L, modrm_val); break;
                case 0x9d: TRACEI("setnl\t");
                           READMODRM; SETN(L, modrm_val); break;
                case 0x9e: TRACEI("setle\t");
                           READMODRM; SET(LE, modrm_val); break;
                case 0x9f: TRACEI("setnle\t");
                           READMODRM; SETN(LE, modrm_val); break;

                case 0xa2: TRACEI("cpuid"); CPUID(); break;

                case 0xa3: TRACEI("bt reg, modrm");
                           READMODRM; BT(modrm_reg, modrm_val,oz); break;

                case 0xa4: TRACEI("shld imm8, reg, modrm");
                           READMODRM; READIMM8; SHLD(imm, modrm_reg, modrm_val,oz); break;
                case 0xa5: TRACEI("shld cl, reg, modrm");
                           READMODRM; SHLD(reg_c, modrm_reg, modrm_val,oz); break;

                case 0xab: TRACEI("bts reg, modrm");
                           READMODRM; BTS(modrm_reg, modrm_val,oz); break;

                case 0xac: TRACEI("shrd imm8, reg, modrm");
                           READMODRM; READIMM8; SHRD(imm, modrm_reg, modrm_val,oz); break;
                case 0xad: TRACEI("shrd cl, reg, modrm");
                           READMODRM; SHRD(reg_c, modrm_reg, modrm_val,oz); break;

                case 0xaf: TRACEI("imul modrm, reg");
                           READMODRM; IMUL2(modrm_val, modrm_reg,oz); break;

                case 0xb0: TRACEI("cmpxchg reg8, modrm8");
                           READMODRM_MEM; CMPXCHG(modrm_reg, modrm_val,8); break;
                case 0xb1: TRACEI("cmpxchg reg, modrm");
                           READMODRM_MEM; CMPXCHG(modrm_reg, modrm_val,oz); break;

                case 0xb3: TRACEI("btr reg, modrm");
                           READMODRM; BTR(modrm_reg, modrm_val,oz); break;

                case 0xb6: TRACEI("movz modrm8, reg");
                           READMODRM; MOVZX(modrm_val, modrm_reg,8,oz); break;
                case 0xb7: TRACEI("movz modrm16, reg");
                           READMODRM; MOVZX(modrm_val, modrm_reg,16,oz); break;

#define GRP8(bit, val,z) \
    switch (modrm.opcode) { \
        case 4: TRACEI("bt"); BT(bit, val,z); break; \
        case 5: TRACEI("bts"); BTS(bit, val,z); break; \
        case 6: TRACEI("btr"); BTR(bit, val,z); break; \
        case 7: TRACEI("btc"); BTC(bit, val,z); break; \
        default: UNDEFINED; \
    }

                case 0xba: TRACEI("grp8 imm8, modrm");
                           READMODRM; READIMM8; GRP8(imm, modrm_val,oz); break;

#undef GRP8

                case 0xbb: TRACEI("btc reg, modrm");
                           READMODRM; BTC(modrm_reg, modrm_val,oz); break;
                case 0xbc: TRACEI("bsf modrm, reg");
                           READMODRM; BSF(modrm_val, modrm_reg,oz); break;
                case 0xbd: TRACEI("bsr modrm, reg");
                           READMODRM; BSR(modrm_val, modrm_reg,oz); break;

                case 0xbe: TRACEI("movs modrm8, reg");
                           READMODRM; MOVSX(modrm_val, modrm_reg,8,oz); break;
                case 0xbf: TRACEI("movs modrm16, reg");
                           READMODRM; MOVSX(modrm_val, modrm_reg,16,oz); break;

                case 0xc0: TRACEI("xadd reg8, modrm8");
                           READMODRM; XADD(modrm_reg, modrm_val,8); break;
                case 0xc1: TRACEI("xadd reg, modrm");
                           READMODRM; XADD(modrm_reg, modrm_val,oz); break;

#if OP_SIZE != 16
                case 0xc8: TRACEI("bswap eax");
                           BSWAP(reg_a); break;
                case 0xc9: TRACEI("bswap ecx");
                           BSWAP(reg_c); break;
                case 0xca: TRACEI("bswap edx");
                           BSWAP(reg_d); break;
                case 0xcb: TRACEI("bswap ebx");
                           BSWAP(reg_b); break;
                case 0xcc: TRACEI("bswap esp");
                           BSWAP(reg_sp); break;
                case 0xcd: TRACEI("bswap ebp");
                           BSWAP(reg_bp); break;
                case 0xce: TRACEI("bswap esi");
                           BSWAP(reg_si); break;
                case 0xcf: TRACEI("bswap edi");
                           BSWAP(reg_di); break;
#endif

                default: TRACEI("undefined");
                         UNDEFINED;
            }
            break;

        MAKE_OP(0x10, ADC, "adc");
        MAKE_OP(0x18, SBB, "sbb");
        MAKE_OP(0x20, AND, "and");
        MAKE_OP(0x28, SUB, "sub");

        case 0x2e: TRACEI("segment cs (ignoring)"); goto restart;

        MAKE_OP(0x30, XOR, "xor");
        MAKE_OP(0x38, CMP, "cmp");

        case 0x3e: TRACEI("segment ds (useless)"); goto restart;

        case 0x40: TRACEI("inc oax"); INC(reg_a,oz); break;
        case 0x41: TRACEI("inc ocx"); INC(reg_c,oz); break;
        case 0x42: TRACEI("inc odx"); INC(reg_d,oz); break;
        case 0x43: TRACEI("inc obx"); INC(reg_b,oz); break;
        case 0x44: TRACEI("inc osp"); INC(reg_sp,oz); break;
        case 0x45: TRACEI("inc obp"); INC(reg_bp,oz); break;
        case 0x46: TRACEI("inc osi"); INC(reg_si,oz); break;
        case 0x47: TRACEI("inc odi"); INC(reg_di,oz); break;
        case 0x48: TRACEI("dec oax"); DEC(reg_a,oz); break;
        case 0x49: TRACEI("dec ocx"); DEC(reg_c,oz); break;
        case 0x4a: TRACEI("dec odx"); DEC(reg_d,oz); break;
        case 0x4b: TRACEI("dec obx"); DEC(reg_b,oz); break;
        case 0x4c: TRACEI("dec osp"); DEC(reg_sp,oz); break;
        case 0x4d: TRACEI("dec obp"); DEC(reg_bp,oz); break;
        case 0x4e: TRACEI("dec osi"); DEC(reg_si,oz); break;
        case 0x4f: TRACEI("dec odi"); DEC(reg_di,oz); break;

        case 0x50: TRACEI("push oax"); PUSH(reg_a,oz); break;
        case 0x51: TRACEI("push ocx"); PUSH(reg_c,oz); break;
        case 0x52: TRACEI("push odx"); PUSH(reg_d,oz); break;
        case 0x53: TRACEI("push obx"); PUSH(reg_b,oz); break;
        case 0x54: TRACEI("push osp"); PUSH(reg_sp,oz); break;
        case 0x55: TRACEI("push obp"); PUSH(reg_bp,oz); break;
        case 0x56: TRACEI("push osi"); PUSH(reg_si,oz); break;
        case 0x57: TRACEI("push odi"); PUSH(reg_di,oz); break;

        case 0x58: TRACEI("pop oax"); POP(reg_a,oz); break;
        case 0x59: TRACEI("pop ocx"); POP(reg_c,oz); break;
        case 0x5a: TRACEI("pop odx"); POP(reg_d,oz); break;
        case 0x5b: TRACEI("pop obx"); POP(reg_b,oz); break;
        case 0x5c: TRACEI("pop osp"); POP(reg_sp,oz); break;
        case 0x5d: TRACEI("pop obp"); POP(reg_bp,oz); break;
        case 0x5e: TRACEI("pop osi"); POP(reg_si,oz); break;
        case 0x5f: TRACEI("pop odi"); POP(reg_di,oz); break;

        case 0x65: TRACE("segment gs\n"); SEG_GS(); goto restart;

        case 0x66:
#if OP_SIZE == 32
            TRACE("entering 16 bit mode\n");
            return glue(DECODER_NAME, 16)(DECODER_PASS_ARGS);
#else
            TRACE("entering 32 bit mode\n");
            return glue(DECODER_NAME, 32)(DECODER_PASS_ARGS);
#endif

        case 0x67: TRACEI("address size prefix (ignored)"); goto restart;

        case 0x68: TRACEI("push imm\t");
                   READIMM; PUSH(imm,oz); break;
        case 0x69: TRACEI("imul imm\t");
                   READMODRM; READIMM; IMUL3(imm, modrm_val, modrm_reg,oz); break;
        case 0x6a: TRACEI("push imm8\t");
                   READIMM8; PUSH(imm,oz); break;
        case 0x6b: TRACEI("imul imm8\t");
                   READMODRM; READIMM8; IMUL3(imm, modrm_val, modrm_reg,oz); break;

        case 0x70: TRACEI("jo rel8\t");
                   READIMM8; J_REL(O, imm); break;
        case 0x71: TRACEI("jno rel8\t");
                   READIMM8; JN_REL(O, imm); break;
        case 0x72: TRACEI("jb rel8\t");
                   READIMM8; J_REL(B, imm); break;
        case 0x73: TRACEI("jnb rel8\t");
                   READIMM8; JN_REL(B, imm); break;
        case 0x74: TRACEI("je rel8\t");
                   READIMM8; J_REL(E, imm); break;
        case 0x75: TRACEI("jne rel8\t");
                   READIMM8; JN_REL(E, imm); break;
        case 0x76: TRACEI("jbe rel8\t");
                   READIMM8; J_REL(BE, imm); break;
        case 0x77: TRACEI("ja rel8\t");
                   READIMM8; JN_REL(BE, imm); break;
        case 0x78: TRACEI("js rel8\t");
                   READIMM8; J_REL(S, imm); break;
        case 0x79: TRACEI("jns rel8\t");
                   READIMM8; JN_REL(S, imm); break;
        case 0x7a: TRACEI("jp rel8\t");
                   READIMM8; J_REL(P, imm); break;
        case 0x7b: TRACEI("jnp rel8\t");
                   READIMM8; JN_REL(P, imm); break;
        case 0x7c: TRACEI("jl rel8\t");
                   READIMM8; J_REL(L, imm); break;
        case 0x7d: TRACEI("jnl rel8\t");
                   READIMM8; JN_REL(L, imm); break;
        case 0x7e: TRACEI("jle rel8\t");
                   READIMM8; J_REL(LE, imm); break;
        case 0x7f: TRACEI("jnle rel8\t");
                   READIMM8; JN_REL(LE, imm); break;

#define GRP1(src, dst,z) \
    switch (modrm.opcode) { \
        case 0: TRACE("add"); \
                ADD(src, dst,z); break; \
        case 1: TRACE("or"); \
                OR(src, dst,z); break; \
        case 2: TRACE("adc"); \
                ADC(src, dst,z); break; \
        case 3: TRACE("sbb"); \
                SBB(src, dst,z); break; \
        case 4: TRACE("and"); \
                AND(src, dst,z); break; \
        case 5: TRACE("sub"); \
                SUB(src, dst,z); break; \
        case 6: TRACE("xor"); \
                XOR(src, dst,z); break; \
        case 7: TRACE("cmp"); \
                CMP(src, dst,z); break; \
        default: TRACE("undefined"); \
                 UNDEFINED; \
    }

        case 0x80: TRACEI("grp1 imm8, modrm8");
                   READMODRM; READIMM8; GRP1(imm, modrm_val,8); break;
        case 0x81: TRACEI("grp1 imm, modrm");
                   READMODRM; READIMM; GRP1(imm, modrm_val,oz); break;
        case 0x83: TRACEI("grp1 imm8, modrm");
                   READMODRM; READIMM8; GRP1(imm, modrm_val,oz); break;

#undef GRP1

        case 0x84: TRACEI("test reg8, modrm8");
                   READMODRM; TEST(modrm_reg, modrm_val,8); break;
        case 0x85: TRACEI("test reg, modrm");
                   READMODRM; TEST(modrm_reg, modrm_val,oz); break;

        case 0x86: TRACEI("xchg reg8, modrm8");
                   READMODRM; XCHG(modrm_reg, modrm_val,8); break;
        case 0x87: TRACEI("xchg reg, modrm");
                   READMODRM; XCHG(modrm_reg, modrm_val,oz); break;

        case 0x88: TRACEI("mov reg8, modrm8");
                   READMODRM; MOV(modrm_reg, modrm_val,8); break;
        case 0x89: TRACEI("mov reg, modrm");
                   READMODRM; MOV(modrm_reg, modrm_val,oz); break;
        case 0x8a: TRACEI("mov modrm8, reg8");
                   READMODRM; MOV(modrm_val, modrm_reg,8); break;
        case 0x8b: TRACEI("mov modrm, reg");
                   READMODRM; MOV(modrm_val, modrm_reg,oz); break;

        case 0x8d: TRACEI("lea\t\t"); READMODRM_MEM;
                   MOV(addr, modrm_reg,oz); break;

        // only gs is supported, and it does nothing
        // see comment in sys/tls.c
        case 0x8c: TRACEI("mov seg, modrm\t"); READMODRM;
            if (modrm.reg != reg_ebp) UNDEFINED;
            MOV(gs, modrm_val,16); break;
        case 0x8e: TRACEI("mov modrm, seg\t"); READMODRM;
            if (modrm.reg != reg_ebp) UNDEFINED;
            MOV(modrm_val, gs,16); break;

        case 0x8f: TRACEI("pop modrm");
                   READMODRM; POP(modrm_val,oz); break;

        case 0x90: TRACEI("nop"); break;
        case 0x91: TRACEI("xchg ocx, oax");
                   XCHG(reg_c, reg_a,oz); break;
        case 0x92: TRACEI("xchg odx, oax");
                   XCHG(reg_d, reg_a,oz); break;
        case 0x93: TRACEI("xchg obx, oax");
                   XCHG(reg_b, reg_a,oz); break;
        case 0x94: TRACEI("xchg osp, oax");
                   XCHG(reg_sp, reg_a,oz); break;
        case 0x95: TRACEI("xchg obp, oax");
                   XCHG(reg_bp, reg_a,oz); break;
        case 0x96: TRACEI("xchg osi, oax");
                   XCHG(reg_si, reg_a,oz); break;
        case 0x97: TRACEI("xchg odi, oax");
                   XCHG(reg_di, reg_a,oz); break;

        case 0x98: TRACEI("cvte"); CVTE; break;
        case 0x99: TRACEI("cvt"); CVT; break;

        case 0x9b: TRACEI("fwait (ignored)"); break;

        case 0x9c: TRACEI("pushf"); PUSHF(); break;
        case 0x9d: TRACEI("popf"); POPF(); break;
        case 0x9e: TRACEI("sahf\t\t"); SAHF; break;

        case 0xa0: TRACEI("mov mem, al\t");
                   READADDR; MOV(mem_addr, reg_a,8); break;
        case 0xa1: TRACEI("mov mem, eax\t");
                   READADDR; MOV(mem_addr, reg_a,oz); break;
        case 0xa2: TRACEI("mov al, mem\t");
                   READADDR; MOV(reg_a, mem_addr,8); break;
        case 0xa3: TRACEI("mov oax, mem\t");
                   READADDR; MOV(reg_a, mem_addr,oz); break;

        case 0xa4: TRACEI("movsb"); STR(movs, 8); break;
        case 0xa5: TRACEI("movs"); STR(movs, oz); break;
        case 0xa6: TRACEI("cmpsb"); STR(cmps, 8); break;
        case 0xa7: TRACEI("cmps"); STR(cmps, oz); break;

        case 0xa8: TRACEI("test imm8, al");
                   READIMM8; TEST(imm, reg_a,8); break;
        case 0xa9: TRACEI("test imm, oax");
                   READIMM; TEST(imm, reg_a,oz); break;

        case 0xaa: TRACEI("stosb"); STR(stos, 8); break;
        case 0xab: TRACEI("stos"); STR(stos, oz); break;
        case 0xac: TRACEI("lodsb"); STR(lods, 8); break;
        case 0xad: TRACEI("lods"); STR(lods, oz); break;
        case 0xae: TRACEI("scasb"); STR(scas, 8); break;
        case 0xaf: TRACEI("scas"); STR(scas, oz); break;

        case 0xb0: TRACEI("mov imm, al\t");
                   READIMM8; MOV(imm, reg_a,8); break;
        case 0xb1: TRACEI("mov imm, cl\t");
                   READIMM8; MOV(imm, reg_c,8); break;
        case 0xb2: TRACEI("mov imm, dl\t");
                   READIMM8; MOV(imm, reg_d,8); break;
        case 0xb3: TRACEI("mov imm, bl\t");
                   READIMM8; MOV(imm, reg_b,8); break;
        case 0xb4: TRACEI("mov imm, ah\t");
                   READIMM8; MOV(imm, reg_ah,8); break;
        case 0xb5: TRACEI("mov imm, ch\t");
                   READIMM8; MOV(imm, reg_ch,8); break;
        case 0xb6: TRACEI("mov imm, dh\t");
                   READIMM8; MOV(imm, reg_dh,8); break;
        case 0xb7: TRACEI("mov imm, bh\t");
                   READIMM8; MOV(imm, reg_bh,8); break;

        case 0xb8: TRACEI("mov imm, oax\t");
                   READIMM; MOV(imm, reg_a,oz); break;
        case 0xb9: TRACEI("mov imm, ocx\t");
                   READIMM; MOV(imm, reg_c,oz); break;
        case 0xba: TRACEI("mov imm, odx\t");
                   READIMM; MOV(imm, reg_d,oz); break;
        case 0xbb: TRACEI("mov imm, obx\t");
                   READIMM; MOV(imm, reg_b,oz); break;
        case 0xbc: TRACEI("mov imm, osp\t");
                   READIMM; MOV(imm, reg_sp,oz); break;
        case 0xbd: TRACEI("mov imm, obp\t");
                   READIMM; MOV(imm, reg_bp,oz); break;
        case 0xbe: TRACEI("mov imm, osi\t");
                   READIMM; MOV(imm, reg_si,oz); break;
        case 0xbf: TRACEI("mov imm, odi\t");
                   READIMM; MOV(imm, reg_di,oz); break;

#define GRP2(count, val,z) \
    switch (modrm.opcode) { \
        case 0: TRACE("rol"); ROL(count, val,z); break; \
        case 1: TRACE("ror"); ROR(count, val,z); break; \
        case 2: TRACE("rcl"); RCL(count, val,z); break; \
        case 3: TRACE("rcr"); RCR(count, val,z); break; \
        case 4: \
        case 6: TRACE("shl"); SHL(count, val,z); break; \
        case 5: TRACE("shr"); SHR(count, val,z); break; \
        case 7: TRACE("sar"); SAR(count, val,z); break; \
    }

        case 0xc0: TRACEI("grp2 imm8, modrm8");
                   READMODRM; READIMM8; GRP2(imm, modrm_val,8); break;
        case 0xc1: TRACEI("grp2 imm8, modrm");
                   READMODRM; READIMM8; GRP2(imm, modrm_val,oz); break;

        case 0xc2: TRACEI("ret near imm\t");
                   READIMM16; RET_NEAR(imm); break;
        case 0xc3: TRACEI("ret near");
                   RET_NEAR(0); break;

        case 0xc9: TRACEI("leave");
                   MOV(reg_bp, reg_sp,oz); POP(reg_bp,oz); break;

        case 0xcd: TRACEI("int imm8\t");
                   READIMM8; INT(imm); break;

        case 0xc6: TRACEI("mov imm8, modrm8");
                   READMODRM; READIMM8; MOV(imm, modrm_val,8); break;
        case 0xc7: TRACEI("mov imm, modrm");
                   READMODRM; READIMM; MOV(imm, modrm_val,oz); break;

        case 0xd0: TRACEI("grp2 1, modrm8");
                   READMODRM; GRP2(1, modrm_val,8); break;
        case 0xd1: TRACEI("grp2 1, modrm");
                   READMODRM; GRP2(1, modrm_val,oz); break;
        case 0xd2: TRACEI("grp2 cl, modrm8");
                   READMODRM; GRP2(reg_c, modrm_val,8); break;
        case 0xd3: TRACEI("grp2 cl, modrm");
                   READMODRM; GRP2(reg_c, modrm_val,oz); break;

#undef GRP2

        case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
            TRACEI("fpu\t\t"); READMODRM;
            if (modrm.type != modrm_reg) {
                switch (insn << 4 | modrm.opcode) {
                    case 0xd80: TRACE("fadd mem32"); FADDM(mem_addr_real,32); break;
                    case 0xd81: TRACE("fmul mem32"); FMULM(mem_addr_real,32); break;
                    case 0xd82: TRACE("fcom mem32"); FCOMM(mem_addr_real,32); break;
                    case 0xd83: TRACE("fcomp mem32"); FCOMM(mem_addr_real,32); FPOP; break;
                    case 0xd84: TRACE("fsub mem32"); FSUBM(mem_addr_real,32); break;
                    case 0xd85: TRACE("fsubr mem32"); FSUBRM(mem_addr_real,32); break;
                    case 0xd86: TRACE("fdiv mem32"); FDIVM(mem_addr_real,32); break;
                    case 0xd87: TRACE("fdivr mem32"); FDIVRM(mem_addr_real,32); break;
                    case 0xd90: TRACE("fld mem32"); FLDM(mem_addr_real,32); break;
                    case 0xd92: TRACE("fst mem32"); FSTM(mem_addr_real,32); break;
                    case 0xd93: TRACE("fstp mem32"); FSTM(mem_addr_real,32); FPOP; break;
                    case 0xd95: TRACE("fldcw mem16"); FLDCW(mem_addr); break;
                    case 0xd97: TRACE("fnstcw mem16"); FSTCW(mem_addr); break;
                    case 0xda0: TRACE("fiadd mem32"); FIADD(mem_addr,32); break;
                    case 0xda1: TRACE("fimul mem32"); FIMUL(mem_addr,32); break;
                    case 0xda4: TRACE("fisub mem32"); FISUB(mem_addr,32); break;
                    case 0xda5: TRACE("fisubr mem32"); FISUBR(mem_addr,32); break;
                    case 0xda6: TRACE("fidiv mem32"); FIDIV(mem_addr,32); break;
                    case 0xda7: TRACE("fidivr mem32"); FIDIVR(mem_addr,32); break;
                    case 0xdb0: TRACE("fild mem32"); FILD(mem_addr,32); break;
                    case 0xdb2: TRACE("fist mem32"); FIST(mem_addr,32); break;
                    case 0xdb3: TRACE("fistp mem32"); FIST(mem_addr,32); FPOP; break;
                    case 0xdb5: TRACE("fld mem80"); FLDM(mem_addr_real,80); break;
                    case 0xdb7: TRACE("fstp mem80"); FSTM(mem_addr_real,80); FPOP; break;
                    case 0xdc0: TRACE("fadd mem64"); FADDM(mem_addr_real,64); break;
                    case 0xdc1: TRACE("fmul mem64"); FMULM(mem_addr_real,64); break;
                    case 0xdc2: TRACE("fcom mem64"); FCOMM(mem_addr_real,64); break;
                    case 0xdc3: TRACE("fcomp mem64"); FCOMM(mem_addr_real,64); FPOP; break;
                    case 0xdc4: TRACE("fsub mem64"); FSUBM(mem_addr_real,64); break;
                    case 0xdc5: TRACE("fsubr mem64"); FSUBRM(mem_addr_real,64); break;
                    case 0xdc6: TRACE("fdiv mem64"); FDIVM(mem_addr_real,64); break;
                    case 0xdc7: TRACE("fdivr mem64"); FDIVRM(mem_addr_real,64); break;
                    case 0xdd0: TRACE("fld mem64"); FLDM(mem_addr_real,64); break;
                    case 0xdd2: TRACE("fst mem64"); FSTM(mem_addr_real,64); break;
                    case 0xdd3: TRACE("fstp mem64"); FSTM(mem_addr_real,64); FPOP; break;
                    case 0xde0: TRACE("fiadd mem16"); FIADD(mem_addr,16); break;
                    case 0xde1: TRACE("fimul mem16"); FIMUL(mem_addr,16); break;
                    case 0xde4: TRACE("fisub mem16"); FISUB(mem_addr,16); break;
                    case 0xde5: TRACE("fisubr mem16"); FISUBR(mem_addr,16); break;
                    case 0xde6: TRACE("fidiv mem16"); FIDIV(mem_addr,16); break;
                    case 0xde7: TRACE("fidivr mem16"); FIDIVR(mem_addr,16); break;
                    case 0xdf0: TRACE("fild mem16"); FILD(mem_addr,16); break;
                    case 0xdf3: TRACE("fistp mem16"); FIST(mem_addr,16); FPOP; break;
                    case 0xdf5: TRACE("fild mem64"); FILD(mem_addr,64); break;
                    case 0xdf7: TRACE("fistp mem64"); FIST(mem_addr,64); FPOP; break;
                    default: TRACE("undefined"); UNDEFINED;
                }
            } else {
                switch (insn << 4 | modrm.opcode) {
                    case 0xd80: TRACE("fadd st(i), st"); FADD(st_i, st_0); break;
                    case 0xd81: TRACE("fmul st(i), st"); FMUL(st_i, st_0); break;
                    case 0xd82: TRACE("fcom st"); FCOM(); break;
                    case 0xd83: TRACE("fcomp st"); FCOM(); FPOP; break;
                    case 0xd84: TRACE("fsub st(i), st"); FSUB(st_i, st_0); break;
                    case 0xd85: TRACE("fsubr st(i), st"); FSUBR(st_i, st_0); break;
                    case 0xd86: TRACE("fdiv st(i), st"); FDIV(st_i, st_0); break;
                    case 0xd87: TRACE("fdivr st(i), st"); FDIVR(st_i, st_0); break;
                    case 0xd90: TRACE("fld st(i)"); FLD(); break;
                    case 0xd91: TRACE("fxch st"); FXCH(); break;
                    case 0xdb5: TRACE("fucomi st"); FUCOMI(); break;
                    case 0xdb6: TRACE("fcomi st"); FCOMI(); break;
                    case 0xdc0: TRACE("fadd st, st(i)"); FADD(st_0, st_i); break;
                    case 0xdc1: TRACE("fmul st, st(i)"); FMUL(st_0, st_i); break;
                    case 0xdc4: TRACE("fsubr st, st(i)"); FSUBR(st_0, st_i); break;
                    case 0xdc5: TRACE("fsub st, st(i)"); FSUB(st_0, st_i); break;
                    case 0xdc6: TRACE("fdivr st, st(i)"); FDIVR(st_0, st_i); break;
                    case 0xdc7: TRACE("fdiv st, st(i)"); FDIV(st_0, st_i); break;
                    case 0xdd3: TRACE("fstp st"); FST(); FPOP; break;
                    case 0xdd4: TRACE("fucom st"); FUCOM(); break;
                    case 0xdd5: TRACE("fucomp st"); FUCOM(); FPOP; break;
                    case 0xda5: TRACE("fucompp st"); FUCOM(); FPOP; FPOP; break;
                    case 0xde0: TRACE("faddp st, st(i)"); FADD(st_0, st_i); FPOP; break;
                    case 0xde1: TRACE("fmulp st, st(i)"); FMUL(st_0, st_i); FPOP; break;
                    case 0xde4: TRACE("fsubrp st, st(i)"); FSUBR(st_0, st_i); FPOP; break;
                    case 0xde5: TRACE("fsubp st, st(i)"); FSUB(st_0, st_i); FPOP; break;
                    case 0xde6: TRACE("fdivrp st, st(i)"); FDIVR(st_0, st_i); FPOP; break;
                    case 0xde7: TRACE("fdivp st, st(i)"); FDIV(st_0, st_i); FPOP; break;
                    case 0xdf5: TRACE("fucomip st"); FUCOMI(); FPOP; break;
                    case 0xdf6: TRACE("fcomip st"); FCOMI(); FPOP; break;
                    default: switch (insn << 8 | modrm.opcode << 4 | modrm.rm_opcode) {
                    case 0xd940: TRACE("fchs"); FCHS(); break;
                    case 0xd941: TRACE("fabs"); FABS(); break;
                    case 0xd944: TRACE("ftst"); FTST(); break;
                    case 0xd945: TRACE("fxam"); FXAM(); break;
                    case 0xd950: TRACE("fld1"); FLDC(one); break;
                    case 0xd951: TRACE("fldl2t"); FLDC(log2t); break;
                    case 0xd952: TRACE("fldl2e"); FLDC(log2e); break;
                    case 0xd953: TRACE("fldpi"); FLDC(pi); break;
                    case 0xd954: TRACE("fldlg2"); FLDC(log2); break;
                    case 0xd955: TRACE("fldln2"); FLDC(ln2); break;
                    case 0xd956: TRACE("fldz"); FLDC(zero); break;
                    case 0xd960: TRACE("f2xm1"); F2XM1(); break;
                    case 0xd961: TRACE("fyl2x"); FYL2X(); break;
                    case 0xd963: TRACE("fpatan"); FPATAN(); break;
                    case 0xd970: TRACE("fprem"); FPREM(); break;
                    case 0xd972: TRACE("fsqrt"); FSQRT(); break;
                    case 0xd974: TRACE("frndint"); FRNDINT(); break;
                    case 0xd975: TRACE("fscale"); FSCALE(); break;
                    case 0xde31: TRACE("fcompp"); FCOM(); FPOP; FPOP; break;
                    case 0xdf40: TRACE("fnstsw ax"); FSTSW(reg_a); break;
                    default: TRACE("undefined"); UNDEFINED;
                }}
            }
            break;

        case 0xe3: TRACEI("jcxz rel8\t");
                   READIMM8; JCXZ_REL(imm); break;

        case 0xe8: TRACEI("call near\t");
                   READIMM; CALL_REL(imm); break;

        case 0xe9: TRACEI("jmp rel\t");
                   READIMM; JMP_REL(imm); break;
        case 0xeb: TRACEI("jmp rel8\t");
                   READIMM8; JMP_REL(imm); break;

        // lock
        case 0xf0:
            lockrestart:
            READINSN;
            switch (insn) {
                case 0x65: TRACE("segment gs\n"); SEG_GS(); goto lockrestart;

                case 0x66:
                    // I didn't think this through
#if OP_SIZE == 32
                    TRACE("locked 16-bit mode\n");
                    RESTORE_IP;
                    return glue(DECODER_NAME, 16)(DECODER_PASS_ARGS);
#else
                    goto lockrestart;
#endif



#define MAKE_OP_ATOMIC(x, OP, op) \
        case x+0x0: TRACEI("lock " op " reg8, modrm8"); \
                   READMODRM_MEM; ATOMIC_##OP(modrm_reg, modrm_val,8); break; \
        case x+0x1: TRACEI("lock " op " reg, modrm"); \
                   READMODRM_MEM; ATOMIC_##OP(modrm_reg, modrm_val,oz); break; \

                MAKE_OP_ATOMIC(0x00, ADD, "add");
                MAKE_OP_ATOMIC(0x08, OR, "or");
                MAKE_OP_ATOMIC(0x10, ADC, "adc");
                MAKE_OP_ATOMIC(0x18, SBB, "sbb");
                MAKE_OP_ATOMIC(0x20, AND, "and");
                MAKE_OP_ATOMIC(0x28, SUB, "sub");
                MAKE_OP_ATOMIC(0x30, XOR, "xor");

#undef MAKE_OP_ATOMIC

#define GRP1_ATOMIC(src, dst,z) \
    switch (modrm.opcode) { \
        case 0: TRACE("lock add"); ATOMIC_ADD(src, dst,z); break; \
        case 1: TRACE("lock or");  ATOMIC_OR(src, dst,z); break; \
        case 2: TRACE("lock adc"); ATOMIC_ADC(src, dst,z); break; \
        case 3: TRACE("lock sbb"); ATOMIC_SBB(src, dst,z); break; \
        case 4: TRACE("lock and"); ATOMIC_AND(src, dst,z); break; \
        case 5: TRACE("lock sub"); ATOMIC_SUB(src, dst,z); break; \
        case 6: TRACE("lock xor"); ATOMIC_XOR(src, dst,z); break; \
        default: TRACE("undefined"); UNDEFINED; \
    }

                case 0x80: TRACEI("lock grp1 imm8, modrm8");
                           READMODRM_MEM; READIMM8; GRP1_ATOMIC(imm, modrm_val,8); break;
                case 0x81: TRACEI("lock grp1 imm, modrm");
                           READMODRM_MEM; READIMM; GRP1_ATOMIC(imm, modrm_val,oz); break;
                case 0x83: TRACEI("lock grp1 imm8, modrm");
                           READMODRM_MEM; READIMM8; GRP1_ATOMIC(imm, modrm_val,oz); break;

#undef GRP1_ATOMIC

                case 0x0f:
                    READINSN;
                    switch (insn) {
                        case 0xab: TRACEI("lock bts reg, modrm");
                                   READMODRM; ATOMIC_BTS(modrm_reg, modrm_val,oz); break;
                        case 0xb3: TRACEI("lock btr reg, modrm");
                                   READMODRM; ATOMIC_BTR(modrm_reg, modrm_val,oz); break;
                        case 0xbb: TRACEI("lock btc reg, modrm");
                                   READMODRM; ATOMIC_BTC(modrm_reg, modrm_val,oz); break;

#define GRP8_ATOMIC(bit, val,z) \
    switch (modrm.opcode) { \
        case 5: TRACEI("bts"); ATOMIC_BTS(bit, val,z); break; \
        case 6: TRACEI("btr"); ATOMIC_BTR(bit, val,z); break; \
        case 7: TRACEI("btc"); ATOMIC_BTC(bit, val,z); break; \
        default: UNDEFINED; \
    }
                        case 0xba: TRACEI("lock grp8 imm8, modrm");
                                   READMODRM; READIMM8; GRP8_ATOMIC(imm, modrm_val,oz); break;
#undef GRP8_ATOMIC

                        case 0xb0: TRACEI("lock cmpxchg reg8, modrm8");
                                   READMODRM_MEM; ATOMIC_CMPXCHG(modrm_reg, modrm_val,8); break;
                        case 0xb1: TRACEI("lock cmpxchg reg, modrm");
                                   READMODRM_MEM; ATOMIC_CMPXCHG(modrm_reg, modrm_val,oz); break;

                        case 0xc0: TRACEI("lock xadd reg8, modrm8");
                                   READMODRM_MEM; ATOMIC_XADD(modrm_reg, modrm_val,8); break;
                        case 0xc1: TRACEI("lock xadd reg, modrm");
                                   READMODRM_MEM; ATOMIC_XADD(modrm_reg, modrm_val,oz); break;
                        default: TRACE("undefined"); UNDEFINED;
                    }
                    break;

#define GRP5_ATOMIC(val,z) \
    switch (modrm.opcode) { \
        case 0: TRACE("lock inc"); ATOMIC_INC(val,z); break; \
        case 1: TRACE("lock dec"); ATOMIC_DEC(val,z); break; \
        default: TRACE("undefined"); UNDEFINED; \
    }

                case 0xfe: TRACEI("lock grp5 modrm8\t");
                           READMODRM_MEM; GRP5_ATOMIC(modrm_val,8); break;
                case 0xff: TRACEI("lock grp5 modrm\t");
                           READMODRM_MEM; GRP5_ATOMIC(modrm_val,oz); break;

#undef GRP5_ATOMIC

                default: TRACE("undefined"); UNDEFINED;
            }
            break;

        case 0xf2:
            READINSN;
            switch (insn) {
                case 0x0f:
                    READINSN;
                    switch (insn) {
                        case 0x10:
                            TRACEI("movsd xmm:modrm, xmm");
                            READMODRM; VLOAD(xmm_modrm_val, xmm_modrm_reg,64);
                            break;

                        case 0x18 ... 0x1f: TRACEI("rep nop modrm\t"); READMODRM; break;
                        default: TRACE("undefined"); UNDEFINED;
                    }
                    break;

                case 0xa6: TRACEI("repnz cmpsb"); REPNZ(cmps, 8); break;
                case 0xa7: TRACEI("repnz cmps"); REPNZ(cmps, oz); break;
                case 0xae: TRACEI("repnz scasb"); REPNZ(scas, 8); break;
                case 0xaf: TRACEI("repnz scas"); REPNZ(scas, oz); break;
                default: TRACE("undefined"); UNDEFINED;
            }
            break;

        case 0xf3:
            READINSN;
            switch (insn) {
                case 0x0f:
                    // 2-byte opcode prefix
                    // after a rep prefix, means we have sse/mmx insanity
                    READINSN;
                    switch (insn) {
                        case 0x18 ... 0x1f: TRACEI("repz nop modrm\t"); READMODRM; break;

                        // tzcnt is like bsf but the result when the input is zero is defined as the operand size
                        // for now, it can just be an alias
                        case 0xbc: TRACEI("~~tzcnt~~ bsf modrm, reg");
                                   READMODRM; BSF(modrm_val, modrm_reg,oz); break;
                        case 0xbd: TRACEI("~~lzcnt~~ bsr modrm, reg");
                                   READMODRM; BSR(modrm_val, modrm_reg,oz); break;

                        default: TRACE("undefined"); UNDEFINED;
                    }
                    break;

                case 0x90: TRACEI("pause"); break;

                case 0xa4: TRACEI("rep movsb"); REP(movs, 8); break;
                case 0xa5: TRACEI("rep movs"); REP(movs, oz); break;
                case 0xa6: TRACEI("repz cmpsb"); REPZ(cmps, 8); break;
                case 0xa7: TRACEI("repz cmps"); REPZ(cmps, oz); break;
                case 0xaa: TRACEI("rep stosb"); REP(stos, 8); break;
                case 0xab: TRACEI("rep stos"); REP(stos, oz); break;
                case 0xac: TRACEI("rep lodsb"); REP(lods, 8); break;
                case 0xad: TRACEI("rep lods"); REP(lods, oz); break;
                case 0xae: TRACEI("repz scasb"); REPZ(scas, 8); break;
                case 0xaf: TRACEI("repz scas"); REPZ(scas, oz); break;

                // repz ret is equivalent to ret but on some amd chips there's
                // a branch prediction penalty if the target of a branch is a
                // ret. gcc used to use nop ret but repz ret is only one
                // instruction
                case 0xc3: TRACEI("repz ret\t"); RET_NEAR(0); break;
                default: TRACE("undefined\n"); UNDEFINED;
            }
            break;

#define GRP3(val,z) \
    switch (modrm.opcode) { \
        case 0: \
        case 1: TRACE("test imm "); \
                READIMM##z; TEST(imm, val,z); break; \
        case 2: TRACE("not"); \
                NOT(val,z); break; \
        case 3: TRACE("neg"); \
                NEG(val,z); break; \
        case 4: TRACE("mul"); \
                MUL1(modrm_val,z); break; \
        case 5: TRACE("imul"); \
                IMUL1(modrm_val,z); break; \
        case 6: TRACE("div"); \
                DIV(modrm_val,z); break; \
        case 7: TRACE("idiv"); \
                IDIV(modrm_val,z); break; \
        default: TRACE("undefined"); UNDEFINED; \
    }

        case 0xf6: TRACEI("grp3 modrm8\t");
                   READMODRM; GRP3(modrm_val,8); break;
        case 0xf7: TRACEI("grp3 modrm\t");
                   READMODRM; GRP3(modrm_val,oz); break;

#undef GRP3

        case 0xfc: TRACEI("cld"); CLD; break;
        case 0xfd: TRACEI("std"); STD; break;

#define GRP5(val,z) \
    switch (modrm.opcode) { \
        case 0: TRACE("inc"); \
                INC(val,z); break; \
        case 1: TRACE("dec"); \
                DEC(val,z); break; \
        case 2: TRACE("call indirect near"); \
                CALL(val); break; \
        case 3: TRACE("call indirect far"); UNDEFINED; \
        case 4: TRACE("jmp indirect near"); \
                JMP(val); break; \
        case 5: TRACE("jmp indirect far"); UNDEFINED; \
        case 6: TRACE("push"); \
                PUSH(val,z); break; \
        case 7: TRACE("undefined"); UNDEFINED; \
    }

        case 0xfe: TRACEI("grp5 modrm8\t");
                   READMODRM; GRP5(modrm_val,8); break;
        case 0xff: TRACEI("grp5 modrm\t");
                   READMODRM; GRP5(modrm_val,oz); break;

#undef GRP5

        default:
            TRACE("undefined\n");
            UNDEFINED;
    }
    TRACE("\n");
    FINISH;
}
