#ifndef AGAIN
#include "misc.h"
#include "emu/cpu.h"
#include "emu/modrm.h"
#include "emu/interrupt.h"
#include "sys/calls.h"

// instructions defined as static inline functions
#include "emu/instructions.h"

#define OP_SIZE 32
#define cpu_step CONCAT(cpu_step, OP_SIZE)
#endif

// this will be the next PyEval_EvalFrameEx.
int cpu_step(struct cpu_state *cpu) {

#undef ax
#undef bx
#undef cx
#undef dx
#undef si
#undef di
#undef bp
#undef sp
#if OP_SIZE == 32
#define ax cpu->eax
#define bx cpu->ebx
#define cx cpu->ecx
#define dx cpu->edx
#define si cpu->esi
#define di cpu->edi
#define bp cpu->ebp
#define sp cpu->esp
#else
#define ax cpu->ax
#define bx cpu->bx
#define cx cpu->cx
#define dx cpu->dx
#define si cpu->si
#define di cpu->di
#define bp cpu->bp
#define sp cpu->sp
#endif

    // watch out: these macros can evaluate the arguments any number of times
#define MEM_(addr,size) MEM_GET(cpu, addr, size)
#define MEM(addr) MEM_(addr,OP_SIZE)
#define MEM8(addr) MEM_(addr,8)
#define REG_(reg_id,size) REG_VAL(cpu, reg_id, size)
#undef REG // was last defined in modrm.h
#define REG(reg_id) REG_(reg_id, OP_SIZE)
#define REGPTR_(regptr,size) REG_(CONCAT3(regptr.reg,size,_id),size)
#define REGPTR(regptr) REGPTR_(regptr, OP_SIZE)
#define REGPTR8(regptr) REGPTR_(regptr, 8)
#define REGPTR64(regptr) REGPTR_(regptr, 64)

#define CHECK_W(addr) CHECK_WRITE(cpu, addr)

    struct modrm_info modrm;
    dword_t addr = 0;
#define READMODRM modrm_decode32(cpu, &addr, &modrm)
#define MODRM_CHECK_W if (modrm.type != mod_reg) { CHECK_W(addr); }
#define READMODRM_W READMODRM; MODRM_CHECK_W
#define modrm_val_(size) \
    *(modrm.type == mod_reg ? &REGPTR_(modrm.modrm_regid, size) : &MEM_(addr, size))
#define modrm_val modrm_val_(OP_SIZE)
#define modrm_val8 modrm_val_(8)
#define modrm_val16 modrm_val_(16)
#define modrm_val64 modrm_val_(64)
#define modrm_reg REGPTR(modrm.reg)
#define modrm_reg8 REGPTR8(modrm.reg)
#define modrm_reg64 REGPTR64(modrm.reg)

#undef imm
    byte_t imm8;
#if OP_SIZE == 16
    word_t imm16;
#else
    dword_t imm32;
#endif
#define READIMM_(name,size) \
    name = MEM_(cpu->eip,size); \
    cpu->eip += size/8; \
    TRACE("imm %x ", name)
#define imm CONCAT(imm, OP_SIZE)
#define READIMM READIMM_(imm, OP_SIZE)
#define READIMM8 READIMM_(imm8, 8)
    dword_t addr_offset = 0;
#define READADDR READIMM_(addr_offset, 32); addr += addr_offset
#define READADDR_W READADDR; CHECK_W(addr)
    byte_t insn;
#define READINSN \
    insn = MEM8(cpu->eip); \
    cpu->eip++; \
    TRACE("%02x ", insn);

restart:
    TRACE("%08x\t", cpu->eip);
    READINSN;
    switch (insn) {
        // if any instruction handlers declare variables, they should create a
        // new block for those variables.
        // any subtraction that occurs probably needs to have a cast to a
        // signed type, so sign extension happens.

        case 0x01: TRACEI("add reg, modrm");
                   READMODRM_W; ADD(modrm_reg, modrm_val); break;
        case 0x03: TRACEI("add modrm, reg");
                   READMODRM_W; ADD(modrm_val, modrm_reg); break;
        case 0x05: TRACEI("add imm, eax");
                   READIMM; ADD(imm, ax); break;

        case 0x0d: TRACEI("or imm, eax\t");
                   READIMM; OR(imm, ax); break;

        case 0x0f:
            // 2-byte opcode prefix
            READINSN;
            switch (insn) {
                case 0xa2:
                    TRACEI("cpuid");
                    do_cpuid(&cpu->eax, &cpu->ebx, &cpu->ecx, &cpu->edx);
                    break;

                // TODO more sets
                case 0x92: TRACEI("setb\t");
                           READMODRM_W; SET(B, modrm_val8); break;
                case 0x94: TRACEI("sete\t");
                           READMODRM_W; SET(E, modrm_val8); break;

                // TODO more jumps
                case 0x84: TRACEI("je rel\t");
                           READIMM; J_REL(E, imm); break;
                case 0x85: TRACEI("jne rel\t");
                           READIMM; J_REL(!E, imm); break;

                case 0xaf: TRACEI("imul modrm, reg");
                           READMODRM; IMUL(modrm_reg, modrm_val); break;

                case 0xb6: TRACEI("movz modrm8, reg");
                           READMODRM; MOV(modrm_val8, modrm_reg); break;
                case 0xb7: TRACEI("movz modrm16, reg");
                           READMODRM; MOV(modrm_val16, modrm_reg); break;

                case 0xd6:
                    // someone tell intel to get a life
                    if (OP_SIZE == 16) {
                        TRACEI("movq xmm, modrm");
                        READMODRM_W; MOV(modrm_reg64, modrm_reg64);
                    }
                    break;

                default:
                    TRACEI("undefined");
                    return INT_UNDEFINED;
            }
            break;

        case 0x21: TRACEI("and reg, modrm");
                   READMODRM_W; AND(modrm_reg, modrm_val); break;

        case 0x29: TRACEI("sub reg, modrm");
                   READMODRM_W; SUB(modrm_reg, modrm_val); break;

        case 0x30: TRACEI("xor reg8, modrm8");
                   READMODRM_W; XOR(modrm_reg8, modrm_val8); break;
        case 0x31: TRACEI("xor reg, modrm");
                   READMODRM_W; XOR(modrm_reg, modrm_val); break;
        case 0x33: TRACEI("xor modrm, reg");
                   READMODRM; XOR(modrm_val, modrm_reg); break;

        case 0x39: TRACEI("cmp reg, modrm");
                   READMODRM; CMP(modrm_reg, modrm_val); break;
        case 0x3d: TRACEI("cmp imm, eax");
                   READIMM; CMP(imm, ax); break;

        case 0x50: TRACEI("push eax");
                   PUSH(ax); break;
        case 0x51: TRACEI("push ecx");
                   PUSH(cx); break;
        case 0x52: TRACEI("push edx");
                   PUSH(dx); break;
        case 0x53: TRACEI("push ebx");
                   PUSH(bx); break;
        case 0x54: {
            TRACEI("push esp");
            // need to make sure to push the old value
            dword_t old_sp = sp;
            PUSH(old_sp); break;
        }
        case 0x55: TRACEI("push ebp");
                   PUSH(bp); break;
        case 0x56: TRACEI("push esi");
                   PUSH(si); break;
        case 0x57: TRACEI("push edi");
                   PUSH(di); break;

        case 0x58: TRACEI("pop eax");
                   POP(ax); break;
        case 0x59: TRACEI("pop ecx");
                   POP(cx); break;
        case 0x5a: TRACEI("pop edx");
                   POP(dx); break;
        case 0x5b: TRACEI("pop ebx");
                   POP(bx); break;
        case 0x5c: {
            TRACEI("pop esp");
            dword_t new_sp;
            POP(new_sp);
            sp = new_sp;
            break;
        }
        case 0x5d: TRACEI("pop ebp");
                   POP(bp); break;
        case 0x5e: TRACEI("pop esi");
                   POP(si); break;
        case 0x5f: TRACEI("pop edi");
                   POP(di); break;

        case 0x65: TRACE("segment gs\n");
                   addr += cpu->tls_ptr; goto restart;

        case 0x66:
#if OP_SIZE == 32
            TRACE("entering 16 bit mode\n");
            return cpu_step16(cpu);
#else
            TRACE("entering 32 bit mode\n");
            return cpu_step32(cpu);
#endif

        case 0x68: TRACEI("push imm\t");
                   READIMM; PUSH(imm); break;
        case 0x6a: TRACEI("push imm8\t");
                   READIMM8; PUSH(imm8); break;

        case 0x73: TRACEI("jnb rel8\t");
                   READIMM8; J_REL(!B, (int8_t) imm8); break;
        case 0x74: TRACEI("je rel8\t");
                   READIMM8; J_REL(E, (int8_t) imm8); break;
        case 0x75: TRACEI("jne rel8\t");
                   READIMM8; J_REL(!E, (int8_t) imm8); break;
        case 0x76: TRACEI("jbe rel8\t");
                   READIMM8; J_REL(BE, (int8_t) imm8); break;
        case 0x77: TRACEI("ja rel8\t");
                   READIMM8; J_REL(!BE, (int8_t) imm8); break;
        case 0x78: TRACEI("js rel8\t");
                   READIMM8; J_REL(S, (int8_t) imm8); break;
        case 0x79: TRACEI("jns rel8\t");
                   READIMM8; J_REL(!S, (int8_t) imm8); break;
        case 0x7e: TRACEI("jle rel8\t");
                   READIMM8; J_REL(LE, (int8_t) imm8); break;

        case 0x80: TRACEI("grp1 imm8, modrm8");
                   READMODRM; READIMM8; GRP1(imm8, modrm_val8); break;
        case 0x81: TRACEI("grp1 imm, modrm");
                   READMODRM; READIMM; GRP1(imm, modrm_val); break;
        case 0x83: TRACEI("grp1 imm8, modrm");
                   READMODRM; READIMM8; GRP1((uint32_t) (int8_t) imm8, modrm_val); break;

        case 0x84: TRACEI("test reg8, modrm8");
                   READMODRM; TEST(modrm_reg8, modrm_val8); break;
        case 0x85: TRACEI("test reg, modrm");
                   READMODRM; TEST(modrm_reg, modrm_val); break;

        case 0x88: TRACEI("mov reg8, modrm8");
                   READMODRM_W; MOV(modrm_reg8, modrm_val8); break;
        case 0x89: TRACEI("mov reg, modrm");
                   READMODRM_W; MOV(modrm_reg, modrm_val); break;
        case 0x8a: TRACEI("mov modrm8, reg8");
                   READMODRM; MOV(modrm_val8, modrm_reg8); break;
        case 0x8b: TRACEI("mov modrm, reg");
                   READMODRM; MOV(modrm_val, modrm_reg); break;

        case 0x8d:
            TRACEI("lea\t\t");
            READMODRM;
            if (modrm.type == mod_reg) {
                return INT_UNDEFINED;
            }
            modrm_reg = addr; break;
        case 0x8e: TRACEI("mov modrm, seg\t");
                   // only gs is supported, and it does nothing
                   // see comment in sys/tls.c
                   READMODRM;
                   if (modrm.reg.reg32_id != REG_ID(ebp)) {
                       return INT_UNDEFINED;
                   }
                   break;

        case 0xa1: TRACEI("mov mem, eax\t");
                   READADDR_W; MOV(MEM(addr), ax); break;
        case 0xa3: TRACEI("mov eax, mem\t");
                   READADDR_W; MOV(ax, MEM(addr)); break;

        case 0xb8: TRACEI("mov imm, eax\t");
                   READIMM; MOV(imm, ax); break;
        case 0xb9: TRACEI("mov imm, ecx\t");
                   READIMM; MOV(imm, cx); break;
        case 0xba: TRACEI("mov imm, edx\t");
                   READIMM; MOV(imm, dx); break;
        case 0xbb: TRACEI("mov imm, ebx\t");
                   READIMM; MOV(imm, bx); break;
        case 0xbc: TRACEI("mov imm, esp\t");
                   READIMM; MOV(imm, sp); break;
        case 0xbd: TRACEI("mov imm, ebp\t");
                   READIMM; MOV(imm, bp); break;
        case 0xbe: TRACEI("mov imm, esi\t");
                   READIMM; MOV(imm, si); break;
        case 0xbf: TRACEI("mov imm, edi\t");
                   READIMM; MOV(imm, di); break;

        case 0xc1:
            TRACEI("shift imm8, modrm");
            READMODRM_W; READIMM8;
            modrm_val = do_shift(modrm_val, imm8, modrm.opcode);
            break;

        case 0xc3: TRACEI("ret near");
                   RET_NEAR(); break;

        case 0xcd: TRACEI("int imm8\t");
                   READIMM8; INT(imm8); break;

        case 0xc6: TRACEI("mov imm8, modrm8");
                   READMODRM_W; READIMM8; MOV(imm8, modrm_val8); break;
        case 0xc7: TRACEI("mov imm, modrm");
                   READMODRM_W; READIMM; MOV(imm, modrm_val); break;

        case 0xe8: TRACEI("call near\t");
                   READIMM; CALL_REL(imm); break;

        case 0xe9: TRACEI("jmp rel\t");
                   READIMM; JMP_REL(imm); break;

        case 0xf3:
            READINSN;
            switch (insn) {
                case 0x0f:
                    // 2-byte opcode prefix
                    // after a rep prefix, means we have sse/mmx insanity
                    READINSN;
                    switch (insn) {
                        case 0x7e: TRACEI("movq modrm, xmm");
                                   READMODRM; MOV(modrm_val64, modrm_reg64);
                    }
                    break;

                case 0xa4: TRACEI("rep movsb (di), (si)"); REP(MOVSB); break;
                case 0xa5: TRACEI("rep movs (di), (si)"); REP(MOVS); break;

                // repz ret is equivalent to ret but on some amd chips there's
                // a branch prediction penalty if the target of a branch is a
                // ret. gcc used to use nop ret but repz ret is only one
                // instruction
                case 0xc3: TRACEI("repz ret\t"); RET_NEAR(); break;
                default: TRACE("undefined\n"); return INT_UNDEFINED;
            }
            break;

        case 0xf6: TRACEI("grp3 modrm8\t");
                   READMODRM; GRP38(modrm_val8); break;
        case 0xf7: TRACEI("grp3 modrm\t");
                   READMODRM; GRP3(modrm_val); break;

        case 0xfc: TRACEI("cld"); cpu->df = 0; break;
        case 0xfd: TRACEI("std"); cpu->df = 1; break;

        case 0xff: TRACEI("grp5 modrm\t");
                   READMODRM; GRP5(modrm_val); break;

        default:
            TRACE("undefined\n");
            return INT_UNDEFINED;
    }
    TRACE("\n");
    return -1; // everything is ok.
}

#ifndef AGAIN
#define AGAIN

#undef OP_SIZE
#define OP_SIZE 16
#include "cpu.c"

void cpu_run(struct cpu_state *cpu) {
    while (true) {
        int interrupt = cpu_step32(cpu);
        if (interrupt != INT_NONE) {
            TRACE("interrupt %d", interrupt);
            handle_interrupt(cpu, interrupt);
        }
    }
}

#endif
