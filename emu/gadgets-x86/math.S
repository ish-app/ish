#include "gadgets.h"
# asmsyntax=gas (for vim)

.gadget load_addr
    movl %_addr, %_tmp
    gret

.macro setf_oc
    seto CPU_of(%_cpu)
    setc CPU_cf(%_cpu)
.endm
.macro setf_a src, dst
    movl \src, CPU_op1(%_cpu)
    movl \dst, CPU_op2(%_cpu)
    orl $AF_OPS, CPU_flags_res(%_cpu)
.endm
.macro setf_zsp res
    movl \res, CPU_res(%_cpu)
    orl $(ZF_RES|SF_RES|PF_RES), CPU_flags_res(%_cpu)
.endm

# cool thing where I just write a few nested loops and it generates all the gadgets automatically

.macro do_op op arg
    .ifc \op,load
        movl \arg, %_tmp
    .else; .ifc \op,store
        movl %_tmp, \arg
    .else
        .irp da_op, add,sub; .ifc \op,\da_op
            movl \arg, %r14d
            setf_a src=%r14d, dst=%_tmp
        .endif; .endr
        \op\()l \arg, %_tmp
        .irp da_op, add,sub; .ifc \op,\da_op
            setf_oc
        .endif; .endr
        setf_zsp %_tmp
    .endif; .endif
.endm

.irp op, load,store,add,and,sub,xor
    .macro x reg
        do_op \op, %\reg
    .endm
    .reg_gadgets \op

    .gadget \op\()_imm
        do_op \op, (%_ip)
        gret 1

    .gadget \op\()_mem32
        .ifc \op,store
            write_prep
        .else
            read_prep
        .endif
        do_op \op, (%_addrq)
        gret

    .section .rodata
    .gadget_array \op
    .previous
.endr

.irp op, inc,dec
    .gadget \op
        setf_a src=$1 dst=%_tmp
        \op\()l %_tmp
        seto CPU_of(%_cpu)
        setf_zsp %_tmp
        gret
.endr
