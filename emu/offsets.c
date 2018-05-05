#include "emu/cpu.h"
#include "emu/jit.h"

void cpu() {
    OFFSET(CPU_eax, struct cpu_state, eax);
    OFFSET(CPU_ebx, struct cpu_state, ebx);
    OFFSET(CPU_ecx, struct cpu_state, ecx);
    OFFSET(CPU_edx, struct cpu_state, edx);
    OFFSET(CPU_esi, struct cpu_state, esi);
    OFFSET(CPU_edi, struct cpu_state, edi);
    OFFSET(CPU_ebp, struct cpu_state, ebp);
    OFFSET(CPU_esp, struct cpu_state, esp);
    OFFSET(CPU_ax, struct cpu_state, ax);
    OFFSET(CPU_bx, struct cpu_state, bx);
    OFFSET(CPU_cx, struct cpu_state, cx);
    OFFSET(CPU_dx, struct cpu_state, dx);
    OFFSET(CPU_si, struct cpu_state, si);
    OFFSET(CPU_di, struct cpu_state, di);
    OFFSET(CPU_bp, struct cpu_state, bp);
    OFFSET(CPU_sp, struct cpu_state, sp);

    OFFSET(JIT_BLOCK_code, struct jit_block, code);

    OFFSET(TLB_entries, struct tlb, entries);
    OFFSET(TLB_ENTRY_page, struct tlb_entry, page);
    OFFSET(TLB_ENTRY_page_if_writable, struct tlb_entry, page_if_writable);
    OFFSET(TLB_ENTRY_data_minus_addr, struct tlb_entry, data_minus_addr);
}
