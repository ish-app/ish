#include "gadgets.h"

#define REP_LIST once,rep,repnz

.gadget cld
    ldr w8, [_cpu, CPU_eflags]
    bic w8, w8, DF_FLAG
    str w8, [_cpu, CPU_eflags]
    gret

.gadget std
    ldr w8, [_cpu, CPU_eflags]
    orr w8, w8, DF_FLAG
    str w8, [_cpu, CPU_eflags]
    gret

# FIXME non 32 bit
.macro do_strop op, size, rep, s=
    # repnz is only a thing for cmps and scas
    .ifc \rep,repnz
        .ifnc \op,cmps; .ifnc \op,scas
            .exitm
        .endif; .endif
    .endif

    .gadget \op\size\()_\rep
        .ifnc \rep,once
            cbz ecx, 2f
    1:
        .endif
        # df_offset = w12
        mov w12, (\size/8)
        ldr w8, [_cpu, CPU_eflags]
        tst w8, DF_FLAG
        cneg w12, w12, ne
    3:

        .ifc \op,lods
            mov _addr, esi
            read_prep \size
            ldr\s eax, [_xaddr]

        .else; .ifc \op,stos
            mov _addr, edi
            write_prep \size
            str\s eax, [_xaddr]
            write_done \size

        .else; .ifc \op,movs
            mov _addr, esi
            read_prep \size
            ldr\s w8, [_xaddr]
            mov _addr, edi
            write_prep \size
            str\s w8, [_xaddr]
            write_done \size

        .else; .ifc \op, scas
            mov _addr, edi
            read_prep \size
            ldr _tmp, [_xaddr]
            setf_a eax, _tmp
            subs _tmp, _tmp, eax
            setf_oc
            setf_zsp \s

        .else; .ifc \op, cmps
            mov _addr, esi
            read_prep \size
            ldr _tmp, [_xaddr]
            mov _addr, edi
            read_prep \size
            ldr w8, [_xaddr]
            setf_a w8, _tmp
            subs _tmp, _tmp, w8
            setf_oc
            setf_zsp \s
        .endif; .endif; .endif; .endif; .endif

        .ifin(\op, lods,movs,cmps)
            add esi, esi, w12
        .endifin
        .ifin(\op, movs,stos,cmps,scas)
            add edi, edi, w12
        .endifin

        .ifnc \rep,once
            sub ecx, ecx, #1
            .ifin(\op, scas,cmps)
                .ifc \rep,rep
                    cbnz _tmp, 2f
                .else; .ifc \rep,repnz
                    cbz _tmp, 2f
                .endif; .endif
            .endifin
            cbz ecx, 1b
    2:
        .endif
        gret 1
.endm

.irp op, lods,stos,movs,scas,cmps
    .irp size, 8,16,32
        .irp rep, REP_LIST
            .if \size == 8
                do_strop \op, \size, \rep, b
            .elseif \size == 16
                do_strop \op, \size, \rep, h
            .elseif \size == 32
                do_strop \op, \size, \rep
            .endif
        .endr
    .endr
    .gadget_list_size \op, REP_LIST
.endr
# temporary
