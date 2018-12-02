#include "gadgets.h"

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

    .ifin(\op, add,sub,adc,sbb,imul)
        setf_oc
    .endifin
    .ifin(\op, add,sub,adc,sbb,and,or,xor)
        setf_zsp %tmp\s, \ss
    .endifin
    .ifin(\op, bsf,bsr)
        # I sure hope this isn't too hot
        setzb %r14b
        andb $~(1<<6), CPU_eflags(%_cpu)
        shlb $6, %r14b
        orb %r14b, CPU_eflags(%_cpu)
        andl $~ZF_RES, CPU_flags_res(%_cpu)
    .endifin
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
            write_prep \size, \op\size\()_mem
        .else
            read_prep \size, \op\size\()_mem
        .endif
        do_op \op, \size, (%_addrq)
        .ifc \op,store
            write_done \size, \op\size\()_mem
        .endif
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

.irp op, load,store,xchg,add,sub,adc,sbb,and,or,xor
    .irp size, SIZE_LIST
        do_op_size \op, \size
    .endr
    .gadget_array \op
.endr
.irp op, imul,bsf,bsr
    .irp size, 16,32
        do_op_size \op, \size
    .endr
    .gadget_array \op
.endr

# same as above, but only atomics
.macro _do_op_atomic op, arg, size, s, ss
    .ifin(\op, and,or,xor)
        clearf_a
        clearf_oc
    .endifin
    .ifin(\op, adc,sbb)
        btw $0, CPU_cf(%_cpu)
    .endifin

    mov\ss \arg, %r15\s
    xchg %r15, %rax # cmpxchg uses rax as implicit operand
    .ifc \op,xadd
        push %tmp
    .endif
1:
    mov %rax, %r14
    .ifc \op,xadd
        mov (%rsp), %tmp
        xchg %tmp, %r14
    .endif
    .ifin(\op, add,sub,adc,sbb,xadd)
        setf_a src=%tmp\s, dst=%r14\s, ss=\ss
    .endifin
    .ifin(\op, inc,dec)
        setf_a src=$1, dst=%r14\s, ss=\ss
    .endifin

    .ifin(\op, add,sub,adc,sbb,and,or,xor)
        \op\ss %tmp\s, %r14\s
    .endifin
    .ifc \op,xadd
        add\ss %tmp\s, %r14\s
    .endif
    .ifin(\op, inc,dec)
        \op\ss %r14\s
    .endifin
    lock cmpxchg\ss %r14\s, \arg
    jnz 1b
    mov %r15, %rax
    .ifc \op,xadd
        add $8, %rsp
    .endif

    .ifin(\op, add,sub,adc,sbb,xadd)
        setf_oc
    .endifin
    .ifin(\op, inc,dec)
        seto CPU_of(%_cpu)
    .endifin
    # setf_zsp is apparently broken in such a way that it can only use _tmp
    xchg %r14, %r10
    setf_zsp %tmp\s, \ss
    xchg %r14, %r10
.endm
.macro do_op_atomic op, size, arg
    ss \size, _do_op_atomic, \op, \arg
.endm

.macro do_op_size_atomic op, size
    .gadget atomic_\op\size\()_mem
        write_prep \size, atomic_\op\size\()_mem
        do_op_atomic \op, \size, (%_addrq)
        write_done \size, atomic_\op\size\()_mem
        gret 1
.endm

.irp op, add,sub,adc,sbb,and,or,xor,inc,dec,xadd
    .irp size, SIZE_LIST
        do_op_size_atomic \op, \size
    .endr
    .gadget_array atomic_\op
.endr

# unary operations (well, only one explicit operand)

.irp op, inc,dec
    .macro do_\op size, s, ss
        setf_a src=$1, dst=%tmp\s, ss=\ss
        \op\()\ss %tmp\s
        seto CPU_of(%_cpu)
        setf_zsp %tmp\s, \ss
    .endm
.endr
.macro do_sign_extend size, s, ss
    .if \size != 32
        movs\ss\()l %tmp\s, %tmpd
    .endif
.endm
.macro do_zero_extend size, s, ss
    .if \size != 32
        movz\ss\()l %tmp\s, %tmpd
    .endif
.endm
.macro do_div size, s, ss
    div\ss %tmp\s
.endm
.macro do_idiv size, s, ss
    idiv\ss %tmp\s
.endm
.macro do_mul size, s, ss
    mul\ss %tmp\s
    setf_oc
.endm
.macro do_imul1 size, s, ss
    imul\ss %tmp\s
    setf_oc
.endm
.macro do_not size, s, ss
    not\ss %tmp\s
.endm

.irp op, inc,dec,sign_extend,zero_extend,div,idiv,mul,imul1,not
    .irp size, SIZE_LIST
        .gadget \op\()_\size
            ss \size, do_\op
            gret
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

.gadget cvte_16
    cbw
    gret
.gadget cvte_32
    cwde
    gret
.gadget_list cvte, SIZE_LIST
