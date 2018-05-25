#include "gadgets.h"
# asmsyntax=gas (for vim)

.gadget load32_addr
    movl %_addr, %_tmp
    gret

.gadget load16_gs
    movw CPU_gs(%_cpu), %r10w
    gret

.gadget store16_gs
    movw %r10w, CPU_gs(%_cpu)
    gret

# this would have been just a few nice compact nested loops, but gas said "nuh uh"

.macro _do_op op, arg, size, s, ss
    .ifc \op,load
        mov\ss \arg, %tmp\s
        .exitm
    .else; .ifc \op,store
        mov\ss %tmp\s, \arg
        .exitm
    .endif; .endif

    .ifin(\op, add,sub,adc,sbb)
        mov\ss \arg, %r14\s
        setf_a src=%r14\s, dst=%tmp\s, ss=\ss
    .endifin
    .ifin(\op, and,or,xor)
        clearf_a
        clearf_oc
    .endifin
    .ifin(\op, adc,sbb)
        btw $0, CPU_cf(%_cpu)
    .endifin
    \op\ss \arg, %tmp\s
    .ifin(\op, add,sub,adc,sbb)
        setf_oc
    .endifin
    .ifnc \op,imul
        setf_zsp %tmp\s, \ss
    .endif
.endm
.macro do_op op, size, arg
    ss \size, _do_op, \op, \arg
.endm

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
        gret 1

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

.irp op, load,store,add,sub,adc,sbb,and,or,xor
    .irp size, SIZE_LIST
        do_op_size \op, \size
    .endr
    .gadget_array \op
.endr
do_op_size imul, 16
do_op_size imul, 32
.gadget_array imul

# unary operations (well, only one explicit operand)
.irp op, inc,dec
    .gadget \op
        setf_a src=$1 dst=%_tmp, ss=l
        \op\()l %_tmp
        seto CPU_of(%_cpu)
        setf_zsp %_tmp, l
        gret
.endr

.macro do_div size, s, ss
    .gadget div_\size
        div\ss %r10\s
        gret
.endm
.macro do_idiv size, s, ss
    .gadget idiv_\size
        idiv\ss %r10\s
        gret
.endm
.macro do_neg size, s, ss
    .gadget neg_\size
        setf_a src=$0 dst=%r10\s ss=\ss
        neg\ss %r10\s
        setf_oc
        setf_zsp %r10\s, \ss
        gret
.endm
.macro do_not size, s, ss
    .gadget not_\size
        not\ss %r10\s
        gret
.endm

.irp op, idiv,div,neg,not
    .irp size, SIZE_LIST
        ss \size, do_\op
    .endr
    .gadget_list \op, SIZE_LIST
.endr

.gadget cvt_16
    cwd
    gret
.gadget cvt_32
    cdq
    gret
.gadget_list cvt, SIZE_LIST
