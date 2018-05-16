#include "cpu-offsets.h"

# register assignments
#define _esp r8d
#define _ip r9
#define _eip r9d
#define _tmp r10d
#define _cpu r11
#define _tlb r12
#define _addr r13d
#define _addrq r13

.extern jit_exit

.macro .gadget name
    .global gadget_\()\name
    gadget_\()\name :
.endm
.macro gret pop=0
    addq $((\pop+1)*8), %_ip
    jmp *-8(%_ip)
.endm

# using a gas macro for this works fine on gcc but not on clang
#define REG_LIST eax,ecx,edx,ebx,esp,ebp,esi,edi

# memory reading and writing
# TODO cross-page access handling (but it's going to be so slow :cry:)
.irp type, read,write

.macro \type\()_prep
    movl %_addr, %r14d
    shrl $8, %r14d
    andl $0x3ff0, %r14d
    movl %_addr, %r15d
    andl $0xfffff000, %r15d
    .ifc \type,read
        cmpl TLB_ENTRY_page(%_tlb,%r14), %r15d
    .else
        cmpl TLB_ENTRY_page_if_writable(%_tlb,%r14), %r15d
    .endif
    movl %r15d, -TLB_entries+TLB_dirty_page(%_tlb)
    je 1f
    call handle_\type\()_miss
1:
    addq TLB_ENTRY_data_minus_addr(%_tlb,%r14), %_addrq
.endm

.endr

# a gadget for each register
.macro .reg_gadgets type
    .irp reg, REG_LIST
        .gadget \type\()_\reg
        .ifnc \reg,esp
            x \reg
        .else
            x _esp
        .endif
        gret
    .endr
    .purgem x
.endm

# an array of gadgets
.macro .gadget_array_list type, list:vararg
    .global \type\()_gadgets
    .type \type\()_gadgets,@object
    \type\()_gadgets:
        .irp arg, \list 
            .ifndef gadget_\type\()_\arg
                .set gadget_\type\()_\arg, 0
            .endif
            .quad gadget_\type\()_\arg
        .endr
.endm

.macro .gadget_array type
    # This should stay in sync with enum arg in emu/gen.c
    .gadget_array_list \type, eax,ecx,edx,ebx,esp,ebp,esi,edi,ax,cx,dx,bx,sp,bp,si,di,imm,mem32,mem8,addr
.endm

.macro save_c
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
.endm
.macro restore_c
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
.endm

# vim: ft=gas
