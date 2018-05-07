#include "emu/interrupt.h"
#include "gadgets.h"
#include "cpu-offsets.h"

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

# gadget support macros

# memory reading and writing
# TODO cross-page access handling (but it's going to be so slow :cry:)
.irp type, read,write

.macro mem_\type place
    movl %addr, %r14d
    shrl $8, %r14d
    andl $0x3ff0, %r14d
    movl %addr, %r15d
    andl $0xfffff000, %r15d
    .ifc \type,read
        cmpl TLB_ENTRY_page(%tlb,%r14), %r15d
    .else
        cmpl TLB_ENTRY_page_if_writable(%tlb,%r14), %r15d
    .endif
    je 1f
    call handle_\type\()_miss
    jmp 2f
1:
    addq TLB_ENTRY_data_minus_addr(%tlb,%r14), %addrq
2:
    .ifc \type,read
        movl (%addrq), \place
    .else
        movl \place, (%addrq)
    .endif
.endm

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
    movq %rax, %addrq
    restore_c
    testq %addrq, %addrq
    jz segfault
    ret

.endr

segfault:
    addq $8, %rsp # pop return address
    movl $INT_GPF, %tmp
    jmp jit_exit

# a gadget for each register
.macro .reg_gadgets type
    .each_reg
        .gadget \type\()_\reg
        .ifnc \reg,esp
            g_\type \reg
        .else
            g_\type xsp
        .endif
        gret
    .endr
.endm

.macro .gadget_array type
.global \type\()_gadgets
.type \type\()_gadgets,@object
\type\()_gadgets:
    # The following .irp should stay in sync with enum arg in emu/gen.c
    .irp arg, eax,ecx,edx,ebx,esp,ebp,esi,edi,ax,cx,dx,bx,sp,bp,si,di,imm,mem32
        .ifndef gadget_\type\()_\arg
            .set gadget_\type\()_\arg, 0
        .endif
        .quad gadget_\type\()_\arg
    .endr
.endm

.gadget push
    sub $4, %xsp
    movl %xsp, %addr
    mem_write %tmp
    gret
.gadget pop
    movl %xsp, %addr
    mem_read %tmp
    add $4, %xsp
    gret

.macro g_load reg
    movl %\reg, %tmp
.endm
.reg_gadgets load
.macro g_store reg
    movl %tmp, %\reg
.endm
.reg_gadgets store

.gadget sub_imm
    subl (%ip), %tmp
    gret 1

.data
.gadget_array load
.gadget_array store
.gadget_array sub
