#include "gadgets.h"
#include "math.h"

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
        .ifnc \op,cmps N .ifnc \op,scas
            .exitm
        .endif N .endif
    .endif

    .gadget \op\size\()_\rep
        .ifnc \rep,once
            cbz ecx, 2f
        .endif
        # df_offset = w12
        mov w12, (\size/8)
        ldr w8, [_cpu, CPU_eflags]
        tst w8, DF_FLAG
        cneg w12, w12, ne
    1:

        .ifc \op,lods
            mov _addr, esi
            read_prep \size, \op\size\()_\rep
            ldrs eax, [_xaddr], \s

        .else N .ifc \op,stos
            mov _addr, edi
            write_prep \size, \op\size\()_\rep
            str\s eax, [_xaddr]
            write_done \size, \op\size\()_\rep

        .else N .ifc \op,movs
            mov _addr, esi
            read_prep \size, \op\size\()_\rep
            ldr\s _tmp, [_xaddr]
            mov _addr, edi
            write_prep \size, \op\size\()_\rep\()2
            str\s _tmp, [_xaddr]
            write_done \size, \op\size\()_\rep\()2

        .else N .ifc \op, scas
            mov _addr, edi
            read_prep \size, \op\size\()_\rep
            ldr\s w8, [_xaddr]
            mov _tmp, eax
            setf_a src=w8, dst=_tmp
            do_add sub, _tmp, w8, \s
            setf_zsp \s

        .else N .ifc \op, cmps
            mov _addr, esi
            read_prep \size, \op\size\()_\rep
            ldr\s _tmp, [_xaddr]
            mov _addr, edi
            read_prep \size, \op\size\()_\rep\()2
            ldr\s w8, [_xaddr]
            setf_a src=w8, dst=_tmp
            do_add sub, _tmp, w8, \s
            setf_zsp \s
        .endif N .endif N .endif N .endif N .endif

        .ifin(\op, lods,movs,cmps)
            add esi, esi, w12
        .endifin
        .ifin(\op, movs,stos,cmps,scas)
            add edi, edi, w12
        .endifin

        .ifnc \rep,once
            subs ecx, ecx, 1
            .ifin(\op, scas,cmps)
                .ifc \rep,rep
                    cbnz _tmp, 2f
                .else N .ifc \rep,repnz
                    cbz _tmp, 2f
                .endif N .endif
            .endifin
            cbnz ecx, 1b
    2:
        .endif
        gret 1
        .ifin(\op, lods,movs,scas,cmps)
            read_bullshit \size, \op\size\()_\rep
        .endifin
        .ifc \op,stos
            write_bullshit \size, \op\size\()_\rep
        .endif
        .ifc \op,movs
            write_bullshit \size, \op\size\()_\rep\()2
        .endif
        .ifc \op,cmps
            read_bullshit \size, \op\size\()_\rep\()2
        .endif
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
