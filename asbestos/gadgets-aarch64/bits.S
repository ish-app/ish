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
                .ifnb \s
                    sxt\s _tmp, _tmp
                .endif
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

            # aaaaaaaaaaaaaa
            .ifin(\type, rcl,rcr)
                .ifc \type,rcr
                    ubfx w9, _tmp, \size-1, 1
                    ldrb w10, [_cpu, CPU_cf]
                    eor w9, w9, w10
                    strb w9, [_cpu, CPU_of]
                .endif

                ldrb w9, [_cpu, CPU_cf]
                lsl x9, x9, \size
                orr _xtmp, _xtmp, x9
                # so ok we mask the shift count, not too hard
                and w8, w8, 31
                # ...now mod by \size+1 oof
                .if \size == 8
                    .irpc _, 123
                        subs w10, w8, \size+1
                        csel w8, w10, w8, gt
                    .endr
                .elseif \size == 16
                    subs w10, w8, \size+1
                    csel w8, w10, w8, gt
                .endif
                mov w9, \size+1
                sub w9, w9, w8
                .ifc \type,rcl
                    lsl x8, _xtmp, x8
                    lsr x9, _xtmp, x9
                .else
                    lsr x8, _xtmp, x8
                    lsl x9, _xtmp, x9
                .endif
                orr _xtmp, x8, x9
                ubfx x9, _xtmp, \size, 1
                strb w9, [_cpu, CPU_cf]
                .ifc \type,rcl
                    ubfx w10, _tmp, \size-1, 1
                    eor w10, w10, w9
                    strb w10, [_cpu, CPU_of]
                .endif
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

.irp type, shl,shr,sar,rol,ror,rcl,rcr
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

.macro do_bt_op op, arg, size, s
    .ifc \op,bt
        .ifnc \arg, w8
            mov w8, \arg
        .endif
        and _tmp, _tmp, \size-1
        lsr w8, w8, _tmp
        and w8, w8, 1
        strb w8, [_cpu, CPU_cf]
    .else
        mov w9, 1
        and _tmp, _tmp, \size-1
        lsl w9, w9, _tmp
        tst \arg, w9
        .ifc \op,btc
            eor \arg, \arg, w9
        .else N .ifc \op,bts
            orr \arg, \arg, w9
        .else N .ifc \op,btr
            bic \arg, \arg, w9
        .endif N .endif N .endif
        cset w9, ne
        strb w9, [_cpu, CPU_cf]
    .endif
.endm

.macro do_bt op, size, s
    .gadget \op\size\()_mem
        bic w8, _tmp, 0x1f
        add _addr, _addr, w8, lsr 3
        # hell {{{
        .ifin(\op, bt)
            read_prep \size, \op\size\()_mem
        .endifin
        .ifin(\op, btc,bts,btr)
            write_prep \size, \op\size\()_mem
        .endifin
        # }}}
        ldr w8, [_xaddr]
        do_bt_op \op, w8, \size, \s
        .ifin(\op, btc,bts,btr)
            str w8, [_xaddr]
            write_done \size, \op\size\()_mem
        .endifin
        gret 1
        # also hell {{{
        .ifin(\op, bt)
            read_bullshit \size, \op\size\()_mem
        .endifin
        .ifin(\op, btc,bts,btr)
            write_bullshit \size, \op\size\()_mem
        .endifin
        # }}}

    .macro x name, reg
        .gadget \op\size\()_\name
            do_bt_op \op, \reg, \size, \s
            gret
    .endm
    .each_reg x
    .purgem x
.endm

.irp op, bt,btc,bts,btr
    .irp size, 16,32
        ss \size, do_bt, \op
    .endr
    .gadget_array \op
.endr

# atomic versions of the above

.macro do_bt_atomic op, size, s
    .gadget atomic_\op\size\()_mem
        bic w8, _tmp, 0x1f
        add _addr, _addr, w8, lsr 3
        write_prep \size, atomic_\op\size\()_mem
        # this is simple enough that I'm comfortable doing it with ldaxr/stlxr
    1:
        ldaxr w8, [_xaddr]
        mov w9, 1
        and _tmp, _tmp, \size-1
        lsl w9, w9, _tmp
        tst w8, w9
        .ifc \op,btc
            eor w8, w8, w9
        .else N .ifc \op,bts
            orr w8, w8, w9
        .else N .ifc \op,btr
            bic w8, w8, w9
        .endif N .endif N .endif
        cset w9, ne
        stlxr w10, w8, [_xaddr]
        cbnz w10, 1b
        strb w9, [_cpu, CPU_cf]
        write_done \size, atomic_\op\size\()_mem
        gret 1
        write_bullshit \size, atomic_\op\size\()_mem
.endm

.irp op, btc,bts,btr
    .irp size, 16,32
        ss \size, do_bt_atomic, \op
    .endr
    .gadget_array atomic_\op
.endr

.macro x name reg
    .gadget bswap_\name
        rev \reg, \reg
        gret
.endm
.each_reg x
.purgem x
.gadget_list bswap, REG_LIST
