#include "gadgets.h"
#include "emu/interrupt.h"

.gadget push
    sub $4, %_esp
    movl %_esp, %_addr
    write_prep
    movl %_tmp, (%_addrq)
    gret
.gadget pop
    movl %_esp, %_addr
    read_prep
    movl (%_addrq), %_tmp
    add $4, %_esp
    gret

.macro _invoke macro, reg, post
    \macro reg_\reg, e\reg\post
.endm
.macro .each_reg macro
    .irp reg, a,b,c,d
        _invoke \macro, \reg, x
    .endr
    .irp reg, si,di,bp
        _invoke \macro, \reg,
    .endr
    \macro reg_sp, _esp
.endm

.macro x name, reg
    .gadget addr_\name
        movl %\reg, %_addr
        addl (%_ip), %_addr
        gret 1
.endm
.each_reg x
.purgem x
.gadget_array addr, REG_LIST

.macro x name, reg
    .irp times, 1,2,4
        .gadget si_\name\()_\times
            .ifnc \reg,esp
                leal (%_addr,%\reg,\times), %_addr
            .else
                leal (%_addr,%_esp,\times), %_addr
            .endif
            gret
    .endr
.endm
.each_reg x
.purgem x

.section .rodata
.global si_gadgets
si_gadgets:
.irp reg, REG_LIST
    .irp times, 1,2,4
        .quad gadget_si_\reg\()_\times
    .endr
.endr
.previous

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

.gadget_array, REG_LIST
