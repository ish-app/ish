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

.macro x reg
    movl %\reg, %_addr
    addl (%_ip), %_addr
    gret 1
.endm
.reg_gadgets addr

.irp reg, REG_LIST
    .irp times, 1,2,4
        .gadget si_\reg\()_\times
            .ifnc \reg,esp
                leal (%_addr,%\reg,\times), %_addr
            .else
                leal (%_addr,%_esp,\times), %_addr
            .endif
            gret
    .endr
.endr
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

.section .rodata
.gadget_array_list addr, REG_LIST
