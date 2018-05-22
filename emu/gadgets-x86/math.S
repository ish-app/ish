#include "gadgets.h"
# asmsyntax=gas (for vim)

.gadget load32_addr
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
.macro clearf_oc
    movl $0, CPU_of(%_cpu)
    movl $0, CPU_cf(%_cpu)
.endm
.macro setf_zsp res, ss
    .ifnc \ss,l
        movs\ss\()l \res, %_tmp
    .endif
    movl %_tmp, CPU_res(%_cpu)
    orl $(ZF_RES|SF_RES|PF_RES), CPU_flags_res(%_cpu)
.endm

# this would have been just a few nice compact nested loops, but gas said "nuh uh"

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
        .ifin(\op, and,or,xor)
            clearf_a
            clearf_oc
        .endifin
        \op\ss \arg, %_tmp\s
        .ifin(\op, add,sub)
            setf_oc
        .endifin
        setf_zsp %_tmp\s, \ss
    .endif; .endif
.endm
.macro do_op op, size, arg
    .ifc \size,8
        _do_op \op, \arg, \size, b, b
    .else; .ifc \size,16
        _do_op \op, \arg, \size, w, w
    .else; .ifc \size,32
        _do_op \op, \arg, \size, d, l
    .else
        .error "bad size"
    .endif; .endif; .endif
.endm
#undef _tmp
#define _tmp r10d

.macro do_reg_op op, size, reg
    .gadget \op\size\()_reg_\reg
        .if \size == 32
            do_op \op, \size, %e\reg\()x
        .elseif \size == 16
            do_op \op, \size, %\reg\()x
        .elseif \size == 8
            do_op \op, \size, %\reg\()l
        .endif
        gret
.endm

.macro do_hi_op op, size, reg
    xchg %\reg\()h, %\reg\()l
    do_op \op, \size, %\reg\()l
    xchg %\reg\()h, %\reg\()l
.endm

.macro do_op_size op, size
    .ifnc \op,store
        .gadget \op\size\()_imm
            do_op \op, \size, (%_ip)
            gret 1
    .endif

    .gadget \op\size\()_mem
        .ifc \op,store
            write_prep
        .else
            read_prep
        .endif
        do_op \op, \size, (%_addrq)
        gret

    .irp reg, a,b,c,d
        do_reg_op \op, \size, \reg
    .endr

    .irp reg, si,di,sp,bp
        .gadget \op\size\()_reg_\reg
            .if \size == 32
                .ifnc \reg,sp
                    do_op \op, \size, %e\reg
                .else
                    do_op \op, \size, %_esp
                .endif
            .elseif \size == 16
                .ifnc \reg,sp
                    do_op \op, \size, %\reg
                .else
                    do_op \op, \size, %_sp
                .endif
            .elseif \size == 8
                .ifc \reg,sp; do_hi_op \op, \size, a; .else
                .ifc \reg,bp; do_hi_op \op, \size, c; .else
                .ifc \reg,si; do_hi_op \op, \size, d; .else
                .ifc \reg,di; do_hi_op \op, \size, b
                .endif; .endif; .endif; .endif
            .endif
            gret
    .endr
.endm

.irp op, load,store,add,sub,and,or,xor
    .irp size, 8,16,32
        do_op_size \op, \size
    .endr

    .pushsection .rodata
    .gadget_array_start \op
        # sync with enum size
        .irp size, 8,16,32
            gadgets \op\size, GADGET_LIST
        .endr
    .popsection
.endr

.irp op, inc,dec
    .gadget \op
        setf_a src=$1 dst=%_tmp, ss=l
        \op\()l %_tmp
        seto CPU_of(%_cpu)
        setf_zsp %_tmp, l
        gret
.endr
