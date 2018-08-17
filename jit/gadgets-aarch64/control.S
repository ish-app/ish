#include "gadgets.h"

.gadget call
    sub _esp, _esp, 4
    mov _addr, _esp
    write_prep 32
    ldr w8, [_ip, 16]
    str w8, [_xaddr]
    write_done 32
    ldr _ip, [_ip, 8]
    b jit_ret_chain

.gadget call_indir
    sub _esp, _esp, 4
    mov _addr, _esp
    ldr w8, [_ip, 8]
    str w8, [_xaddr]
    write_done 32
    mov _eip, _tmp
    b jit_ret

.gadget ret
    mov _addr, _esp
    ldr w8, [_ip, 8]
    add _esp, _esp, w8
    read_prep 32
    ldr _eip, [_xaddr]
    b jit_ret

.gadget jmp_indir
    mov _eip, _tmp
    b jit_ret
.gadget jmp
    ldr _ip, [_ip]
    b jit_ret_chain
.gadget jcxz
    cbnz _ecx, 1f
    ldr _ip, [_ip]
    b jit_ret_chain
1:
    ldr _ip, [_ip, 8]
    b jit_ret_chain

#define COND_LIST o,c,z,cz,s,p,sxo,sxoz
#define IMPLEMENTED_CONDS c,z,cz,s,p,sxo,sxoz

.macro check_res
    cmpl $0, CPU_res(%_cpu)
.endm
.macro check_cf
    cmpb $0, CPU_cf(%_cpu)
.endm

.macro check_res_or_flag resflag, flag, target
    ldr w8, [_cpu, CPU_flags_res]
    tbz w8, \resflag, 2f
    ldr w8, [_cpu, CPU_eflags]
    tbz w8, \flag, 2f
    b \target
2:
.endm

.macro do_jump cond, target
    # please tell me if you know a better way
    .ifc \cond,o
    .else; .ifc \cond,c
        ldrb w8, [_cpu, CPU_cf]
        cbnz w8, \target
    .else; .ifc \cond,z
        check_res_or_flag 1/*ZF_RES*/, 6/*ZF_FLAG*/, \target
        ldr w8, [_cpu, CPU_res]
        cbz w8, \target
    .else; .ifc \cond,cz
        ldrb w8, [_cpu, CPU_cf]
        cbnz w8, \target
        check_res_or_flag 1/*ZF_RES*/, 6/*ZF_FLAG*/, \target
        ldr w8, [_cpu, CPU_res]
        cbz w8, \target
    .else; .ifc \cond,s
        check_res_or_flag 2/*SF_RES*/, 7/*SF_FLAG*/, \target
        ldr w8, [_cpu, CPU_res]
        cmp w8, 0
        b.lt \target
    .else; .ifc \cond,p
        check_res_or_flag 0/*PF_RES*/, 2/*PF_FLAG*/, \target
        # this is so sad
        ldr w8, [_cpu, CPU_res]
        uxtb w8, w8
        fmov s0, w8
        cnt v0.8b, v0.8b
        uaddlv h0, v0.8b
        fmov w8, s0
        tbz w8, 0, \target
    .else; .ifc \cond,sxo
        ldr w8, [_cpu, CPU_res]
        cmp w8, 0
        cset w8, lt
        ldrb w9, [_cpu, CPU_of]
        cmp w8, w9
        b.eq \target
    .else; .ifc \cond,sxoz
        ldr w8, [_cpu, CPU_res]
        cmp w8, 0
        b.eq \target
        cset w8, lt
        ldrb w9, [_cpu, CPU_of]
        cmp w8, w9
        b.eq \target
    .endif; .endif; .endif; .endif; .endif; .endif; .endif; .endif
.endm

.irp cond, IMPLEMENTED_CONDS
    .gadget jmp_\cond
        do_jump \cond, 1f
        ldr _ip, [_ip, 8]
        b jit_ret_chain
    1:  ldr _ip, [_ip]
        b jit_ret_chain

    .gadget set_\cond
        do_jump \cond, 1f
        mov _tmp, 0
        gret
    1:  mov _tmp, 1
        gret
    .gadget setn_\cond
        do_jump \cond, 1f
        mov _tmp, 1
        gret
    1:  mov _tmp, 0
        gret

    .gadget skip_\cond
        do_jump \cond, 1f
        gret 1
    1:  ldr x8, [_ip]
        add _ip, _ip, x8
        gret 1
    .gadget skipn_\cond
        do_jump \cond, 1f
        ldr x8, [_ip]
        add _ip, _ip, x8
    1:  gret 1
.endr
.gadget_list jmp, COND_LIST
.gadget_list set, COND_LIST
.gadget_list setn, COND_LIST
.gadget_list skip, COND_LIST
.gadget_list skipn, COND_LIST

.gadget pushf
    save_c
    mov x0, _cpu
    bl helper_collapse_flags
    restore_c

    sub _esp, _esp, 4
    mov _addr, _esp
    write_prep 32
    ldr x8, [_cpu, CPU_eflags]
    str x8, [_xaddr]
    write_done 32
    gret

.gadget popf
    mov _addr, _esp
    read_prep 32
    ldr x8, [_xaddr]
    add _esp, _esp, 4

    save_c
    mov x0, _cpu
    bl helper_expand_flags
    restore_c
    gret

.gadget sahf
    ubfx w8, _eax, 8, 8
    strb w8, [_cpu, CPU_eflags]
    save_c
    mov x0, _cpu
    bl helper_expand_flags
    restore_c
    gret
