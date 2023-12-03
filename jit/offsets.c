#include "jit/jit.h"
#include "jit/frame.h"
#include "emu/cpu.h"
#include "emu/tlb.h"

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
    OFFSET(CPU, cpu_state, gs);
    OFFSET(CPU, cpu_state, tls_ptr);

    OFFSET(CPU, cpu_state, eflags);
    OFFSET(CPU, cpu_state, of);
    OFFSET(CPU, cpu_state, cf);
    OFFSET(CPU, cpu_state, res);
    OFFSET(CPU, cpu_state, op1);
    OFFSET(CPU, cpu_state, op2);
    OFFSET(CPU, cpu_state, flags_res);
    OFFSET(CPU, cpu_state, df_offset);
    OFFSET(CPU, cpu_state, fsw);
    OFFSET(CPU, cpu_state, xmm);
    MACRO(PF_RES);
    MACRO(ZF_RES);
    MACRO(SF_RES);
    MACRO(AF_OPS);
    MACRO(PF_FLAG);
    MACRO(AF_FLAG);
    MACRO(ZF_FLAG);
    MACRO(SF_FLAG);
    MACRO(DF_FLAG);

    OFFSET(LOCAL, jit_frame, bp);
    OFFSET(LOCAL, jit_frame, value);
    OFFSET(LOCAL, jit_frame, value_addr);
    OFFSET(LOCAL, jit_frame, last_block);
    OFFSET(LOCAL, jit_frame, ret_cache);
    OFFSET(CPU, cpu_state, segfault_addr);
    OFFSET(CPU, cpu_state, segfault_was_write);
    OFFSET(CPU, cpu_state, poked_ptr);
    MACRO(MEM_READ);
    MACRO(MEM_WRITE);

    OFFSET(JIT_BLOCK, jit_block, addr);
    OFFSET(JIT_BLOCK, jit_block, code);

    OFFSET(TLB, tlb, entries);
    OFFSET(TLB, tlb, dirty_page);
    OFFSET(TLB, tlb, segfault_addr);
    OFFSET(TLB_ENTRY, tlb_entry, page);
    OFFSET(TLB_ENTRY, tlb_entry, page_if_writable);
    OFFSET(TLB_ENTRY, tlb_entry, data_minus_addr);
}
