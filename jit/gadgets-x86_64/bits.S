#include "gadgets.h"

.macro do_shift type, size, s, ss
    .irp arg, reg_c,imm
        .gadget \type\size\()_\arg
            .ifc \arg,imm
                movl %ecx, %r14d
                movb (%_ip), %cl
            .endif
            testb $31, %cl
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

.macro do_shiftd op, arg
    .macro x name, reg
        .gadget \op\()_\arg\()32_\name
            .ifc \arg,imm
                .ifnc \name,reg_c
                    pushq %rcx
                .else
                    pushq %rdx
                    xchg %ecx, %edx
                .endif
                movb (%_ip), %cl
            .endif
            testb $(32 - 1), %cl
            jz 1f
            .ifc \name,reg_c
                \op %cl, %edx, %tmpd
            .else
                \op %cl, %\reg, %tmpd
            .endif
            setf_oc
            setf_zsp %tmpd, l
        1:
            .ifc \arg,imm
                .ifnc \name,reg_c
                    popq %rcx
                .else
                    xchg %ecx, %edx
                    popq %rdx
                .endif
                gret 1
            .else
                gret
            .endif
    .endm
    .each_reg x
    .purgem x
    .gadget_array \op\()_\arg
.endm
.irp op, shrd,shld
    .irp arg, imm,cl
        do_shiftd \op, \arg
    .endr
.endr

.macro do_bt arg
    andl $(32-1), %tmpd
    btl %tmpd, \arg
    setf_c
.endm

.gadget bt32_mem
    movl %tmpd, %r14d
    andl $~(32-1), %r14d
    shrl $3, %r14d
    addl %r14d, %_addr
    read_prep 32, bt32_mem
    do_bt (%_addrq)
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
