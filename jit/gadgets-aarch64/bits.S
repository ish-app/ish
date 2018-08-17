#include "gadgets.h"

.macro do_shift type, size, s
    .irp arg, reg_c,imm
        .gadget \type\size\()_\arg
            .ifc \arg,imm
                ldr w8, [_ip]
                ands w8, w8, \size-1
            .else
                ands w8, _ecx, \size-1
            .endif
            b.eq 1f

            # shift by one less, then by one more
            # that way we can retrieve the last bit shifted out for calculating CF and OF
            .ifc \type,shl
                sub w8, w8, 1
                lsl _tmp, _tmp, w8
                ubfx w9, _tmp, \size-1, 1
                ubfx w10, _tmp, \size-2, 1
                lsl _tmp, _tmp, 1
                eor w10, w10, w9
                strb w9, [_cpu, CPU_cf]
                strb w10, [_cpu, CPU_of]
            .else
                hlt 0
            .endif

            .ifin(\type, shl,shr,sar)
                setf_zsp \s
                clearf_a
            .endifin
        1:
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
            hlt 0
            /*
            .ifc \arg,imm
                pushq %rcx
                movb (%_ip), %cl
            .endif
            testb $(32 - 1), %cl
            jz 1f
            \op %cl, %\reg, %tmpd
            setf_oc
            setf_zsp %tmpd, l
        1:
            .ifc \arg,imm
                popq %rcx
                gret 1
            .else
                gret
            .endif
            */
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

.macro do_bt_w8
    lsr w8, w8, _tmp
    and w8, w8, 1
    strb w8, [_cpu, CPU_cf]
.endm

.gadget bt32_mem
    add _addr, _addr, _tmp, lsr 3
    read_prep 32
    ldr w8, [_xaddr]
    do_bt_w8
    gret 1

.macro x name reg
    .gadget bt32_\name
        mov w8, \reg
        do_bt_w8
        gret
.endm
.each_reg x
.purgem x

.gadget_array bt

.macro x name reg
    .gadget bswap_\name
        rev \reg, \reg
        gret
.endm
.each_reg x
.purgem x
.gadget_list bswap, REG_LIST
