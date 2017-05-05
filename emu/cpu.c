#include "emu/cpu.h"
#include "emu/modrm.h"
#include "emu/interrupt.h"

// this will be the next PyEval_EvalFrameEx.
int cpu_step(struct cpu_state *cpu) {
    // watch out: these macros can evaluate the arguments any number of times
#define MEM(addr, size) MEM_GET(cpu, addr, size)
#define REG(reg_id, size) REG_VAL(cpu, reg_id, size)
#define REGPTR(regptr, size) REG(regptr.reg##size##_id, size)
    // used by MODRM_MEM, don't use for anything else
    struct modrm_info modrm;
    dword_t modrm_addr;
#define DECODE_MODRM(size) \
    modrm_decode##size(cpu, &modrm_addr, &modrm)
#define PUSH(thing, size) \
    cpu->esp -= size; \
    MEM(cpu->esp, size) = thing

    byte_t insn = MEM(cpu->eip,8);
    printf("0x%x\n", insn);
    cpu->eip++;
    switch (insn) {
        // if any instruction handlers declare variables, they should create a
        // new block for those variables

        // push dword register
        case 0x50:
            PUSH(cpu->eax,32); break;
        case 0x51:
            PUSH(cpu->ecx,32); break;
        case 0x52:
            PUSH(cpu->edx,32); break;
        case 0x53:
            PUSH(cpu->ebx,32); break;
        case 0x54: {
            // need to make sure to push the old value
            dword_t old_esp = cpu->esp;
            PUSH(old_esp, 32);
            break;
        }
        case 0x55:
            PUSH(cpu->ebp,32); break;
        case 0x56:
            PUSH(cpu->esi,32); break;
        case 0x57:
            PUSH(cpu->edi,32); break;

        // move register to dword modrm
        case 0x89: {
            DECODE_MODRM(32);
            if (modrm.type == mod_reg) {
                REGPTR(modrm.modrm_reg,32) = REGPTR(modrm.reg,32);
            } else {
                MEM(modrm_addr,32) = REGPTR(modrm.reg,32);
            }
            break;
        }
    }
    return 0;
}

void cpu_run(struct cpu_state *cpu) {
    while (true) {
        int interrupt = cpu_step(cpu);
        if (interrupt != INT_NONE) {
            /* handle_interrupt(interrupt); */
        }
    }
}
