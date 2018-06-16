#include "cpu-offsets.h"

# register assignments
#define _esp r8d
#define _sp r8w
#define _ip r9
#define _eip r9d
#define _tmp r10d
#define tmp r10
#define tmpd r10d
#define tmpw r10w
#define tmpb r10b
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

# memory reading and writing
# TODO cross-page access handling (but it's going to be so slow :cry:)
.irp type, read,write

.macro \type\()_prep size
    movl %_addr, %r14d
    shrl $8, %r14d
    andl $0x3ff0, %r14d
    .ifc \type,read
        movl %_addr, %r15d
        andl $0xfff, %r15d
        cmpl $(0x1000-(\size/8)), %r15d
        ja 12f
    .endif
    movl %_addr, %r15d
    andl $0xfffff000, %r15d
    .ifc \type,read
        cmpl TLB_ENTRY_page(%_tlb,%r14), %r15d
    .else
        cmpl TLB_ENTRY_page_if_writable(%_tlb,%r14), %r15d
    .endif
    movl %r15d, -TLB_entries+TLB_dirty_page(%_tlb)
    jne 11f
    addq TLB_ENTRY_data_minus_addr(%_tlb,%r14), %_addrq
10:

.pushsection .text.bullshit
11:
    call handle_\type\()_miss
    jmp 10b
    .ifc \type,read
    12:
        movq $(\size/8), %r14
        call handle_crosspage_read
        jmp 10b
    .endif
.popsection
.endm

.endr

#define ifin(thing, ...) irp da_op, __VA_ARGS__; .ifc thing,\da_op
#define endifin endif; .endr

# sync with enum reg
#define REG_LIST reg_a,reg_c,reg_d,reg_b,reg_sp,reg_bp,reg_si,reg_di
# sync with enum arg
#define GADGET_LIST REG_LIST,imm,mem,addr,gs
# sync with enum size
#define SIZE_LIST 8,16,32

# an array of gadgets
.macro _gadget_array_start name
    .pushsection .rodata
    .global \name\()_gadgets
    .type \name\()_gadgets,@object
    \name\()_gadgets:
.endm

.macro gadgets type, list:vararg
    .irp arg, \list
        .ifndef gadget_\type\()_\arg
            .set gadget_\type\()_\arg, 0
        .endif
        .quad gadget_\type\()_\arg
    .endr
.endm

.macro .gadget_list type, list:vararg
    _gadget_array_start \type
        gadgets \type, \list
    .popsection
.endm

.macro .gadget_list_size type, list:vararg
    _gadget_array_start \type
        # sync with enum size
        gadgets \type\()8, \list
        gadgets \type\()16, \list
        gadgets \type\()32, \list
    .popsection
.endm

.macro .gadget_array type
    .gadget_list_size \type, GADGET_LIST
.endm

.macro _invoke reg, post, macro:vararg
    \macro reg_\reg, e\reg\post
.endm
.macro .each_reg macro:vararg
    .irp reg, a,b,c,d
        _invoke \reg, x, \macro
    .endr
    .irp reg, si,di,bp
        _invoke \reg, , \macro
    .endr
    \macro reg_sp, _esp
.endm

.macro ss size, macro, args:vararg
    .ifnb \args
        .if \size == 8
            \macro \args, \size, b, b
        .elseif \size == 16
            \macro \args, \size, w, w
        .elseif \size == 32
            \macro \args, \size, d, l
        .else
            .error "bad size"
        .endif
    .else
        .if \size == 8
            \macro \size, b, b
        .elseif \size == 16
            \macro \size, w, w
        .elseif \size == 32
            \macro \size, d, l
        .else
            .error "bad size"
        .endif
    .endif
.endm

.macro setf_c
    setc CPU_cf(%_cpu)
.endm
.macro setf_oc
    seto CPU_of(%_cpu)
    setf_c
.endm
.macro setf_a src, dst, ss
    mov\ss \src, CPU_op1(%_cpu)
    mov\ss \dst, CPU_op2(%_cpu)
    orl $AF_OPS, CPU_flags_res(%_cpu)
.endm
.macro clearf_a
    andl $~AF_FLAG, CPU_eflags(%_cpu)
    andl $~AF_OPS, CPU_flags_res(%_cpu)
.endm
.macro clearf_oc
    movl $0, CPU_of(%_cpu)
    movl $0, CPU_cf(%_cpu)
.endm
.macro setf_zsp res, ss
    .ifnc \ss,l
        movs\ss\()l \res, %_tmp
    .endif
    movl %_tmp, CPU_res(%_cpu)
    orl $(ZF_RES|SF_RES|PF_RES), CPU_flags_res(%_cpu)
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
