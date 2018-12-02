#include "gadgets.h"

.macro do_shift type, size, s
    .irp arg, reg_c,imm
        .gadget \type\size\()_\arg
            .ifc \arg,imm
                ldr w8, [_ip]
                ands w8, w8, 31
            .else
                ands w8, ecx, 31
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
            .endif
            .ifc \type,shr
                ubfx w10, _tmp, \size-1, 1
                sub w8, w8, 1
                lsr _tmp, _tmp, w8
                and w9, _tmp, 1
                lsr _tmp, _tmp, 1
                strb w9, [_cpu, CPU_cf]
                strb w10, [_cpu, CPU_of]
            .endif
            .ifc \type,sar
                # lazy ass copy paste job
                sub w8, w8, 1
                asr _tmp, _tmp, w8
                and w9, _tmp, 1
                asr _tmp, _tmp, 1
                strb w9, [_cpu, CPU_cf]
                strb wzr, [_cpu, CPU_of]
            .endif

            # regrets
            .ifin(\type, rol,ror)
                .ifb \s
                    .ifc \type,rol
                        neg w8, w8
                    .endif
                    ror _tmp, _tmp, w8
                .else
                    # kill me
                    uxt\s _tmp, _tmp
                    neg w9, w8
                    and w8, w8, \size-1
                    and w9, w9, \size-1
                    .ifc \type,rol
                        lsl w8, _tmp, w8
                        lsr w9, _tmp, w9
                    .else
                        lsr w8, _tmp, w8
                        lsl w9, _tmp, w9
                    .endif
                    orr _tmp, w8, w9
                .endif
                .ifc \type,rol
                    ubfx w9, _tmp, 0, 1
                    ubfx w10, _tmp, \size-1, 1
                .else
                    ubfx w9, _tmp, \size-1, 1
                    ubfx w10, _tmp, \size-2, 1
                .endif
                eor w10, w10, w9
                strb w9, [_cpu, CPU_cf]
                strb w10, [_cpu, CPU_of]
            .endifin

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
            .ifc \arg,imm
                ldrb w8, [_ip]
            .else
                uxtb w8, ecx
            .endif
            tst w8, 31
            b.eq 1f
            mov w9, 32
            sub w9, w9, w8
            sub w8, w8, 1 /* shift by one less */
            .ifc \op,shrd
                lsr w8, _tmp, w8
                # and by one more
                and w10, w8, 1
                lsr w8, w8, 1
                lsl w9, \reg, w9
            .else
                lsl w8, _tmp, w8
                # and by one more
                ubfx w10, w8, 31, 1
                lsl w8, w8, 1
                lsr w9, \reg, w9
            .endif
            orr _tmp, w8, w9
            strb w10, [_cpu, CPU_cf]
            setf_zsp
        1:
            .ifc \arg,imm
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

.macro do_bt_w8
    and _tmp, _tmp, 0x1f
    lsr w8, w8, _tmp
    and w8, w8, 1
    strb w8, [_cpu, CPU_cf]
.endm

.gadget bt32_mem
    bic w8, _tmp, 0x1f
    add _addr, _addr, w8, lsr 3
    read_prep 32, bt32_mem
    ldr w8, [_xaddr]
    do_bt_w8
    gret 1
    read_bullshit 32, bt32_mem

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
