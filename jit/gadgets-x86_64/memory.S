#include "gadgets.h"
#include "emu/interrupt.h"

.gadget push
    leal -4(%_esp), %_addr
    write_prep 32, push
    movl %_tmp, (%_addrq)
    write_done 32, push
    sub $4, %_esp
    gret 1
.gadget pop
    movl %_esp, %_addr
    read_prep 32, pop
    movl (%_addrq), %_tmp
    add $4, %_esp
    gret 1

.macro x name, reg
    .gadget addr_\name
        movl %\reg, %_addr
        addl (%_ip), %_addr
        gret 1
.endm
.each_reg x
.purgem x
.gadget addr_none
    movl (%_ip), %_addr
    gret 1
.gadget_list addr, REG_LIST

.macro x name, reg
    .irp times, 1,2,4,8
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

.pushsection_rodata
.global.name si_gadgets
.irp reg, REG_LIST
    .irp times, 1,2,4,8
        .quad NAME(gadget_si_\reg\()_\times)
    .endr
.endr
.popsection

.gadget seg_gs
    addl CPU_tls_ptr(%_cpu), %_addr
    gret

.irp type, read,write

.global handle_\type\()_miss
handle_\type\()_miss:
    subq $8, %rsp
    save_c
    # %tlb actually points to tlb->entries
    leaq -TLB_entries(%_tlb), %rdi
    movl %_addr, %esi
    .ifc \type,read
        movq $0, %rdx
    .else
        movq $1, %rdx
    .endif
    call NAME(tlb_handle_miss)
    movq %rax, %r15
    restore_c
    testq %r15, %r15
    jz segfault_\type
    movq %r15, %_addrq
    addq $8, %rsp
    ret

segfault_\type:
    movl -TLB_entries+TLB_segfault_addr(%_tlb), %_addr
    movl %_addr, CPU_segfault_addr(%_cpu)
    .ifc \type,read
        movb $0, CPU_segfault_was_write(%_cpu)
    .else
        movb $1, CPU_segfault_was_write(%_cpu)
    .endif
    movq (%_ip), %_ip
    leal -4(%_esp), %r14d
    btq $62, %_ip
    cmovcl %r14d, %_esp
    movl %_eip, %_eip
    movl $INT_GPF, %_tmp
    jmp jit_exit

.endr

.global crosspage_load
crosspage_load:
    save_c odd
    leaq -TLB_entries(%_tlb), %rdi
    movl %_addr, %esi
    leaq LOCAL_value(%_cpu), %rdx
    movq %r14, %rcx
    call NAME(__tlb_read_cross_page)
    movq %rax, %r14
    restore_c odd
    testq %r14, %r14
    jz segfault_read
    movl %_addr, LOCAL_value_addr(%_cpu)
    leaq LOCAL_value(%_cpu), %_addrq
    ret

.global crosspage_store
crosspage_store:
    save_c odd
    leaq -TLB_entries(%_tlb), %rdi
    movl LOCAL_value_addr(%_cpu), %esi
    leaq LOCAL_value(%_cpu), %rdx
    movq %r14, %rcx
    call NAME(__tlb_write_cross_page)
    movq %rax, %r14
    restore_c odd
    testq %r14, %r14
    jz segfault_write
    ret
