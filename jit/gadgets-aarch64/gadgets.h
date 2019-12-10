#include "../gadgets-generic.h"
#include "cpu-offsets.h"

# register assignments
eax .req w20
xax .req x20
ebx .req w21
ecx .req w22
xcx .req x22
edx .req w23
xdx .req x23
esi .req w24
edi .req w25
ebp .req w26
esp .req w27
_ip .req x28
eip .req w28
_tmp .req w0
_xtmp .req x0
_cpu .req x1
_tlb .req x2
_addr .req w3
_xaddr .req x3

.extern jit_exit

.macro .gadget name
    .global NAME(gadget_\()\name)
    .align 4
    NAME(gadget_\()\name) :
.endm
.macro gret pop=0
    ldr x8, [_ip, \pop*8]!
    add _ip, _ip, 8 /* TODO get rid of this */
    br x8
.endm

# memory reading and writing
.irp type, read,write

.macro \type\()_prep size, id
    and w8, _addr, 0xfff
    cmp x8, (0x1000-(\size/8))
    b.hi crosspage_load_\id
    and w8, _addr, 0xfffff000
    str w8, [_tlb, (-TLB_entries+TLB_dirty_page)]
    ubfx x9, _xaddr, 12, 10
    eor x9, x9, _xaddr, lsr 22
    lsl x9, x9, 4
    add x9, x9, _tlb
    .ifc \type,read
        ldr w10, [x9, TLB_ENTRY_page]
    .else
        ldr w10, [x9, TLB_ENTRY_page_if_writable]
    .endif
    cmp w8, w10
    b.ne handle_miss_\id
    ldr x10, [x9, TLB_ENTRY_data_minus_addr]
    add _xaddr, x10, _xaddr, uxtx
back_\id:
.endm

.macro \type\()_bullshit size, id
handle_miss_\id :
    bl handle_\type\()_miss
    b back_\id
crosspage_load_\id :
    mov x19, (\size/8)
    bl crosspage_load
    b back_\id
.ifc \type,write
crosspage_store_\id :
    mov x19, (\size/8)
    bl crosspage_store
    b back_write_done_\id
.endif
.endm

.endr
.macro write_done size, id
    add x8, _cpu, LOCAL_value
    cmp x8, _xaddr
    b.eq crosspage_store_\id
back_write_done_\id :
.endm

.macro .each_reg macro:vararg
    \macro reg_a, eax
    \macro reg_b, ebx
    \macro reg_c, ecx
    \macro reg_d, edx
    \macro reg_si, esi
    \macro reg_di, edi
    \macro reg_bp, ebp
    \macro reg_sp, esp
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
    orr w10, w10, AF_OPS
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
.macro setf_zsp s, val=_tmp
    .ifnb \s
        sxt\s \val, \val
    .endif
    str \val, [_cpu, CPU_res]
    ldr w10, [_cpu, CPU_flags_res]
    orr w10, w10, (ZF_RES|SF_RES|PF_RES)
    str w10, [_cpu, CPU_flags_res]
.endm

.macro save_c
    stp x0, x1, [sp, -0x60]!
    stp x2, x3, [sp, 0x10]
    stp x8, x9, [sp, 0x20]
    stp x10, x11, [sp, 0x30]
    stp x12, x13, [sp, 0x40]
    str lr, [sp, 0x50]
.endm
.macro restore_c
    ldr lr, [sp, 0x50]
    ldp x12, x13, [sp, 0x40]
    ldp x10, x11, [sp, 0x30]
    ldp x8, x9, [sp, 0x20]
    ldp x2, x3, [sp, 0x10]
    ldp x0, x1, [sp], 0x60
.endm

.macro movs dst, src, s
    .ifc \s,h
        bfxil \dst, \src, 0, 16
    .else N .ifc \s,b
        bfxil \dst, \src, 0, 8
    .else
        mov \dst, \src
    .endif N .endif
.endm
.macro op_s op, dst, src1, src2, s
    .ifb \s
        \op \dst, \src1, \src2
    .else
        movs w10, \dst, \s
        \op w10, \src1, \src2
        movs \dst, w10, \s
    .endif
.endm
.macro ldrs src, dst, s
    ldr\s w10, \dst
    movs \src, w10, \s
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

.macro load_regs
    ldr eax, [_cpu, CPU_eax]
    ldr ebx, [_cpu, CPU_ebx]
    ldr ecx, [_cpu, CPU_ecx]
    ldr edx, [_cpu, CPU_edx]
    ldr esi, [_cpu, CPU_esi]
    ldr edi, [_cpu, CPU_edi]
    ldr ebp, [_cpu, CPU_ebp]
    ldr esp, [_cpu, CPU_esp]
.endm

.macro save_regs
    str eax, [_cpu, CPU_eax]
    str ebx, [_cpu, CPU_ebx]
    str ecx, [_cpu, CPU_ecx]
    str edx, [_cpu, CPU_edx]
    str edi, [_cpu, CPU_edi]
    str esi, [_cpu, CPU_esi]
    str ebp, [_cpu, CPU_ebp]
    str esp, [_cpu, CPU_esp]
    str eip, [_cpu, CPU_eip]
.endm

# vim: ft=gas
