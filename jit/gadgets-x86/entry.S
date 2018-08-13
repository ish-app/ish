#include "emu/interrupt.h"
#include "gadgets.h"

.global jit_enter
.type jit_enter,function
jit_enter:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    leaq JIT_BLOCK_code(%rdi), %_ip
    movq %rsi, %_cpu
    movq %rsp, LOCAL_bp(%_cpu)
    leaq TLB_entries(%rdx), %_tlb
    movl CPU_eax(%_cpu), %eax
    movl CPU_ebx(%_cpu), %ebx
    movl CPU_ecx(%_cpu), %ecx
    movl CPU_edx(%_cpu), %edx
    movl CPU_esi(%_cpu), %esi
    movl CPU_edi(%_cpu), %edi
    movl CPU_ebp(%_cpu), %ebp
    movl CPU_esp(%_cpu), %_esp
    gret

.global jit_ret_chain
jit_ret_chain:
    btq $63, %_ip
    jc 1f
    gret
1:
.global jit_ret
jit_ret:
    movl $-1, %_tmp
    jmp jit_exit

.global jit_exit
jit_exit:
    movl %eax, CPU_eax(%_cpu)
    movl %ebx, CPU_ebx(%_cpu)
    movl %ecx, CPU_ecx(%_cpu)
    movl %edx, CPU_edx(%_cpu)
    movl %esi, CPU_esi(%_cpu)
    movl %edi, CPU_edi(%_cpu)
    movl %ebp, CPU_ebp(%_cpu)
    movl %_esp, CPU_esp(%_cpu)
    movl %_eip, CPU_eip(%_cpu)
    movq LOCAL_bp(%_cpu), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    mov %_tmp, %eax
    ret

.gadget interrupt
    movl (%_ip), %_tmp
    movl 8(%_ip), %_eip
    jmp jit_exit

.gadget exit
    movl (%_ip), %_eip
    jmp jit_ret
