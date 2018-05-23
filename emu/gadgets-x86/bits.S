#include "gadgets.h"

.macro do_shift type, size
    .irp arg, reg_c,imm
        .gadget \type\size\()_\arg
            .ifc \arg,reg_c
                \type\()l %cl, %_tmp
            .else; .ifc \arg,imm
                pushq %rcx
                movb (%_ip), %cl
                \type\()l %cl, %_tmp
                popq %rcx
            .endif; .endif
            setf_oc
            setf_zsp %_tmp, l
            clearf_a
            .ifc \arg,imm
                gret 1
            .else
                gret
            .endif
    .endr
.endm

.irp type, shl,shr
    .irp size, 8,16,32
        do_shift \type, \size
    .endr
    .gadget_array \type
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
