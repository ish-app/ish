#include "gadgets.h"
# asmsyntax=gas (for vim)

.gadget load_addr
    movl %_addr, %_tmp
    gret

# cool thing where I just write a few nested loops and it generates all the gadgets automatically

.macro do_op op arg
    .ifc \op,load
        movl \arg, %_tmp
    .else; .ifc \op,store
        movl %_tmp, \arg
    .else
        \op\()l \arg, %_tmp
        .irp da_op, add,sub; .ifc \op,\da_op
            seto CPU_of(%_cpu)
            setc CPU_cf(%_cpu)
            movl \arg, %r14d
            movl %r14d, CPU_op1(%_cpu)
            movl %_tmp, CPU_op2(%_cpu)
            orl $AF_OPS, CPU_flags_res(%_cpu)
        .endif; .endr
        movl %_tmp, CPU_res(%_cpu)
        orl $(PF_RES|ZF_RES|SF_RES), CPU_flags_res(%_cpu)
    .endif; .endif
.endm

.macro oc_flags
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
