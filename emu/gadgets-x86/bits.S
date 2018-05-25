#include "gadgets.h"

.macro do_shift type, size
    .irp arg, reg_c,imm
        .gadget \type\size\()_\arg
            .ifc \arg,imm
                pushq %rcx
                movb (%_ip), %cl
            .endif
            testb %cl, %cl
            jz 1f
            \type\()l %cl, %_tmp
            setf_oc
            setf_zsp %_tmp, l
            clearf_a
        1:
            .ifc \arg,imm
                popq %rcx
            .endif
            .ifc \arg,imm
                gret 1
            .else
                gret
            .endif
    .endr
.endm

.irp type, shl,shr,sar
    .irp size, 8,16,32
        do_shift \type, \size
    .endr
    .gadget_array \type
.endr

.irp arg, imm,cl
    .macro x name, reg
        .gadget shrd_\arg\()32_\name
            .ifc \arg,imm
                pushq %rcx
                movb (%_ip), %cl
            .endif
            testb $(32 - 1), %cl
            jz 1f
            shrd %cl, %\reg, %tmpd
            setf_zsp %tmpd, l
        1:
            .ifc \arg,imm
                popq %rcx
                gret 1
            .else
                gret
            .endif
    .endm
    .each_reg x
    .purgem x
    .gadget_array shrd_\arg
.endr

.macro do_bt arg
    movl \arg, %r14d
    andl $~(-1<<5), %r14d
    btl %r14d, %_tmp
    setf_c
.endm
.gadget bt32_imm
    do_bt (%_ip)
    gret 1
.macro x name reg
    .gadget bt32_\name
        do_bt %\reg
        gret
.endm
.each_reg x
.purgem x
.gadget_array bt
