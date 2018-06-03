#include "gadgets.h"

.macro do_shift type, size, s, ss
    .irp arg, reg_c,imm
        .gadget \type\size\()_\arg
            .ifc \arg,imm
                movl %ecx, %r14d
                movb (%_ip), %cl
            .endif
            testb $(\size - 1), %cl
            jz 1f
            \type\()\ss %cl, %tmp\s
            setf_oc
            .ifin(\type, shl,shr,sar)
                setf_zsp %tmp\s, \ss
                clearf_a
            .endifin
        1:
            .ifc \arg,imm
                movl %r14d, %ecx
            .endif
            .ifc \arg,imm
                gret 1
            .else
                gret
            .endif
    .endr
.endm

.irp type, shl,shr,sar,rol,ror
    .irp size, 8,16,32
        ss \size, do_shift, \type
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
            setf_oc
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

.macro x name reg
    .gadget bswap_\name
        bswap %\reg
        gret
.endm
.each_reg x
.purgem x
.gadget_list bswap, REG_LIST
