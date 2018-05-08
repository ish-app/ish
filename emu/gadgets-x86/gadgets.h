#include "cpu-offsets.h"

# register assignments
#define xsp r8d
#define ip r9
#define tmp r10d
#define cpu r11
#define tlb r12
#define addr r13d
#define addrq r13

.macro .gadget name
    .global gadget_\()\name
    gadget_\()\name :
.endm
.macro gret pop=0
    addq $((\pop+1)*8), %ip
    jmp *-8(%ip)
.endm

# using a gas macro for this works fine on gcc but not on clang
#define each_reg irp reg, eax,ecx,edx,ebx,ebp,esp,esi,edi

# memory reading and writing
# TODO cross-page access handling (but it's going to be so slow :cry:)
.irp type, read,write

.macro \type\()_prep
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
1:
    addq TLB_ENTRY_data_minus_addr(%tlb,%r14), %addrq
.endm

.endr

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

# an array of gadgets
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
