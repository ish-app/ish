#include "gadgets.h"
#include "math.h"

.gadget cpuid
    # regrettable
    save_c
    sub sp, sp, 0x10
    str eax, [sp, 0xc]
    str ebx, [sp, 0x8]
    str ecx, [sp, 0x4]
    str edx, [sp, 0x0]
    add x0, sp, 0xc
    add x1, sp, 0x8
    add x2, sp, 0x4
    mov x3, sp
    bl NAME(helper_cpuid)
    ldr eax, [sp, 0xc]
    ldr ebx, [sp, 0x8]
    ldr ecx, [sp, 0x4]
    ldr edx, [sp, 0x0]
    add sp, sp, 0x10
    restore_c
    gret

.macro do_cmpxchg size, s
    .gadget cmpxchg\size\()_mem
        write_prep \size, cmpxchg\size\()_mem
        ldr\s w8, [_xaddr]
        setf_a eax, w8
        mov w9, eax
        do_add sub, w9, w8, \s
        setf_zsp \s, val=w9
        .ifnb \s
            cmp w9, 0
            and w9, eax, (-1 << \size)
            orr w8, w8, w9
        .endif
        csel eax, w8, eax, ne
        csel w8, _tmp, w8, eq
        cset w9, eq
        str\s w8, [_xaddr]
        write_done \size, cmpxchg\size\()_mem
        gret 1
        write_bullshit \size, cmpxchg\size\()_mem

    .gadget atomic_cmpxchg\size\()_mem
        write_prep \size, atomic_cmpxchg\size\()_mem
        mov w12, eax
        ldr w11, [_xaddr]
    1:
        mov w8, w11
        setf_a eax, w8
        mov w9, eax
        do_add sub, w9, w8, \s
        setf_zsp \s, val=w9
        csel eax, w8, eax, ne
        csel w8, _tmp, w8, eq
        cset w9, eq

        # all that setf stuff writes to memory which means instead of just using
        # ldaxr and stlxr we now have to do *another* compare-and-exchange
    2:
        ldaxr\s w10, [_xaddr]
        cmp w10, w11
        b.ne 3f
        stlxr\s w10, w8, [_xaddr]
        cbnz w10, 2b

        write_done \size, atomic_cmpxchg\size\()_mem
        gret 1
        write_bullshit \size, atomic_cmpxchg\size\()_mem
    3:
        dmb ish
        mov w11, w10
        mov eax, w12
        b 1b
.endm

.irp size, SIZE_LIST
    ss \size, do_cmpxchg
.endr
.gadget_array cmpxchg
.gadget_array atomic_cmpxchg

.macro do_helper type, size=
    .gadget helper_\type\size
        .ifin(\type, read,write)
            \type\()_prep (\size), helper_\type\size
        .endifin
        save_regs
        save_c
        mov x0, _cpu
        .ifc \type,1
            ldr x1, [_ip, 8]
        .endif
        .ifc \type,2
            ldr x1, [_ip, 8]
            ldr x2, [_ip, 16]
        .endif
        .ifin(\type, read,write)
            mov x1, _xaddr
        .endifin
        ldr x8, [_ip]
        blr x8
        restore_c
        load_regs
        .ifc \type,write
            write_done (\size), helper_\type\size
        .endif
        .ifc \type,0
            gret 1
        .else N .ifc \type,2
            gret 3
        .else
            gret 2
        .endif N .endif
        .ifc \type,read
            read_bullshit (\size), helper_\type\size
        .else N .ifc \type,write
            write_bullshit (\size), helper_\type\size
        .endif N .endif
.endm
do_helper 0
do_helper 1
do_helper 2
.irp size, SIZE_LIST,64,80
    do_helper read, \size
    do_helper write, \size
.endr

# sync with enum vec_arg
#define VEC_ARG_LIST xmm,reg

# important: this is VEC_ARG_LIST^2
.gadget_list vec_helper_reg, xmm_xmm,xmm_reg,reg_xmm,reg_reg

.irp size, SIZE_LIST,64,128
    .gadget_list vec_helper_load\size\(), VEC_ARG_LIST
    .gadget_list vec_helper_store\size\(), VEC_ARG_LIST
.endr

.gadget fstsw_ax
    ldrh w10, [_cpu, CPU_fsw]
    movs eax, w10, h
    gret

