#include "gadgets.h"
# asmsyntax=gas (for vim)

.gadget load_addr
    movl %_addr, %_tmp
    gret

.macro setf_oc
    seto CPU_of(%_cpu)
    setc CPU_cf(%_cpu)
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
.macro setf_zsp res
    mov \res, CPU_res(%_cpu)
    orl $(ZF_RES|SF_RES|PF_RES), CPU_flags_res(%_cpu)
.endm

# cool thing where I just write a few nested loops and it generates all the gadgets automatically

# bwl
# bwd

#define ifin(thing, ...) irp da_op, __VA_ARGS__; .ifc thing,\da_op
#define endifin endif; .endr
#undef _tmp
#define _tmp r10
.macro _do_op op, arg, size, s, ss
    .ifc \op,load
        mov\ss \arg, %_tmp\s
    .else; .ifc \op,store
        mov\ss %_tmp\s, \arg
    .else
        .ifin(\op, add,sub)
            mov\ss \arg, %r14\s
            setf_a src=%r14\s, dst=%_tmp\s, ss=\ss
        .endifin
        .ifin(\op, and,xor)
            clearf_a
        .endifin
        \op\ss \arg, %_tmp\s
        .ifin(\op, add,sub)
            setf_oc
        .endifin
        setf_zsp %_tmp\s
    .endif; .endif
.endm
.macro do_op op, arg, size
    .ifc \size,8
        _do_op \op, \arg, \size, b, b
    .else; .ifc \size,32
        _do_op \op, \arg, \size, d, l
    .else
        .error "bad size"
    .endif; .endif
.endm
#undef _tmp
#define _tmp r10d

.irp op, load,store,add,and,sub,xor
    .macro x reg
        do_op \op, %\reg, 32
    .endm
    .reg_gadgets \op

    .ifnc \op,store
        .gadget \op\()_imm
            do_op \op, (%_ip), 32
            gret 1
    .endif

    .irp size, 8,32
        .gadget \op\()_mem\size
            .ifc \op,store
                write_prep
            .else
                read_prep
            .endif
            do_op \op, (%_addrq), \size
            gret
    .endr

    .section .rodata
    .gadget_array \op
    .previous
.endr

.irp op, inc,dec
    .gadget \op
        setf_a src=$1 dst=%_tmp, ss=l
        \op\()l %_tmp
        seto CPU_of(%_cpu)
        setf_zsp %_tmp
        gret
.endr
