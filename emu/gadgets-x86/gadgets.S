#include "gadgets.h"
#include "cpu-offsets.h"

.global jit_enter
.type jit_enter,function
jit_enter:
    push %rbp
    push %rbx
    leaq JIT_BLOCK_code(%rsi), %ip
    movq %rdi, %cpu
    movl CPU_eax(%cpu), %eax
    movl CPU_ebx(%cpu), %ebx
    movl CPU_ecx(%cpu), %ecx
    movl CPU_edx(%cpu), %edx
    movl CPU_esi(%cpu), %esi
    movl CPU_edi(%cpu), %edi
    movl CPU_ebp(%cpu), %ebp
    movl CPU_esp(%cpu), %xsp
    # TODO more of those
    endgadget

.global jit_exit
jit_exit:
    movl %eax, CPU_eax(%cpu)
    movl %ebx, CPU_ebx(%cpu)
    movl %ecx, CPU_ecx(%cpu)
    movl %edx, CPU_edx(%cpu)
    movl %esi, CPU_esi(%cpu)
    movl %edi, CPU_edi(%cpu)
    movl %ebp, CPU_ebp(%cpu)
    movl %xsp, CPU_esp(%cpu)
    # TODO more of those
    pop %rbx
    pop %rbp
    mov %tmp, %eax
    ret

gadget interrupt
    op %tmp
    jmp jit_exit

gadget exit
    movl $-1, %tmp
    jmp jit_exit
