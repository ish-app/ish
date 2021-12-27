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
        .ifnb \s
            cmp w9, 0
            and w9, eax, (-1 << \size)
            orr w8, w8, w9
        .endif
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

.extern segfault_write

.gadget atomic_cmpxchg8b
    #Test for alignment.
    tst _addr, 0x7
    b.ne 2f

    #cmpxchg8b via aligned exclusive 8b load
    write_prep 64, atomic_cmpxchg8b
    mov w9, eax
    bfi x9, xdx, 32, 32
    mov w10, ebx
    bfi x10, xcx, 32, 32

1:
    ldaxr x8, [_xaddr]
    cmp x9, x8
    csel x9, x8, x9, ne
    csel x8, x10, x8, eq
    cset w11, eq
    stlxr w12, x8, [_xaddr]
    cbnz w12, 1b
    write_done 64, atomic_cmpxchg8b
    ubfx xax, x9, 0, 32
    ubfx xdx, x9, 32, 32

    ldr w8, [_cpu, CPU_flags_res]
    ldr w9, [_cpu, CPU_eflags]
    and w8, w8, ~ZF_RES
    bfi w9, w11, 6, 1
    str w8, [_cpu, CPU_flags_res]
    str w9, [_cpu, CPU_eflags]
    gret 1
    write_bullshit 64, atomic_cmpxchg8b

2:  #All unaligned paths
    b segfault_write


.gadget cmpxchg8b
    write_prep 64, cmpxchg8b
    mov w9, eax
    bfi x9, xdx, 32, 32
    mov w10, ebx
    bfi x10, xcx, 32, 32

    ldr x8, [_xaddr]
    cmp x9, x8
    csel x9, x8, x9, ne
    csel x8, x10, x8, eq
    cset w11, eq
    str x8, [_xaddr]
    write_done 64, cmpxchg8b
    ubfx xax, x9, 0, 32
    ubfx xdx, x9, 32, 32

    ldr w8, [_cpu, CPU_flags_res]
    ldr w9, [_cpu, CPU_eflags]
    and w8, w8, ~ZF_RES
    bfi w9, w11, 6, 1
    str w8, [_cpu, CPU_flags_res]
    str w9, [_cpu, CPU_eflags]
    gret 1
    write_bullshit 64, cmpxchg8b

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
            ldr x8, [_ip, 8]
        .endifin
        .ifin(\type, 0,1,2)
            ldr x8, [_ip]
        .endifin
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

.macro do_vec_helper rm, _imm, size=
    .gadget vec_helper_\rm\size\_imm
        .ifin(\rm, read,write)
            \rm\()_prep (\size), vec_helper_\rm\size\_imm
        .endifin
        save_regs
        save_c
        mov x0, _cpu

        # the argument order should be a consistent src, dst
        .ifc \rm,reg
            # src
            ldrh w1, [_ip, 8]
            add x1, x0, x1
            # dst
            ldrh w2, [_ip, 10]
            add x2, x0, x2
        .endif
        .ifc \rm,read
            # src
            mov x1, _xaddr
            # dst
            ldrh w2, [_ip, 16]
            add x2, x0, x2
        .endif
        .ifc \rm,write
            # src
            ldrh w1, [_ip, 16]
            add x1, x0, x1
            # dst
            mov x2, _xaddr
        .endif
        .ifc \rm,imm
            # src
            ldrh w1, [_ip, 8]
            # dst
            ldrh w2, [_ip, 10]
            add x2, x0, x2
        .endif

        .ifc _imm,_imm
            # imm for third argument
            .ifin(\rm, reg)
                ldr w3, [_ip, 12]
            .endifin
            .ifin(\rm, read,write)
                ldr w3, [_ip, 20]
            .endifin
        .endif

        .ifin(\rm, read,write)
            ldr x8, [_ip, 8]
        .endifin
        .ifin(\rm, reg,imm)
            ldr x8, [_ip]
        .endifin
        blr x8

        restore_c
        load_regs
        .ifc \rm,write
            write_done (\size), vec_helper_\rm\size\_imm
        .endif
        .ifin(\rm, reg,imm)
            gret 2
        .endifin
        .ifin(\rm, read,write)
            gret 3
        .endifin
        .ifc \rm,read
            read_bullshit (\size), vec_helper_\rm\size\_imm
        .else N .ifc \rm,write
            write_bullshit (\size), vec_helper_\rm\size\_imm
        .endif N .endif
.endm

.irp _imm, ,_imm
    .irp rm, reg,imm
        do_vec_helper \rm, \_imm
    .endr
    .irp size, SIZE_LIST,64,128
        do_vec_helper read, \_imm, \size
        do_vec_helper write, \_imm, \size
    .endr
.endr

.gadget fstsw_ax
    ldrh w10, [_cpu, CPU_fsw]
    movs eax, w10, h
    gret

