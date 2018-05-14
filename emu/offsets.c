#include "emu/cpu.h"
#include "emu/jit.h"

void cpu() {
    OFFSET(CPU, cpu_state, eax);
    OFFSET(CPU, cpu_state, ebx);
    OFFSET(CPU, cpu_state, ecx);
    OFFSET(CPU, cpu_state, edx);
    OFFSET(CPU, cpu_state, esi);
    OFFSET(CPU, cpu_state, edi);
    OFFSET(CPU, cpu_state, ebp);
    OFFSET(CPU, cpu_state, esp);
    OFFSET(CPU, cpu_state, ax);
    OFFSET(CPU, cpu_state, bx);
    OFFSET(CPU, cpu_state, cx);
    OFFSET(CPU, cpu_state, dx);
    OFFSET(CPU, cpu_state, si);
    OFFSET(CPU, cpu_state, di);
    OFFSET(CPU, cpu_state, bp);
    OFFSET(CPU, cpu_state, sp);
    OFFSET(CPU, cpu_state, eip);

    OFFSET(CPU, cpu_state, eflags);
    OFFSET(CPU, cpu_state, of);
    OFFSET(CPU, cpu_state, cf);
    OFFSET(CPU, cpu_state, res);
    OFFSET(CPU, cpu_state, op1);
    OFFSET(CPU, cpu_state, op2);
    OFFSET(CPU, cpu_state, flags_res);
    MACRO(PF_RES);
    MACRO(ZF_RES);
    MACRO(SF_RES);
    MACRO(AF_OPS);
    MACRO(AF_FLAG);

    OFFSET(JIT_BLOCK, jit_block, code);

    OFFSET(TLB, tlb, entries);
    OFFSET(TLB, tlb, dirty_page);
    OFFSET(TLB_ENTRY, tlb_entry, page);
    OFFSET(TLB_ENTRY, tlb_entry, page_if_writable);
    OFFSET(TLB_ENTRY, tlb_entry, data_minus_addr);
}
