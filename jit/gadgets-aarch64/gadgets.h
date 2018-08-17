#include "cpu-offsets.h"

# register assignments
#define _eax w20
#define _ebx w21
#define _ecx w22
#define _edx w23
#define _xdx x23
#define _esi w24
#define _edi w25
#define _ebp w26
#define _esp w27
#define _ip x28
#define _eip w28
#define _tmp w0
#define _xtmp x0
#define _cpu x1
#define _tlb x2
#define _addr w3
#define _xaddr x3

.extern jit_exit

.macro .gadget name
    .global gadget_\()\name
    gadget_\()\name :
.endm
.macro gret pop=0
    ldr x8, [_ip], 8
    br x8
.endm

# memory reading and writing
.irp type, read,write

.macro \type\()_prep size
    and x8, _xaddr, 0xfff
    cmp x8, (0x1000-(\size/8))
    b.hi 12f
    and x8, _xaddr, 0xfffff000
    ldr x8, [_tlb, (-TLB_entries+TLB_dirty_page)]
    ubfiz x9, _xaddr, 4, 10
    add x9, x9, _tlb
    .ifc \type,read
        ldr x10, [x9, TLB_ENTRY_page]
    .else
        ldr x10, [x9, TLB_ENTRY_page_if_writable]
    .endif
    cmp x8, x10
    b.ne 11f
    ldr x10, [x9, TLB_ENTRY_data_minus_addr]
    add _xaddr, x10, _xaddr, uxtx
10:

.pushsection .text.bullshit
11:
    bl handle_\type\()_miss
    b 10b
12:
    mov x19, (\size/8)
    bl crosspage_load
    b 10b
.popsection
.endm

.endr
.macro write_done size
    add x8, _cpu, LOCAL_value
    cmp x8, _xaddr
    b.eq 11f
10:
.pushsection .text.bullshit
11:
    mov x19, (\size/8)
    bl crosspage_store
    b 10b
.popsection
.endm

#define ifin(thing, ...) irp da_op, __VA_ARGS__; .ifc thing,\da_op
#define endifin endif; .endr

# sync with enum reg
#define REG_LIST reg_a,reg_c,reg_d,reg_b,reg_sp,reg_bp,reg_si,reg_di
# sync with enum arg
#define GADGET_LIST REG_LIST,imm,mem,addr,gs
# sync with enum size
#define SIZE_LIST 8,16,32

# an array of gadgets
.macro .pushsection_rodata
    .pushsection .data.rel.ro
.endm
.macro _gadget_array_start name
    .pushsection_rodata
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
        gadgets \type\()64, \list
        gadgets \type\()80, \list
    .popsection
.endm

.macro .gadget_array type
    .gadget_list_size \type, GADGET_LIST
.endm

.macro .each_reg macro:vararg
    \macro reg_a, _eax
    \macro reg_b, _ebx
    \macro reg_c, _ecx
    \macro reg_d, _edx
    \macro reg_si, _esi
    \macro reg_di, _edi
    \macro reg_bp, _ebp
    \macro reg_sp, _esp
.endm

.macro ss size, macro, args:vararg
    .ifnb \args
        .if \size == 8
            \macro \args, \size, b
        .elseif \size == 16
            \macro \args, \size, h
        .elseif \size == 32
            \macro \args, \size,
        .else
            .error "bad size"
        .endif
    .else
        .if \size == 8
            \macro \size, b
        .elseif \size == 16
            \macro \size, h
        .elseif \size == 32
            \macro \size,
        .else
            .error "bad size"
        .endif
    .endif
.endm

.macro setf_c
    cset w10, cc
    strb w10, [_cpu, CPU_cf]
.endm
.macro setf_oc
    cset w10, vs
    strb w10, [_cpu, CPU_of]
    setf_c
.endm
.macro setf_a src, dst
    str \src, [_cpu, CPU_op1]
    str \dst, [_cpu, CPU_op2]
    ldr w10, [_cpu, CPU_flags_res]
    bic w10, w10, AF_OPS
    str w10, [_cpu, CPU_flags_res]
.endm
.macro clearf_a
    ldr w10, [_cpu, CPU_eflags]
    ldr w11, [_cpu, CPU_flags_res]
    bic w10, w10, AF_FLAG
    bic w11, w11, AF_OPS
    str w10, [_cpu, CPU_eflags]
    str w11, [_cpu, CPU_flags_res]
.endm
.macro clearf_oc
    strb wzr, [_cpu, CPU_of]
    strb wzr, [_cpu, CPU_cf]
.endm
.macro setf_zsp s
    .ifnb \s
        sxt\s _tmp, _tmp
    .endif
    str _tmp, [_cpu, CPU_res]
    ldr w10, [_cpu, CPU_flags_res]
    orr w10, w10, (ZF_RES|SF_RES|PF_RES)
    str w10, [_cpu, CPU_flags_res]
.endm

.macro save_c
    stp x0, x1, [sp, -0x50]!
    stp x2, x3, [sp, 0x10]
    stp x8, x9, [sp, 0x20]
    stp x10, x11, [sp, 0x30]
    stp x12, x13, [sp, 0x40]
.endm
.macro restore_c
    ldp x12, x13, [sp, 0x40]
    ldp x10, x11, [sp, 0x30]
    ldp x8, x9, [sp, 0x20]
    ldp x2, x3, [sp, 0x10]
    ldp x0, x1, [sp], 0x50
.endm

.macro uxts dst, src, s=
    .ifnb \s
        uxt\s \dst, \src
        .exitm
    .endif
    .ifnc \dst,\src
        mov \dst, \src
    .endif
.endm

# vim: ft=gas
