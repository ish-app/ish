#ifndef AGAIN
#include "misc.h"
#include "emu/cpu.h"
#include "emu/modrm.h"
#include "emu/interrupt.h"
#include "sys/calls.h"

void trace_cpu(struct cpu_state *cpu) {
    TRACE("eax=%x ebx=%x ecx=%x edx=%x esi=%x edi=%x ebp=%x esp=%x",
            cpu->eax, cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi, cpu->ebp, cpu->esp);
}

// fuck preprocessor
#define OP_SIZE 32
#define cpu_step CONCAT(cpu_step, OP_SIZE)
#endif

#undef oprnd_t
#if OP_SIZE == 32
#define oprnd_t dword_t
#else
#define oprnd_t word_t
#endif

// this will be the next PyEval_EvalFrameEx.
int cpu_step(struct cpu_state *cpu) {
    // watch out: these macros can evaluate the arguments any number of times
#define MEM_(addr,size) MEM_GET(cpu, addr, size)
#define MEM(addr) MEM_(addr,OP_SIZE)
#define MEM8(addr) MEM_(addr,8)
#define REG_(reg_id,size) REG_VAL(cpu, reg_id, size)
#define REG(reg_id) REG_(reg_id, OP_SIZE)
#define REGPTR_(regptr,size) REG_(CONCAT3(regptr.reg,size,_id),size)
#define REGPTR(regptr) REGPTR_(regptr, OP_SIZE)
#define REGPTR8(regptr) REGPTR_(regptr, 8)

    // used by MODRM_MEM, don't use for anything else
    struct modrm_info modrm;
    dword_t addr;
#define DECODE_MODRM(size) \
    modrm_decode##size(cpu, &addr, &modrm)
#define MODRM_VAL_(size) \
    *(modrm.type == mod_reg ? &REGPTR_(modrm.modrm_reg, size) : &MEM_(addr, size))
#define MODRM_VAL MODRM_VAL_(OP_SIZE)
#define MODRM_VAL8 MODRM_VAL_(8)

#define PUSH(thing) \
    cpu->esp -= OP_SIZE/8; \
    MEM(cpu->esp) = thing
#define POP(thing) \
    thing = MEM(cpu->esp); \
    cpu->esp += OP_SIZE/8

#undef imm
    byte_t imm8;
    word_t imm16;
    dword_t imm32;
#define READIMM_(name,size) \
    name = MEM_(cpu->eip,size); \
    cpu->eip += size/8; \
    TRACE("immediate: %x", name)
#define imm CONCAT(imm, OP_SIZE)
#define READIMM READIMM_(imm, OP_SIZE)
#define READIMM8 READIMM_(imm8, 8)
#define READADDR READIMM_(addr, 32)

    byte_t insn = MEM8(cpu->eip);
    TRACE_("0x%x: ", insn);
    cpu->eip++;
    switch (insn) {
        // if any instruction handlers declare variables, they should create a
        // new block for those variables.
        // any subtraction that occurs probably needs to have a cast to a
        // signed type, so sign extension happens.

        case 0x31:
            TRACE("xor reg, modrm");
            DECODE_MODRM(32);
            MODRM_VAL ^= REGPTR(modrm.reg);
            break;

        case 0x50:
            TRACE("push eax");
            PUSH(cpu->eax); break;
        case 0x51:
            TRACE("push ecx");
            PUSH(cpu->ecx); break;
        case 0x52:
            TRACE("push edx");
            PUSH(cpu->edx); break;
        case 0x53:
            TRACE("push ebx");
            PUSH(cpu->ebx); break;
        case 0x54: {
            TRACE("push esp");
            // need to make sure to push the old value
            dword_t old_esp = cpu->esp;
            PUSH(old_esp);
            break;
        }
        case 0x55:
            TRACE("push ebp");
            PUSH(cpu->ebp); break;
        case 0x56:
            TRACE("push esi");
            PUSH(cpu->esi); break;
        case 0x57:
            TRACE("push edi");
            PUSH(cpu->edi); break;

        case 0x58:
            TRACE("pop eax");
            POP(cpu->eax); break;
        case 0x59:
            TRACE("pop ecx");
            POP(cpu->ecx); break;
        case 0x5a:
            TRACE("pop edx");
            POP(cpu->edx); break;
        case 0x5b:
            TRACE("pop ebx");
            POP(cpu->ebx); break;
        case 0x5c: {
            TRACE("pop esp");
            // need to make sure to pop the old value
            dword_t old_esp = cpu->esp;
            POP(old_esp);
            break;
        }
        case 0x5d:
            TRACE("pop ebp");
            POP(cpu->ebp); break;
        case 0x5e:
            TRACE("pop esi");
            POP(cpu->esi); break;
        case 0x5f:
            TRACE("pop edi");
            POP(cpu->edi); break;

        case 0x66:
#if OP_SIZE == 32
            TRACE("entering 16 bit mode");
            return cpu_step16(cpu);
#else
            TRACE("entering 32 bit mode");
            return cpu_step32(cpu);
#endif

        case 0x68:
            TRACE("push imm");
            READIMM;
            PUSH(imm);
            break;

        case 0x83:
            TRACE("grp1 imm8, reg");
            DECODE_MODRM(32); READIMM8;
            switch (modrm.opcode) {
                case 0b100:
                    TRACE("and");
                    MODRM_VAL &= (int8_t) imm8;
                    break;
                case 0b101:
                    TRACE("sub");
                    MODRM_VAL -= (int8_t) imm8;
                    break;
                default:
                    TRACE("undefined");
                    return INT_UNDEFINED;
            }
            break;

        case 0x88:
            TRACE("movb reg, modrm");
            DECODE_MODRM(32);
            MODRM_VAL8 = REGPTR8(modrm.reg);
            break;

        case 0x89:
            TRACE("mov reg, modrm");
            DECODE_MODRM(32);
            MODRM_VAL = REGPTR(modrm.reg);
            break;

        case 0x8a:
            TRACE("mov modrm, reg");
            DECODE_MODRM(32);
            REGPTR8(modrm.reg) = MODRM_VAL8;
            break;

        case 0x8b:
            TRACE("mov modrm, reg");
            DECODE_MODRM(32);
            REGPTR(modrm.reg) = MODRM_VAL;
            break;

        case 0x8d:
            TRACE("lea modrm, reg");
            DECODE_MODRM(32);
            if (modrm.type == mod_reg) {
                return INT_UNDEFINED;
            }
            REGPTR(modrm.reg) = addr;
            break;

        case 0xa1:
            TRACE("mov (immediate), eax");
            READADDR;
            cpu->eax = MEM(addr);
            break;

        case 0xb8:
            TRACE("mov immediate, eax");
            READIMM; cpu->eax = imm; break;
        case 0xb9:
            TRACE("mov immediate, ecx");
            READIMM; cpu->ecx = imm; break;
        case 0xba:
            TRACE("mov immediate, edx");
            READIMM; cpu->edx = imm; break;
        case 0xbb:
            TRACE("mov immediate, ebx");
            READIMM; cpu->ebx = imm; break;
        case 0xbc:
            TRACE("mov immediate, esp");
            READIMM; cpu->esp = imm; break;
        case 0xbd:
            TRACE("mov immediate, ebx");
            READIMM; cpu->ebx = imm; break;
        case 0xbe:
            TRACE("mov immediate, ebx");
            READIMM; cpu->ebx = imm; break;
        case 0xbf:
            TRACE("mov immediate, ebx");
            READIMM; cpu->ebx = imm; break;

        case 0xcd:
            TRACE("int");
            READIMM8; return imm8;

        case 0xc6:
            TRACE("mov imm8, modrm8");
            DECODE_MODRM(32); READIMM8;
            MODRM_VAL = imm8;
            break;
        case 0xc7:
            TRACE("mov imm, modrm");
            DECODE_MODRM(32); READIMM;
            MODRM_VAL = imm;
            break;

        default:
            TRACE("undefined");
            debugger;
            return INT_UNDEFINED;
    }
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
