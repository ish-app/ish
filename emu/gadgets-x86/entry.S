#include "emu/interrupt.h"
#include "gadgets.h"

.extern tlb_handle_miss

.text
.global jit_enter
.type jit_enter,function
jit_enter:
    push %rbp
    push %rbx
    push %r12
    leaq JIT_BLOCK_code(%rdi), %_ip
    movq %rsi, %_cpu
    leaq TLB_entries(%rdx), %_tlb
    movl CPU_eax(%_cpu), %eax
    movl CPU_ebx(%_cpu), %ebx
    movl CPU_ecx(%_cpu), %ecx
    movl CPU_edx(%_cpu), %edx
    movl CPU_esi(%_cpu), %esi
    movl CPU_edi(%_cpu), %edi
    movl CPU_ebp(%_cpu), %ebp
    movl CPU_esp(%_cpu), %_esp
    # TODO more of those
    gret

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
    # TODO more of those
    movl %_eip, CPU_eip(%_cpu)
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
    movl $-1, %_tmp
    movl (%_ip), %_eip
    jmp jit_exit

# memory stuff that can't go in a header file
.irp type, read,write
    .global handle_\type\()_miss
    handle_\type\()_miss:
        save_c
        # %tlb actually points to tlb->entries
        leaq -TLB_entries(%_tlb), %rdi
        movl %_addr, %esi
        .ifc \type,read
            movq $0, %rdx
        .else
            movq $1, %rdx
        .endif
        call tlb_handle_miss
        movq %rax, %r15
        restore_c
        testq %r15, %r15
        jz segfault
        ret
.endr

segfault:
    addq $8, %rsp # pop return address
    movl $INT_GPF, %_tmp
    jmp jit_exit
