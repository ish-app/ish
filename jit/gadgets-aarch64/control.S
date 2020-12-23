#include "gadgets.h"

.gadget call
    // save return address
    sub _addr, esp, 4
    write_prep 32, call
    ldr w8, [_ip, 16]
    str w8, [_xaddr]
    // push stack pointer
    sub esp, esp, 4
    // save ip-to-arguments to return cache
    ubfx w12, w8, 4, 12
    write_done 32, call // clobbers w8
    add x13, _cpu, LOCAL_ret_cache
    str _ip, [x13, x12, lsl 3]
    // jump to target
    ldr _ip, [_ip, 32]
    b jit_ret_chain
    write_bullshit 32, call

.gadget call_indir
    // save return address
    sub _addr, esp, 4
    write_prep 32, call_indir
    ldr w8, [_ip, 16]
    str w8, [_xaddr]
    // push stack pointer
    sub esp, esp, 4
    // save ip-to-arguments to return cache
    ubfx w12, w8, 4, 12
    write_done 32, call_indir // clobbers w8
    add x13, _cpu, LOCAL_ret_cache
    str _ip, [x13, x12, lsl 3]
    // jump to target
    mov eip, _tmp
    b jit_ret
    write_bullshit 32, call_indir

.gadget ret
    mov _addr, esp
    // load return address and save to _tmp
    read_prep 32, ret
    ldr _tmp, [_xaddr]
    // pop stack pointer
    ldr w8, [_ip, 8]
    add esp, esp, w8
    // load saved ip in return cache
    ubfx w12, _tmp, 4, 12
    add x13, _cpu, LOCAL_ret_cache
    ldr _ip, [x13, x12, lsl 3]
    // found?
    cbz _ip, 2f
    // check if we jumped to the correct CALL instruction
    ldr w9, [_ip, 16]
    ldr x8, [_ip, 8]
    cmp _tmp, w9
    b.ne 1f
    // good, now do return chaining, the logic is similar to `jit_ret_chain`
    ldr _ip, [_ip, 24]
    cmp _ip, 0
    b.lt 1f
    sub x8, _ip, JIT_BLOCK_code
    str x8, [_cpu, LOCAL_last_block]
    gret
1:
    str x8, [_cpu, LOCAL_last_block]
    // fallthrough
2:
    mov eip, _tmp
    b jit_ret
    read_bullshit 32, ret

.gadget jmp_indir
    mov eip, _tmp
    b jit_ret
.gadget jmp
    ldr _ip, [_ip]
    b jit_ret_chain
.gadget jcxz
    cbnz ecx, 1f
    ldr _ip, [_ip]
    b jit_ret_chain
1:
    ldr _ip, [_ip, 8]
    b jit_ret_chain

#define COND_LIST o,c,z,cz,s,p,sxo,sxoz

.macro check_res
    cmpl $0, CPU_res(%_cpu)
.endm
.macro check_cf
    cmpb $0, CPU_cf(%_cpu)
.endm

.macro check_res_or_flag resflag, flag, target, fallthrough
    ldr w8, [_cpu, CPU_flags_res]
    tbnz w8, \resflag, 2f
    ldr w8, [_cpu, CPU_eflags]
    tbnz w8, \flag, \target
    b \fallthrough
2:
.endm

.macro do_jump cond, target
    # please tell me if you know a better way
    .ifc \cond,o
        ldrb w8, [_cpu, CPU_of]
        cbnz w8, \target
    .else N .ifc \cond,c
        ldrb w8, [_cpu, CPU_cf]
        cbnz w8, \target
    .else N .ifc \cond,z
        check_res_or_flag 1/*ZF_RES*/, 6/*ZF_FLAG*/, \target, 3f
        ldr w8, [_cpu, CPU_res]
        cbz w8, \target
    3:
    .else N .ifc \cond,cz
        ldrb w8, [_cpu, CPU_cf]
        cbnz w8, \target
        check_res_or_flag 1/*ZF_RES*/, 6/*ZF_FLAG*/, \target, 3f
        ldr w8, [_cpu, CPU_res]
        cbz w8, \target
    3:
    .else N .ifc \cond,s
        check_res_or_flag 2/*SF_RES*/, 7/*SF_FLAG*/, \target, 3f
        ldr w8, [_cpu, CPU_res]
        cmp w8, 0
        b.lt \target
    3:
    .else N .ifc \cond,p
        check_res_or_flag 0/*PF_RES*/, 2/*PF_FLAG*/, \target, 3f
        # this is so sad
        ldr w8, [_cpu, CPU_res]
        uxtb w8, w8
        fmov s0, w8
        cnt v0.8b, v0.8b
        uaddlv h0, v0.8b
        fmov w8, s0
        tbz w8, 0, \target
    3:
    .else N .ifc \cond,sxo
        ldr w8, [_cpu, CPU_res]
        cmp w8, 0
        cset w8, lt
        ldrb w9, [_cpu, CPU_of]
        cmp w8, w9
        b.ne \target
    .else N .ifc \cond,sxoz
        ldr w8, [_cpu, CPU_res]
        cmp w8, 0
        b.eq \target
        cset w8, lt
        ldrb w9, [_cpu, CPU_of]
        cmp w8, w9
        b.ne \target
    .endif N .endif N .endif N .endif N .endif N .endif N .endif N .endif
.endm

.irp cond, COND_LIST
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
    bl NAME(helper_collapse_flags)
    restore_c

    sub esp, esp, 4
    mov _addr, esp
    write_prep 32, pushf
    ldr w8, [_cpu, CPU_eflags]
    str w8, [_xaddr]
    write_done 32, pushf
    gret
    write_bullshit 32, pushf

.gadget popf
    mov _addr, esp
    read_prep 32, popf
    ldr w8, [_xaddr]
    str w8, [_cpu, CPU_eflags]
    add esp, esp, 4

    save_c
    mov x0, _cpu
    bl NAME(helper_expand_flags)
    restore_c
    gret
    read_bullshit 32, popf

.gadget sahf
    ubfx w8, eax, 8, 8
    strb w8, [_cpu, CPU_eflags]
    save_c
    mov x0, _cpu
    bl NAME(helper_expand_flags)
    restore_c
    gret
