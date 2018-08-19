#include "gadgets.h"

.gadget load32_addr
    mov _tmp, _addr
    gret

.gadget load16_gs
    ldrh _tmp, [_cpu, #CPU_gs]
    gret

.gadget store16_gs
    strh _tmp, [_cpu, #CPU_gs]
    gret

# this would have been just a few nice compact nested loops, but gas said "nuh uh"

.macro _do_op op, arg, size, s
    .ifc \op,load
        movs _tmp, \arg, \s
        uxts _tmp, _tmp, \s
        .exitm
    .else; .ifc \op,store
        movs \arg, _tmp, \s
        .exitm
    .endif; .endif

    /*
    .ifin(\op, add,sub,adc,sbb)
        mov\ss \arg, %r14\s
        setf_a src=%r14\s, dst=%tmp\s, ss=\ss
    .endifin
    */
    .ifin(\op, and,or,xor)
        clearf_a
        clearf_oc
    .endifin
    /*
    .ifin(\op, adc,sbb)
        btw $0, CPU_cf(%_cpu)
    .endifin
     */

    .ifc \op,xor
        op_s eor _tmp, _tmp, \arg
    .else; .ifc \op,and
        op_s and _tmp, _tmp, \arg
    .else; .ifc \op,add
        op_s add _tmp, _tmp, \arg
    .else
        hlt 0
    .endif; .endif; .endif

    /*
    \op\ss \arg, %tmp\s

    .ifin(\op, add,sub,adc,sbb,imul)
        setf_oc
    .endifin
    */
    .ifin(\op, add,sub,adc,sbb,and,or,xor)
        setf_zsp \s
    .endifin
    /*
    .ifin(\op, bsf,bsr)
        # I sure hope this isn't too hot
        setzb %r14b
        andb $~(1<<6), CPU_eflags(%_cpu)
        shlb $6, %r14b
        orb %r14b, CPU_eflags(%_cpu)
        andl $~ZF_RES, CPU_flags_res(%_cpu)
    .endifin
    */
.endm
.macro do_op op, size, arg
    ss \size, _do_op, \op, \arg
.endm

.macro do_reg_op op, size, reg
    .gadget \op\size\()_reg_\reg
        do_op \op, \size, e\reg\()x
        gret
.endm

.macro do_hi_op op, size, reg
    # xchg %\reg\()h, %\reg\()l
    #do_op \op, \size, %\reg\()l
    hlt 0
    # xchg %\reg\()h, %\reg\()l
.endm

.macro do_op_size op, size
    .ifnc \op,store
        .gadget \op\size\()_imm
            ldr w8, [_ip]
            do_op \op, \size, w8
            gret 1
    .endif

    .gadget \op\size\()_mem
        .ifc \op,store
            write_prep \size
        .else
            read_prep \size
        .endif
        ldr w8, [_xaddr]
        do_op \op, \size, w8
        .ifc \op,store
            str w8, [_xaddr]
            write_done \size
        .endif
        gret 1

    .irp reg, a,b,c,d
        do_reg_op \op, \size, \reg
    .endr

    .irp reg, si,di,sp,bp
        .gadget \op\size\()_reg_\reg
            .if \size == 8
                .ifc \reg,sp; do_hi_op \op, \size, a; .else
                .ifc \reg,bp; do_hi_op \op, \size, c; .else
                .ifc \reg,si; do_hi_op \op, \size, d; .else
                .ifc \reg,di; do_hi_op \op, \size, b
                .endif; .endif; .endif; .endif
            .else
                do_op \op, \size, e\reg
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

# unary operations (well, only one explicit operand)

.irp op, inc,dec
    .macro do_\op size, s
        # setf_a $1 %tmp\s
        # \op\()\ss %tmp\s
        # seto CPU_of(%_cpu)
        # setf_zsp
        hlt 0
    .endm
.endr
.macro do_sign_extend size, s, ss
    .if \size != 32
        # movs\ss\()l %tmp\s, %tmpd
    .endif
    hlt 0
.endm
.macro do_zero_extend size, s, ss
    .if \size != 32
        # movz\ss\()l %tmp\s, %tmpd
    .endif
    hlt 0
.endm
.macro do_div size, s, ss
    # div\ss %tmp\s
    hlt 0
.endm
.macro do_idiv size, s, ss
    # idiv\ss %tmp\s
    hlt 0
.endm
.macro do_mul size, s, ss
    # mul\ss %tmp\s
    # setf_oc
    hlt 0
.endm
.macro do_imul1 size, s, ss
    # imul\ss %tmp\s
    # setf_oc
    hlt 0
.endm
.macro do_neg size, s, ss
    # setf_a src=$0 dst=%tmp\s ss=\ss
    # neg\ss %tmp\s
    # setf_oc
    # setf_zsp %tmp\s, \ss
    hlt 0
.endm
.macro do_not size, s, ss
    # not\ss %tmp\s
    hlt 0
.endm

.irp op, inc,dec,sign_extend,zero_extend,div,idiv,mul,imul1,neg,not
    .irp size, SIZE_LIST
        .gadget \op\()_\size
            ss \size, do_\op
            gret
    .endr
    .gadget_list \op, SIZE_LIST
.endr

.gadget cvt_16
    tst eax, 0x8000
    cinv w8, wzr, ne
    bfxil edx, w8, 0, 16
    gret
.gadget cvt_32
    tst eax, 0x80000000
    cinv edx, wzr, ne
    gret
.gadget_list cvt, SIZE_LIST

.gadget cvte_16
    sxtb eax, w8
    bfxil eax, w8, 0, 16
    gret
.gadget cvte_32
    sxth eax, eax
    gret
.gadget_list cvte, SIZE_LIST
