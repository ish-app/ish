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
    leaq JIT_BLOCK_code(%rdi), %ip
    movq %rsi, %cpu
    leaq TLB_entries(%rdx), %tlb
    movl CPU_eax(%cpu), %eax
    movl CPU_ebx(%cpu), %ebx
    movl CPU_ecx(%cpu), %ecx
    movl CPU_edx(%cpu), %edx
    movl CPU_esi(%cpu), %esi
    movl CPU_edi(%cpu), %edi
    movl CPU_ebp(%cpu), %ebp
    movl CPU_esp(%cpu), %xsp
    # TODO more of those
    gret

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
    pop %r12
    pop %rbx
    pop %rbp
    mov %tmp, %eax
    ret

.gadget interrupt
    movl (%ip), %tmp
    jmp jit_exit

.gadget exit
    movl $-1, %tmp
    jmp jit_exit

# memory stuff that can't go in a header
.irp type, read,write
    .global handle_\type\()_miss
    handle_\type\()_miss:
        save_c
        # %tlb actually points to tlb->entries
        leaq -TLB_entries(%tlb), %rdi
        movl %addr, %esi
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
    movl $INT_GPF, %tmp
    jmp jit_exit
