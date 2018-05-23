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


.gadget bt32_imm
    movl (%_ip), %r14d
    andl $~(-1<<5), %r14d
    btl %r14d, %_tmp
    setf_c
    gret 1
.gadget_array bt
