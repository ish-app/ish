#include "../gadgets-generic.h"

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
    .global.name gadget_\()\name
.endm
.macro gret pop=0
    addq $((\pop+1)*8), %_ip
    jmp *-8(%_ip)
.endm

# memory reading and writing
.irp type, read,write

.macro \type\()_prep size, id
    movl %_addr, %r14d
    shrl $12, %r14d
    andl $0x3ff, %r14d
    movl %_addr, %r15d
    shrl $22, %r15d
    xor %r15d, %r14d
    shll $4, %r14d
    movl %_addr, %r15d
    andl $0xfff, %r15d
    cmpl $(0x1000-(\size/8)), %r15d
    ja crosspage_load_\id
    movl %_addr, %r15d
    andl $0xfffff000, %r15d
    .ifc \type,read
        cmpl TLB_ENTRY_page(%_tlb,%r14), %r15d
    .else
        cmpl TLB_ENTRY_page_if_writable(%_tlb,%r14), %r15d
    .endif
    movl %r15d, -TLB_entries+TLB_dirty_page(%_tlb)
    jne handle_miss_\id
    addq TLB_ENTRY_data_minus_addr(%_tlb,%r14), %_addrq
back_\id :

.pushsection_bullshit
handle_miss_\id :
    call handle_\type\()_miss
    jmp back_\id
crosspage_load_\id :
    movq $(\size/8), %r14
    call crosspage_load
    jmp back_\id
.popsection
.endm

.endr
.macro write_done size, id
    leaq LOCAL_value(%_cpu), %r14
    cmpq %_addrq, %r14
    je crosspage_store_\id
back_write_done_\id :
.pushsection_bullshit
crosspage_store_\id :
    movq $(\size/8), %r14
    call crosspage_store
    jmp back_write_done_\id
.popsection
.endm

.macro _invoke size, reg, post, macro:vararg
    .if \size == 32
        \macro reg_\reg, e\reg\post
    .else
        \macro reg_\reg, \reg\post
    .endif
.endm
.macro .each_reg_size size, macro:vararg
    .irp reg, a,b,c,d
        _invoke \size, \reg, x, \macro
    .endr
    .irp reg, si,di,bp
        _invoke \size, \reg, , \macro
    .endr
    .if \size == 32
        \macro reg_sp, _esp
    .else
        \macro reg_sp, _sp
    .endif
.endm
.macro .each_reg macro:vararg
    .each_reg_size 32, \macro
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
#if __APPLE__
#define DOLLAR(x) $$x
#else
#define DOLLAR(x) $x
#endif
.macro clearf_oc
    movl DOLLAR(0), CPU_of(%_cpu)
    movl DOLLAR(0), CPU_cf(%_cpu)
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
    sub DOLLAR(8), %rsp # 16 byte alignment is so annoying
.endm
.macro restore_c
    add DOLLAR(8), %rsp
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

.macro load_regs
    movl CPU_eax(%_cpu), %eax
    movl CPU_ebx(%_cpu), %ebx
    movl CPU_ecx(%_cpu), %ecx
    movl CPU_edx(%_cpu), %edx
    movl CPU_esi(%_cpu), %esi
    movl CPU_edi(%_cpu), %edi
    movl CPU_ebp(%_cpu), %ebp
    movl CPU_esp(%_cpu), %_esp
.endm

.macro save_regs
    movl %eax, CPU_eax(%_cpu)
    movl %ebx, CPU_ebx(%_cpu)
    movl %ecx, CPU_ecx(%_cpu)
    movl %edx, CPU_edx(%_cpu)
    movl %esi, CPU_esi(%_cpu)
    movl %edi, CPU_edi(%_cpu)
    movl %ebp, CPU_ebp(%_cpu)
    movl %_esp, CPU_esp(%_cpu)
.endm

# vim: ft=gas
