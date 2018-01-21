#include "misc.h"
#include "emu/cpu.h"
#include "emu/modrm.h"
#include "emu/interrupt.h"
#include "kernel/calls.h"

#undef DEFAULT_CHANNEL
#define DEFAULT_CHANNEL instr
#define TRACEI(msg, ...) TRACE(msg "\t", ##__VA_ARGS__)

// this will be the next PyEval_EvalFrameEx.
int CONCAT(decoder_name, OP_SIZE)(struct cpu_state *cpu, struct tlb *tlb) {
    DECLARE_LOCALS;

    dword_t addr_offset = 0;
#define READADDR READIMM_(addr_offset, 32); addr += addr_offset

#define UNDEFINED { cpu->eip = saved_ip; return INT_UNDEFINED; }

restart:
    TRACE("%d %08x\t", current->pid, cpu->eip);
    READINSN;
    switch (insn) {
        // if any instruction handlers declare variables, they should create a
        // new block for those variables.
        // any subtraction that occurs probably needs to have a cast to a
        // signed type, so sign extension happens.

        case 0x00: TRACEI("add reg8, modrm8");
                   READMODRM; ADD(modrm_reg, modrm_val,8); break;
        case 0x01: TRACEI("add reg, modrm");
                   READMODRM; ADD(modrm_reg, modrm_val,); break;
        case 0x02: TRACEI("add modrm8, reg8");
                   READMODRM; ADD(modrm_val, modrm_reg,8); break;
        case 0x03: TRACEI("add modrm, reg");
                   READMODRM; ADD(modrm_val, modrm_reg,); break;
        case 0x05: TRACEI("add imm, oax\t");
                   READIMM; ADD(imm, oax,); break;

        case 0x08: TRACEI("or reg8, modrm8");
                   READMODRM; OR(modrm_reg, modrm_val,8); break;
        case 0x09: TRACEI("or reg, modrm");
                   READMODRM; OR(modrm_reg, modrm_val,); break;
        case 0x0a: TRACEI("or modrm8, reg8");
                   READMODRM; OR(modrm_val, modrm_reg,8); break;
        case 0x0b: TRACEI("or modrm, reg");
                   READMODRM; OR(modrm_val, modrm_reg,); break;
        case 0x0c: TRACEI("or imm8, al\t");
                   READIMM8; OR(imm8, al,8); break;
        case 0x0d: TRACEI("or imm, eax\t");
                   READIMM; OR(imm, oax,); break;

        case 0x0f:
            // 2-byte opcode prefix
            READINSN;
            switch (insn) {
                case 0x1f: TRACEI("nop modrm\t"); READMODRM; break;

                case 0x28: TRACEI("movp modrm, reg");
                           READMODRM; MOV(modrm_val, modrm_reg,128); break;
                case 0x29: TRACEI("movp reg, modrm");
                           READMODRM; MOV(modrm_reg, modrm_val,128); break;

                case 0x31: TRACEI("rdtsc");
                           // TODO there's a clang builtin for this
                           RDTSC; break;

                case 0x40: TRACEI("cmovo modrm, reg");
                           READMODRM; CMOV(O, modrm_val, modrm_reg,); break;
                case 0x41: TRACEI("cmovno modrm, reg");
                           READMODRM; CMOV(!O, modrm_val, modrm_reg,); break;
                case 0x42: TRACEI("cmovb modrm, reg");
                           READMODRM; CMOV(B, modrm_val, modrm_reg,); break;
                case 0x43: TRACEI("cmovnb modrm, reg");
                           READMODRM; CMOV(!B, modrm_val, modrm_reg,); break;
                case 0x44: TRACEI("cmove modrm, reg");
                           READMODRM; CMOV(E, modrm_val, modrm_reg,); break;
                case 0x45: TRACEI("cmovne modrm, reg");
                           READMODRM; CMOV(!E, modrm_val, modrm_reg,); break;
                case 0x46: TRACEI("cmovbe modrm, reg");
                           READMODRM; CMOV(BE, modrm_val, modrm_reg,); break;
                case 0x47: TRACEI("cmova modrm, reg");
                           READMODRM; CMOV(!BE, modrm_val, modrm_reg,); break;
                case 0x48: TRACEI("cmovs modrm, reg");
                           READMODRM; CMOV(S, modrm_val, modrm_reg,); break;
                case 0x49: TRACEI("cmovns modrm, reg");
                           READMODRM; CMOV(!S, modrm_val, modrm_reg,); break;
                case 0x4a: TRACEI("cmovp modrm, reg");
                           READMODRM; CMOV(P, modrm_val, modrm_reg,); break;
                case 0x4b: TRACEI("cmovnp modrm, reg");
                           READMODRM; CMOV(!P, modrm_val, modrm_reg,); break;
                case 0x4c: TRACEI("cmovl modrm, reg");
                           READMODRM; CMOV(L, modrm_val, modrm_reg,); break;
                case 0x4d: TRACEI("cmovnl modrm, reg");
                           READMODRM; CMOV(!L, modrm_val, modrm_reg,); break;
                case 0x4e: TRACEI("cmovle modrm, reg");
                           READMODRM; CMOV(LE, modrm_val, modrm_reg,); break;
                case 0x4f: TRACEI("cmovnle modrm, reg");
                           READMODRM; CMOV(!LE, modrm_val, modrm_reg,); break;

                case 0x57: TRACEI("xorps modrm, reg");
                           READMODRM; XORP(modrm_val, modrm_reg); break;
                case 0x73: TRACEI("psrlq imm8, reg");
                           // TODO I think this is actually a group
                           READMODRM; READIMM8; PSRLQ(imm8, modrm_val); break;
                case 0x76: TRACEI("pcmpeqd reg, modrm");
                           READMODRM; PCMPEQD(modrm_reg, modrm_val); break;
#if OP_SIZE == 16
                case 0x7e: TRACEI("movd xmm, modrm32");
                           READMODRM; MOVD(modrm_reg, modrm_val); break;
#endif

                case 0x80: TRACEI("jo rel\t");
                           READIMM; J_REL(O, imm); break;
                case 0x81: TRACEI("jno rel\t");
                           READIMM; J_REL(!O, imm); break;
                case 0x82: TRACEI("jb rel\t");
                           READIMM; J_REL(B, imm); break;
                case 0x83: TRACEI("jnb rel\t");
                           READIMM; J_REL(!B, imm); break;
                case 0x84: TRACEI("je rel\t");
                           READIMM; J_REL(E, imm); break;
                case 0x85: TRACEI("jne rel\t");
                           READIMM; J_REL(!E, imm); break;
                case 0x86: TRACEI("jbe rel\t");
                           READIMM; J_REL(BE, imm); break;
                case 0x87: TRACEI("ja rel\t");
                           READIMM; J_REL(!BE, imm); break;
                case 0x88: TRACEI("js rel\t");
                           READIMM; J_REL(S, imm); break;
                case 0x89: TRACEI("jns rel\t");
                           READIMM; J_REL(!S, imm); break;
                case 0x8a: TRACEI("jp rel\t");
                           READIMM; J_REL(P, imm); break;
                case 0x8b: TRACEI("jnp rel\t");
                           READIMM; J_REL(!P, imm); break;
                case 0x8c: TRACEI("jl rel\t");
                           READIMM; J_REL(L, imm); break;
                case 0x8d: TRACEI("jnl rel\t");
                           READIMM; J_REL(!L, imm); break;
                case 0x8e: TRACEI("jle rel\t");
                           READIMM; J_REL(LE, imm); break;
                case 0x8f: TRACEI("jnle rel\t");
                           READIMM; J_REL(!LE, imm); break;

                case 0x90: TRACEI("seto\t");
                           READMODRM; SET(O, modrm_val); break;
                case 0x91: TRACEI("setno\t");
                           READMODRM; SET(!O, modrm_val); break;
                case 0x92: TRACEI("setb\t");
                           READMODRM; SET(B, modrm_val); break;
                case 0x93: TRACEI("setnb\t");
                           READMODRM; SET(!B, modrm_val); break;
                case 0x94: TRACEI("sete\t");
                           READMODRM; SET(E, modrm_val); break;
                case 0x95: TRACEI("setne\t");
                           READMODRM; SET(!E, modrm_val); break;
                case 0x96: TRACEI("setbe\t");
                           READMODRM; SET(BE, modrm_val); break;
                case 0x97: TRACEI("setnbe\t");
                           READMODRM; SET(!BE, modrm_val); break;
                case 0x98: TRACEI("sets\t");
                           READMODRM; SET(S, modrm_val); break;
                case 0x99: TRACEI("setns\t");
                           READMODRM; SET(!S, modrm_val); break;
                case 0x9a: TRACEI("setp\t");
                           READMODRM; SET(P, modrm_val); break;
                case 0x9b: TRACEI("setnp\t");
                           READMODRM; SET(!P, modrm_val); break;
                case 0x9c: TRACEI("setl\t");
                           READMODRM; SET(L, modrm_val); break;
                case 0x9d: TRACEI("setnl\t");
                           READMODRM; SET(!L, modrm_val); break;
                case 0x9e: TRACEI("setle\t");
                           READMODRM; SET(LE, modrm_val); break;
                case 0x9f: TRACEI("setnle\t");
                           READMODRM; SET(!LE, modrm_val); break;

                case 0xa2:
                    TRACEI("cpuid");
                    do_cpuid(&cpu->eax, &cpu->ebx, &cpu->ecx, &cpu->edx);
                    break;

                case 0xa3: TRACEI("bt reg, modrm");
                           READMODRM; BT(modrm_reg, modrm_val,); break;

                case 0xa4: TRACEI("shld imm8, reg, modrm");
                           READMODRM; READIMM8; SHLD(imm8, modrm_reg, modrm_val,); break;
                case 0xa5: TRACEI("shld cl, reg, modrm");
                           READMODRM; SHLD(cl, modrm_reg, modrm_val,); break;

                case 0xab: TRACEI("bts reg, modrm");
                           READMODRM; BTS(modrm_reg, modrm_val,); break;

                case 0xac: TRACEI("shrd imm8, reg, modrm");
                           READMODRM; READIMM8; SHRD(imm8, modrm_reg, modrm_val,); break;
                case 0xad: TRACEI("shrd cl, reg, modrm");
                           READMODRM; SHRD(cl, modrm_reg, modrm_val,); break;

                case 0xaf: TRACEI("imul modrm, reg");
                           READMODRM; IMUL2(modrm_val, modrm_reg,); break;

                case 0xb1: TRACEI("cmpxchg reg, modrm");
                           READMODRM; CMPXCHG(modrm_reg, modrm_val,); break;

                case 0xb3: TRACEI("btr reg, modrm");
                           READMODRM; BTR(modrm_reg, modrm_val,); break;

                case 0xb6: TRACEI("movz modrm8, reg");
                           READMODRM; MOVZX(modrm_val, modrm_reg,8,); break;
                case 0xb7: TRACEI("movz modrm16, reg");
                           READMODRM; MOVZX(modrm_val, modrm_reg,16,); break;

#define GRP8(bit, val,z) \
    switch(modrm.opcode) { \
        case 4: TRACEI("bt"); BT(bit, val,z); break; \
        case 5: TRACEI("bts"); BTS(bit, val,z); break; \
        case 6: TRACEI("btr"); BTR(bit, val,z); break; \
        case 7: TRACEI("btc"); BTC(bit, val,z); break; \
        default: UNDEFINED; \
    }

                case 0xba: TRACEI("grp8 imm8, modrm");
                           READMODRM; READIMM8; GRP8(imm8, modrm_val,); break;

#undef GRP8

                case 0xbb: TRACEI("btc reg, modrm");
                           READMODRM; BTC(modrm_reg, modrm_val,); break;
                case 0xbc: TRACEI("bsf modrm, reg");
                           READMODRM; BSF(modrm_val, modrm_reg,); break;
                case 0xbd: TRACEI("bsr modrm, reg");
                           READMODRM; BSR(modrm_val, modrm_reg,); break;

                case 0xbe: TRACEI("movs modrm8, reg");
                           READMODRM; MOVSX(modrm_val, modrm_reg,8,); break;
                case 0xbf: TRACEI("movs modrm16, reg");
                           READMODRM; MOVSX(modrm_val, modrm_reg,16,); break;

                case 0xc0: TRACEI("xadd reg8, modrm8");
                           READMODRM; XADD(modrm_reg, modrm_val,8); break;
                case 0xc1: TRACEI("xadd reg, modrm");
                           READMODRM; XADD(modrm_reg, modrm_val,); break;

                case 0xc8: TRACEI("bswap eax");
                           BSWAP(eax); break;
                case 0xc9: TRACEI("bswap ecx");
                           BSWAP(ecx); break;
                case 0xca: TRACEI("bswap edx");
                           BSWAP(edx); break;
                case 0xcb: TRACEI("bswap ebx");
                           BSWAP(ebx); break;
                case 0xcc: TRACEI("bswap esp");
                           BSWAP(esp); break;
                case 0xcd: TRACEI("bswap ebp");
                           BSWAP(ebp); break;
                case 0xce: TRACEI("bswap esi");
                           BSWAP(esi); break;
                case 0xcf: TRACEI("bswap edi");
                           BSWAP(edi); break;

#if OP_SIZE == 16
                case 0xd4: TRACEI("paddq modrm, reg");
                           READMODRM; PADD(modrm_val, modrm_reg); break;
                case 0xd6: TRACEI("movq xmm, modrm");
                           READMODRM; MOVQ(modrm_reg, modrm_val); break;
#endif

                case 0xfb: TRACEI("psubq modrm, reg");
                           READMODRM; PSUB(modrm_val, modrm_reg); break;
                default: TRACEI("undefined");
                         UNDEFINED;
            }
            break;

        case 0x10: TRACEI("adc reg8, modrm8");
                   READMODRM; ADC(modrm_reg, modrm_val,8); break;
        case 0x11: TRACEI("adc reg, modrm");
                   READMODRM; ADC(modrm_reg, modrm_val,); break;
        case 0x13: TRACEI("adc modrm, reg");
                   READMODRM; ADC(modrm_val, modrm_reg,); break;

        case 0x19: TRACEI("sbb reg, modrm");
                   READMODRM; SBB(modrm_reg, modrm_val,); break;
        case 0x1b: TRACEI("sbb modrm, reg");
                   READMODRM; SBB(modrm_val, modrm_reg,); break;

        case 0x20: TRACEI("and reg8, modrm8");
                   READMODRM; AND(modrm_reg, modrm_val,8); break;
        case 0x21: TRACEI("and reg, modrm");
                   READMODRM; AND(modrm_reg, modrm_val,); break;
        case 0x22: TRACEI("and modrm8, reg8");
                   READMODRM; AND(modrm_val, modrm_reg,8); break;
        case 0x23: TRACEI("and modrm, reg");
                   READMODRM; AND(modrm_val, modrm_reg,); break;
        case 0x24: TRACEI("and imm8, al\t");
                   READIMM8; AND(imm8, al,8); break;
        case 0x25: TRACEI("and imm, oax\t");
                   READIMM; AND(imm, oax,); break;

        case 0x28: TRACEI("sub reg8, modrm8");
                   READMODRM; SUB(modrm_reg, modrm_val,8); break;
        case 0x29: TRACEI("sub reg, modrm");
                   READMODRM; SUB(modrm_reg, modrm_val,); break;
        case 0x2a: TRACEI("sub modrm8, reg8");
                   READMODRM; SUB(modrm_val, modrm_reg,8); break;
        case 0x2b: TRACEI("sub modrm, reg");
                   READMODRM; SUB(modrm_val, modrm_reg,); break;
        case 0x2d: TRACEI("sub imm, oax\t");
                   READIMM; SUB(imm, oax,); break;

        case 0x2e: TRACEI("segment cs (ignoring)"); goto restart;

        case 0x30: TRACEI("xor reg8, modrm8");
                   READMODRM; XOR(modrm_reg, modrm_val,8); break;
        case 0x31: TRACEI("xor reg, modrm");
                   READMODRM; XOR(modrm_reg, modrm_val,); break;
        case 0x32: TRACEI("xor modrm8, reg8");
                   READMODRM; XOR(modrm_val, modrm_reg,8); break;
        case 0x33: TRACEI("xor modrm, reg");
                   READMODRM; XOR(modrm_val, modrm_reg,); break;
        case 0x34: TRACEI("xor imm8, al\t");
                   READIMM8; XOR(imm8, al,8); break;
        case 0x35: TRACEI("xor imm, oax");
                   READIMM; XOR(imm, oax,); break;

        case 0x38: TRACEI("cmp reg8, modrm8");
                   READMODRM; CMP(modrm_reg, modrm_val,8); break;
        case 0x39: TRACEI("cmp reg, modrm");
                   READMODRM; CMP(modrm_reg, modrm_val,); break;
        case 0x3a: TRACEI("cmp modrm8, reg8");
                   READMODRM; CMP(modrm_val, modrm_reg,8); break;
        case 0x3b: TRACEI("cmp modrm, reg");
                   READMODRM; CMP(modrm_val, modrm_reg,); break;
        case 0x3c: TRACEI("cmp imm8, al\t");
                   READIMM8; CMP(imm8, al,8); break;
        case 0x3d: TRACEI("cmp imm, oax\t");
                   READIMM; CMP(imm, oax,); break;

        case 0x40: TRACEI("inc oax"); INC(oax,); break;
        case 0x41: TRACEI("inc ocx"); INC(ocx,); break;
        case 0x42: TRACEI("inc odx"); INC(odx,); break;
        case 0x43: TRACEI("inc obx"); INC(obx,); break;
        case 0x44: TRACEI("inc osp"); INC(osp,); break;
        case 0x45: TRACEI("inc obp"); INC(obp,); break;
        case 0x46: TRACEI("inc osi"); INC(osi,); break;
        case 0x47: TRACEI("inc odi"); INC(odi,); break;
        case 0x48: TRACEI("dec oax"); DEC(oax,); break;
        case 0x49: TRACEI("dec ocx"); DEC(ocx,); break;
        case 0x4a: TRACEI("dec odx"); DEC(odx,); break;
        case 0x4b: TRACEI("dec obx"); DEC(obx,); break;
        case 0x4c: TRACEI("dec osp"); DEC(osp,); break;
        case 0x4d: TRACEI("dec obp"); DEC(obp,); break;
        case 0x4e: TRACEI("dec osi"); DEC(osi,); break;
        case 0x4f: TRACEI("dec odi"); DEC(odi,); break;

        case 0x50: TRACEI("push oax"); PUSH(oax); break;
        case 0x51: TRACEI("push ocx"); PUSH(ocx); break;
        case 0x52: TRACEI("push odx"); PUSH(odx); break;
        case 0x53: TRACEI("push obx"); PUSH(obx); break;
        case 0x54: TRACEI("push osp"); PUSH(osp); break;
        case 0x55: TRACEI("push obp"); PUSH(obp); break;
        case 0x56: TRACEI("push osi"); PUSH(osi); break;
        case 0x57: TRACEI("push odi"); PUSH(odi); break;

        case 0x58: TRACEI("pop oax"); POP(oax); break;
        case 0x59: TRACEI("pop ocx"); POP(ocx); break;
        case 0x5a: TRACEI("pop odx"); POP(odx); break;
        case 0x5b: TRACEI("pop obx"); POP(obx); break;
        case 0x5c: TRACEI("pop osp"); POP(osp); break;
        case 0x5d: TRACEI("pop obp"); POP(obp); break;
        case 0x5e: TRACEI("pop osi"); POP(osi); break;
        case 0x5f: TRACEI("pop odi"); POP(odi); break;

        case 0x65: TRACELN("segment gs");
                   addr += cpu->tls_ptr; goto restart;

        case 0x66:
#if OP_SIZE == 32
            TRACELN("entering 16 bit mode");
            return cpu_step16(cpu, tlb);
#else
            TRACELN("entering 32 bit mode");
            return cpu_step32(cpu, tlb);
#endif

        case 0x67: TRACEI("address size prefix (ignored)"); goto restart;

        case 0x68: TRACEI("push imm\t");
                   READIMM; PUSH(imm); break;
        case 0x69: TRACEI("imul imm\t");
                   READMODRM; READIMM; IMUL3(imm, modrm_val, modrm_reg,); break;
        case 0x6a: TRACEI("push imm8\t");
                   READIMM8; PUSH(imm8); break;
        case 0x6b: TRACEI("imul imm8\t");
                   READMODRM; READIMM8; IMUL3(imm8, modrm_val, modrm_reg,); break;

        case 0x70: TRACEI("jo rel8\t");
                   READIMM8; J_REL(O, imm8); break;
        case 0x71: TRACEI("jno rel8\t");
                   READIMM8; J_REL(!O, imm8); break;
        case 0x72: TRACEI("jb rel8\t");
                   READIMM8; J_REL(B, imm8); break;
        case 0x73: TRACEI("jnb rel8\t");
                   READIMM8; J_REL(!B, imm8); break;
        case 0x74: TRACEI("je rel8\t");
                   READIMM8; J_REL(E, imm8); break;
        case 0x75: TRACEI("jne rel8\t");
                   READIMM8; J_REL(!E, imm8); break;
        case 0x76: TRACEI("jbe rel8\t");
                   READIMM8; J_REL(BE, imm8); break;
        case 0x77: TRACEI("ja rel8\t");
                   READIMM8; J_REL(!BE, imm8); break;
        case 0x78: TRACEI("js rel8\t");
                   READIMM8; J_REL(S, imm8); break;
        case 0x79: TRACEI("jns rel8\t");
                   READIMM8; J_REL(!S, imm8); break;
        case 0x7a: TRACEI("jp rel8\t");
                   READIMM8; J_REL(P, imm8); break;
        case 0x7b: TRACEI("jnp rel8\t");
                   READIMM8; J_REL(!P, imm8); break;
        case 0x7c: TRACEI("jl rel8\t");
                   READIMM8; J_REL(L, imm8); break;
        case 0x7d: TRACEI("jnl rel8\t");
                   READIMM8; J_REL(!L, imm8); break;
        case 0x7e: TRACEI("jle rel8\t");
                   READIMM8; J_REL(LE, imm8); break;
        case 0x7f: TRACEI("jnle rel8\t");
                   READIMM8; J_REL(!LE, imm8); break;

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
                   READMODRM; READIMM; GRP1(imm, modrm_val,); break;
        case 0x83: TRACEI("grp1 imm8, modrm");
                   READMODRM; READIMM8; GRP1(imm8, modrm_val,); break;

#undef GRP1

        case 0x84: TRACEI("test reg8, modrm8");
                   READMODRM; TEST(modrm_reg, modrm_val,8); break;
        case 0x85: TRACEI("test reg, modrm");
                   READMODRM; TEST(modrm_reg, modrm_val,); break;

        case 0x86: TRACEI("xchg reg8, modrm8");
                   READMODRM; XCHG(modrm_reg, modrm_val,8); break;
        case 0x87: TRACEI("xchg reg, modrm");
                   READMODRM; XCHG(modrm_reg, modrm_val,); break;

        case 0x88: TRACEI("mov reg8, modrm8");
                   READMODRM; MOV(modrm_reg, modrm_val,8); break;
        case 0x89: TRACEI("mov reg, modrm");
                   READMODRM; MOV(modrm_reg, modrm_val,); break;
        case 0x8a: TRACEI("mov modrm8, reg8");
                   READMODRM; MOV(modrm_val, modrm_reg,8); break;
        case 0x8b: TRACEI("mov modrm, reg");
                   READMODRM; MOV(modrm_val, modrm_reg,); break;

        case 0x8d: TRACEI("lea\t\t");
                   READMODRM;
                   if (modrm.type == mod_reg)
                       UNDEFINED;
                   MOV(addr, modrm_reg,); break;

        // only gs is supported, and it does nothing
        // see comment in sys/tls.c
        case 0x8c: TRACEI("mov seg, modrm\t"); READMODRM;
            if (modrm.reg.reg32_id != REG_ID(ebp)) UNDEFINED;
            MOV(gs, modrm_val,16); break;
        case 0x8e: TRACEI("mov modrm, seg\t"); READMODRM;
            if (modrm.reg.reg32_id != REG_ID(ebp)) UNDEFINED;
            MOV(modrm_val, gs,16); break;

        case 0x8f: TRACEI("pop modrm");
                   READMODRM; POP(modrm_val); break;

        case 0x90: TRACEI("nop"); break;
        case 0x97: TRACEI("xchg odi, oax");
                   XCHG(odi, oax,); break;

        case 0x98: TRACEI("cvte"); CVTE; break;
        case 0x99: TRACEI("cvt"); CVT; break;

        case 0x9e: TRACEI("sahf\t\t"); SAHF; break;

        case 0x9c: TRACEI("pushf"); PUSHF(); break;
        case 0x9d: TRACEI("popf"); POPF(); break;

        case 0xa0: TRACEI("mov mem, al\t");
                   READADDR; MOV(mem_addr, al,8); break;
        case 0xa1: TRACEI("mov mem, eax\t");
                   READADDR; MOV(mem_addr, oax,); break;
        case 0xa2: TRACEI("mov al, mem\t");
                   READADDR; MOV(al, mem_addr,8); break;
        case 0xa3: TRACEI("mov oax, mem\t");
                   READADDR; MOV(oax, mem_addr,); break;
        case 0xa4: TRACEI("movsb"); MOVS(8); break;
        case 0xa5: TRACEI("movs"); MOVS(OP_SIZE); break;

        case 0xa8: TRACEI("test imm8, al");
                   READIMM8; TEST(imm8, al,8); break;
        case 0xa9: TRACEI("test imm, oax");
                   READIMM; TEST(imm, oax,); break;

        case 0xaa: TRACEI("stosb"); STOS(8); break;
        case 0xab: TRACEI("stos"); STOS(OP_SIZE); break;
        case 0xac: TRACEI("lodsb"); LODS(8); break;

        case 0xb0: TRACEI("mov imm, al\t");
                   READIMM8; MOV(imm8, al,); break;
        case 0xb1: TRACEI("mov imm, cl\t");
                   READIMM8; MOV(imm8, cl,); break;
        case 0xb2: TRACEI("mov imm, dl\t");
                   READIMM8; MOV(imm8, dl,); break;
        case 0xb3: TRACEI("mov imm, bl\t");
                   READIMM8; MOV(imm8, bl,); break;
        case 0xb4: TRACEI("mov imm, ah\t");
                   READIMM8; MOV(imm8, ah,); break;
        case 0xb5: TRACEI("mov imm, ch\t");
                   READIMM8; MOV(imm8, ch,); break;
        case 0xb6: TRACEI("mov imm, dh\t");
                   READIMM8; MOV(imm8, dh,); break;
        case 0xb7: TRACEI("mov imm, bh\t");
                   READIMM8; MOV(imm8, bh,); break;

        case 0xb8: TRACEI("mov imm, oax\t");
                   READIMM; MOV(imm, oax,); break;
        case 0xb9: TRACEI("mov imm, ocx\t");
                   READIMM; MOV(imm, ocx,); break;
        case 0xba: TRACEI("mov imm, odx\t");
                   READIMM; MOV(imm, odx,); break;
        case 0xbb: TRACEI("mov imm, obx\t");
                   READIMM; MOV(imm, obx,); break;
        case 0xbc: TRACEI("mov imm, osp\t");
                   READIMM; MOV(imm, osp,); break;
        case 0xbd: TRACEI("mov imm, obp\t");
                   READIMM; MOV(imm, obp,); break;
        case 0xbe: TRACEI("mov imm, osi\t");
                   READIMM; MOV(imm, osi,); break;
        case 0xbf: TRACEI("mov imm, odi\t");
                   READIMM; MOV(imm, odi,); break;

#define GRP2(count, val,z) \
    switch (modrm.opcode) { \
        case 0: TRACE("rol"); ROL(count, val,z); break; \
        case 1: TRACE("ror"); ROR(count, val,z); break; \
        case 2: TRACE("rcl"); UNDEFINED; \
        case 3: TRACE("rcr"); UNDEFINED; \
        case 4: \
        case 6: TRACE("shl"); SHL(count, val,z); break; \
        case 5: TRACE("shr"); SHR(count, val,z); break; \
        case 7: TRACE("sar"); SAR(count, val,z); break; \
    }

        case 0xc0: TRACEI("grp2 imm8, modrm8");
                   READMODRM; READIMM8; GRP2(imm8, modrm_val,8); break;
        case 0xc1: TRACEI("grp2 imm8, modrm");
                   READMODRM; READIMM8; GRP2(imm8, modrm_val,); break;

        case 0xc2: TRACEI("ret near imm\t");
                   READIMM16; RET_NEAR_IMM(imm); break;
        case 0xc3: TRACEI("ret near");
                   RET_NEAR(); break;

        case 0xc9: TRACEI("leave");
                   MOV(obp, osp,); POP(obp); break;

        case 0xcd: TRACEI("int imm8\t");
                   READIMM8; INT(imm); break;

        case 0xc6: TRACEI("mov imm8, modrm8");
                   READMODRM; READIMM8; MOV(imm8, modrm_val,8); break;
        case 0xc7: TRACEI("mov imm, modrm");
                   READMODRM; READIMM; MOV(imm, modrm_val,); break;

        case 0xd0: TRACEI("grp2 1, modrm8");
                   READMODRM; GRP2(1, modrm_val,8); break;
        case 0xd1: TRACEI("grp2 1, modrm");
                   READMODRM; GRP2(1, modrm_val,); break;
        case 0xd3: TRACEI("grp2 cl, modrm");
                   READMODRM; GRP2(cl, modrm_val,); break;

#undef GRP2

        case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
            TRACEI("fpu\t\t"); READMODRM;
            if (modrm.type != mod_reg) {
                switch (insn << 4 | modrm.opcode) {
                    case 0xd80: TRACE("fadd mem32"); FADDM(mem_addr_real,32); break;
                    case 0xd81: TRACE("fmul mem32"); FMULM(mem_addr_real,32); break;
                    case 0xd86: TRACE("fdiv mem32"); FDIVM(mem_addr_real,32); break;
                    case 0xd90: TRACE("fld mem32"); FLDM(mem_addr_real,32); break;
                    case 0xd95: TRACE("fldcw mem16"); FLDCW(mem_addr); break;
                    case 0xd97: TRACE("fnstcw mem16"); FSTCW(mem_addr); break;
                    case 0xda1: TRACE("fimul mem32"); FIMUL(mem_addr,32); break;
                    case 0xda4: TRACE("fisub mem32"); FISUB(mem_addr,32); break;
                    case 0xda6: TRACE("fidiv mem32"); FIDIV(mem_addr,32); break;
                    case 0xdb0: TRACE("fild mem32"); FILD(mem_addr,32); break;
                    case 0xdb2: TRACE("fist mem32"); FIST(mem_addr,32); break;
                    case 0xdb3: TRACE("fistp mem32"); FIST(mem_addr,32); FPOP; break;
                    case 0xdb7: TRACE("fstp mem80"); FSTM(mem_addr_real,80); FPOP; break;
                    case 0xdb5: TRACE("fld mem80"); FLDM(mem_addr_real,80); break;
                    case 0xdc0: TRACE("fadd mem64"); FADDM(mem_addr_real,64); break;
                    case 0xdc1: TRACE("fmul mem64"); FMULM(mem_addr_real,64); break;
                    case 0xdd0: TRACE("fld mem64"); FLDM(mem_addr_real,64); break;
                    case 0xdc4: TRACE("fsub mem64"); FSUBM(mem_addr_real,64); break;
                    case 0xdc6: TRACE("fdiv mem64"); FDIVM(mem_addr_real,64); break;
                    case 0xdd2: TRACE("fst mem64"); FSTM(mem_addr_real,64); break;
                    case 0xdd3: TRACE("fstp mem64"); FSTM(mem_addr_real,64); FPOP; break;
                    case 0xdf5: TRACE("fild mem64"); FILD(mem_addr,64); break;
                    case 0xdf7: TRACE("fistp mem64"); FIST(mem_addr,64); FPOP; break;
                    default: TRACE("undefined"); UNDEFINED;
                }
            } else {
                switch (insn << 4 | modrm.opcode) {
                    case 0xd80: TRACE("fadd st(i), st"); FADD(st_i, st_0); break;
                    case 0xd81: TRACE("fmul st(i), st"); FMUL(st_i, st_0); break;
                    case 0xd84: TRACE("fsub st(i), st"); FSUB(st_i, st_0); break;
                    case 0xd90: TRACE("fld st(i)"); FLD(); break;
                    case 0xd91: TRACE("fxch st"); FXCH(); break;
                    case 0xdb5: TRACE("fucomi st"); FUCOMI(); break;
                    case 0xdc0: TRACE("fadd st, st(i)"); FADD(st_0, st_i); break;
                    case 0xdc1: TRACE("fmul st, st(i)"); FMUL(st_0, st_i); break;
                    case 0xdd3: TRACE("fstp st"); FST(); FPOP; break;
                    case 0xdd4: TRACE("fucom st"); FUCOM(); break;
                    case 0xdd5: TRACE("fucomp st"); FUCOM(); FPOP; break;
                    case 0xda5: TRACE("fucompp st"); FUCOM(); FPOP; FPOP; break;
                    case 0xde0: TRACE("faddp st, st(i)"); FADD(st_0, st_i); FPOP; break;
                    case 0xde1: TRACE("fmulp st, st(i)"); FMUL(st_0, st_i); FPOP; break;
                    case 0xde4: TRACE("fsubrp st, st(i)"); FSUB(st_i, st_0); FPOP; break;
                    case 0xde5: TRACE("fsubp st, st(i)"); FSUB(st_0, st_i); FPOP; break;
                    case 0xdf5: TRACE("fucomip st"); FUCOMI(); FPOP; break;
                    default: switch (insn << 8 | modrm.opcode << 4 | modrm.rm_opcode) {
                    case 0xd940: TRACE("fchs"); FCHS(); break;
                    case 0xd941: TRACE("fabs"); FABS(); break;
                    case 0xd950: TRACE("fld1"); FLDC(one); break;
                    case 0xd956: TRACE("fldz"); FLDC(zero); break;
                    case 0xd970: TRACE("fprem"); FPREM(); break;
                    case 0xdf40: TRACE("fnstsw ax"); FSTSW(ax); break;
                    default: TRACE("undefined"); UNDEFINED;
                }}
            }
            break;

        case 0xe3: TRACEI("jcxz rel8\t");
                   READIMM8; JCXZ_REL(imm8); break;

        case 0xe8: TRACEI("call near\t");
                   READIMM; CALL_REL(imm); break;

        case 0xe9: TRACEI("jmp rel\t");
                   READIMM; JMP_REL(imm); break;
        case 0xeb: TRACEI("jmp rel8\t");
                   READIMM8; JMP_REL(imm8); break;

        case 0xf0: TRACELN("lock (ignored for now)"); goto restart;

        case 0xf2:
            READINSN;
            switch (insn) {
                case 0x0f:
                    READINSN;
                    switch (insn) {
                        case 0x2c: TRACEI("cvttsd2si modrm64, reg32");
                                   READMODRM; if (modrm.type == mod_reg) UNDEFINED; // TODO xmm
                                   CVTTSD2SI(mem_addr_real, modrm_reg); break;
                        default: TRACE("undefined"); UNDEFINED;
                    }
                    break;
                case 0xae: TRACEI("repnz scasb"); REPNZ(SCAS(8)); break;
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
                        case 0x7e: TRACEI("movq modrm, xmm");
                                   READMODRM; MOVQ(modrm_val, modrm_reg); break;
                        default: TRACE("undefined"); UNDEFINED;
                    }
                    break;

                case 0xa4: TRACEI("rep movsb"); REP(MOVS(8)); break;
                case 0xa5: TRACEI("rep movs"); REP(MOVS(OP_SIZE)); break;

                case 0xa6: TRACEI("repz cmpsb"); REPZ(CMPS(8)); break;

                case 0xaa: TRACEI("rep stosb"); REP(STOS(8)); break;
                case 0xab: TRACEI("rep stos"); REP(STOS(OP_SIZE)); break;

                // repz ret is equivalent to ret but on some amd chips there's
                // a branch prediction penalty if the target of a branch is a
                // ret. gcc used to use nop ret but repz ret is only one
                // instruction
                case 0xc3: TRACEI("repz ret\t"); RET_NEAR(); break;
                default: TRACELN("undefined"); UNDEFINED;
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
                DIV(oax, modrm_val, odx,z); break; \
        case 7: TRACE("idiv"); \
                IDIV(oax, modrm_val, odx,z); break; \
        default: TRACE("undefined"); UNDEFINED; \
    }

        case 0xf6: TRACEI("grp3 modrm8\t");
                   READMODRM; GRP3(modrm_val,8); break;
        case 0xf7: TRACEI("grp3 modrm\t");
                   READMODRM; GRP3(modrm_val,); break;

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
                PUSH(val); break; \
        case 7: TRACE("undefined"); UNDEFINED; \
    }

        case 0xfe: TRACEI("grp5 modrm8\t");
                   READMODRM; GRP5(modrm_val,8); break;
        case 0xff: TRACEI("grp5 modrm\t");
                   READMODRM; GRP5(modrm_val,); break;

#undef GRP5

        default:
            TRACELN("undefined");
            UNDEFINED;
    }
    TRACELN("");
    return -1; // everything is ok.
}
