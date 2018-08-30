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

    .ifin(\op, add,sub,adc,sbc)
        setf_a \arg, _tmp
    .endifin
    .ifin(\op, and,orr,eor)
        clearf_a
        clearf_oc
    .endifin
    /*
    .ifin(\op, adc,sbc)
        btw $0, CPU_cf(%_cpu)
    .endifin
    */

    .ifin(\op, and,orr,eor)
        op_s \op, _tmp, _tmp, \arg, \s
    .endifin

    .ifin(\op, add,sub)
        # setting flags: a horror story
        .ifb \s
            # for 32-bit operands, we can just do the operation and the chip
            # will set v and c right, which we copy
            \op\()s _tmp, _tmp, \arg
            cset w10, vs
            strb w10, [_cpu, CPU_of]
            .ifc \op,add
                cset w10, cs
            .endif
            .ifc \op,sub
                cset w10, cc
            .endif
            strb w10, [_cpu, CPU_cf]
        .else
            # for 16 or 8 bit operands...
            # ok first figure out signed overflow
            sxt\s w10, _tmp
            \op w10, w10, \arg, sxt\s
            cmp w10, w10, sxt\s
            cset w10, ne
            strb w10, [_cpu, CPU_of]
            # now unsigned overflow
            uxt\s w10, _tmp
            \op _tmp, w10, \arg, uxt\s
            .ifc \s,b
                lsr w10, _tmp, 8
            .else
                lsr w10, _tmp, 16
            .endif
            strb w10, [_cpu, CPU_cf]
        .endif
    .endifin

    .ifin(\op, adc,sbc,xchg)
        # unimplemented
        hlt 0
    .endifin

    /*
    \op\ss \arg, %tmp\s

    .ifin(\op, add,sub,adc,sbc,imul)
        setf_oc
    .endifin
    */
    .ifin(\op, add,sub,adc,sbc,and,orr,eor)
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

.macro do_reg_op op, armop, size, reg
    .gadget \op\size\()_reg_\reg
        do_op \armop, \size, e\reg\()x
        gret
.endm

.macro do_hi_op op, size, reg
    # xchg %\reg\()h, %\reg\()l
    #do_op \op, \size, %\reg\()l
    hlt 0
    # xchg %\reg\()h, %\reg\()l
.endm

.macro do_op_size op, armop, size
    .ifnc \op,store
        .gadget \op\size\()_imm
            ldr w8, [_ip]
            do_op \armop, \size, w8
            gret 1
    .endif

    .gadget \op\size\()_mem
        .ifc \op,store
            write_prep \size
        .else
            read_prep \size
        .endif
        ldr w8, [_xaddr]
        do_op \armop, \size, w8
        .ifc \op,store
            str w8, [_xaddr]
            write_done \size
        .endif
        gret 1

    .irp reg, a,b,c,d
        do_reg_op \op, \armop, \size, \reg
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
                do_op \armop, \size, e\reg
            .endif
            gret
    .endr
.endm

.irp op, load,store,xchg,add,sub,adc,sbb,and,or,xor
    .irp size, SIZE_LIST
        # a couple operations have slightly different names on arm
        .ifc \op,xor
            do_op_size \op, eor, \size
        .else; .ifc \op,sbb
            do_op_size \op, sbc, \size
        .else; .ifc \op,or
            do_op_size \op, orr, \size
        .else
            do_op_size \op, \op, \size
        .endif; .endif; .endif
    .endr
    .gadget_array \op
.endr
.irp op, imul,bsf,bsr
    .irp size, 16,32
        do_op_size \op, \op, \size
    .endr
    .gadget_array \op
.endr

# unary operations (well, only one explicit operand)

# TODO OF, AF (not CF)
.macro do_inc size, s
    mov w10, 1
    setf_a w10, _tmp
    op_s add _tmp, _tmp, 1
    setf_zsp
.endm
.macro do_dec size, s
    mov w10, 1
    setf_a w10, _tmp
    op_s sub _tmp, _tmp, 1
    setf_zsp
.endm

.macro do_sign_extend size, s
    .if \size != 32
        # movs\ss\()l %tmp\s, %tmpd
        sxt\s _tmp, _tmp
    .endif
.endm
.macro do_zero_extend size, s
    .if \size != 32
        uxt\s _tmp, _tmp
    .endif
.endm
.macro do_div size, s
    .if \size == 8
        uxth w8, eax
        uxtb _tmp, _tmp
        udiv w9, w8, _tmp
        msub w10, w9, _tmp, w8
        bfi eax, w9, 0, 8
        bfi eax, w10, 8, 8
    .elseif \size == 16
        bfi w8, eax, 0, 16
        bfi w8, edx, 16, 16
        uxth _tmp, _tmp
        udiv w9, w8, _tmp
        msub w10, w9, _tmp, w8
        ubfx eax, w9, 0, 16
        ubfx edx, w10, 0, 16
    .elseif \size == 32
        bfi x8, xax, 0, 32
        bfi x8, xdx, 32, 32
        udiv x9, x8, _xtmp
        msub x10, x9, _xtmp, x8
        mov eax, w9
        mov edx, w10
    .endif
.endm
.macro do_idiv size, s
    # idiv\ss %tmp\s
    hlt 0
.endm
.macro do_mul size, s
    # mul\ss %tmp\s
    # setf_oc
    hlt 0
.endm
.macro do_imul1 size, s
    # imul\ss %tmp\s
    # setf_oc
    hlt 0
.endm
.macro do_neg size, s
    # setf_a src=$0 dst=%tmp\s ss=\ss
    # neg\ss %tmp\s
    # setf_oc
    # setf_zsp %tmp\s, \ss
    hlt 0
.endm
.macro do_not size, s
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
