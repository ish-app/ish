#include "gadgets.h"

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

.gadget cmpxchg32_mem
    write_prep 32, cmpxchg32_mem
    ldr w8, [_xaddr]
    subs w9, eax, w8
    setf_zsp val=w9
    setf_a eax, w8
    setf_oc
    csel eax, w8, eax, ne
    csel w8, _tmp, w8, eq
    cset w9, eq
    str w8, [_xaddr]
    write_done 32, cmpxchg32_mem
    gret 1
    write_bullshit 32, cmpxchg32_mem
.gadget_array cmpxchg

.gadget atomic_cmpxchg32_mem
    write_prep 32, atomic_cmpxchg32_mem
    mov w12, eax
    ldr w11, [_xaddr]
1:
    mov w8, w11
    subs w9, eax, w8
    setf_zsp val=w9
    setf_a eax, w8
    setf_oc
    csel eax, w8, eax, ne
    csel w8, _tmp, w8, eq
    cset w9, eq

    # all that setf stuff writes to memory which means instead of just using
    # ldaxr and stlxr we now have to do *another* compare-and-exchange
2:
    ldaxr w10, [_xaddr]
    cmp w10, w11
    b.ne 3f
    stlxr w10, w8, [_xaddr]
    cbnz w10, 2b

    write_done 32, atomic_cmpxchg32_mem
    gret 1
    write_bullshit 32, atomic_cmpxchg32_mem
3:
    dmb ish
    mov w11, w10
    mov eax, w12
    b 1b
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

.gadget fstsw_ax
    ldrh w10, [_cpu, CPU_fsw]
    movs eax, w10, h
    gret
