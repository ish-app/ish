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

.rodata
.gadget_array_list addr, REG_LIST
