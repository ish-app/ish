#include "gadgets.h"

#define REP_LIST once,rep,repnz

.gadget cld
    andl $~DF_FLAG, CPU_eflags(%_cpu)
    gret

.gadget std
    orl $DF_FLAG, CPU_eflags(%_cpu)
    gret

.macro do_strop op, size, rep, s, ss, a
    # repnz is only a thing for cmps and scas
    .ifc \rep,repnz
        .ifnc \op,cmps; .ifnc \op,scas
            .exitm
        .endif; .endif
    .endif

    .gadget \op\size\()_\rep
        .ifnc \rep,once
            testl %ecx, %ecx
            jz 2f
    1:
        .endif
        movl $-(\size/8), CPU_df_offset(%_cpu)
        testl $DF_FLAG, CPU_eflags(%_cpu)
        jnz 3f
        negl CPU_df_offset(%_cpu)
    3:

        .ifc \op,lods
            movl %esi, %_addr
            read_prep \size, \op\size\()_\rep
            mov\ss (%_addrq), %\a

        .else; .ifc \op,stos
            movl %edi, %_addr
            write_prep \size, \op\size\()_\rep
            mov\ss %\a, (%_addrq)
            write_done \size, \op\size\()_\rep

        .else; .ifc \op,movs
            movl %esi, %_addr
            read_prep \size, \op\size\()_\rep
            mov\ss (%_addrq), %tmp\s
            movl %edi, %_addr
            write_prep \size, \op\size\()_\rep\()2
            mov\ss %tmp\s, (%_addrq)
            write_done \size, \op\size\()_\rep

        .else; .ifc \op, scas
            mov %edi, %_addr
            read_prep \size, \op\size\()_\rep
            mov\ss (%_addrq), %tmp\s
            setf_a src=%\a, dst=%tmp\s, ss=\ss
            sub\ss %\a, %tmp\s
            setf_oc
            setf_zsp %tmp\s, \ss

        .else; .ifc \op, cmps
            movl %esi, %_addr
            read_prep \size, \op\size\()_\rep
            mov\ss (%_addrq), %tmp\s
            mov %edi, %_addr
            read_prep \size, \op\size\()_\rep\()2
            mov\ss (%_addrq), %r14\s
            setf_a src=%r14\s, dst=%tmp\s, ss=\ss
            sub\ss %r14\s, %tmp\s
            setf_oc
            setf_zsp %tmp\s, \ss
        .endif; .endif; .endif; .endif; .endif

        .ifin(\op, lods,movs,cmps)
            addl CPU_df_offset(%_cpu), %esi
        .endifin
        .ifin(\op, movs,stos,cmps,scas)
            addl CPU_df_offset(%_cpu), %edi
        .endifin

        .ifnc \rep,once
            decl %ecx
            .ifin(\op, scas,cmps)
                .ifc \rep,rep
                    test\ss %tmp\s, %tmp\s
                    jnz 2f
                .else; .ifc \rep,repnz
                    test\ss %tmp\s, %tmp\s
                    jz 2f
                .endif; .endif
            .endifin
            testl %ecx, %ecx
            jnz 1b
    2:
        .endif
        gret 1
.endm

.irp op, lods,stos,movs,scas,cmps
    .irp size, 8,16,32
        .irp rep, REP_LIST
            .if \size == 8
                do_strop \op, \size, \rep, b, b, al
            .elseif \size == 16
                do_strop \op, \size, \rep, w, w, ax
            .elseif \size == 32
                do_strop \op, \size, \rep, d, l, eax
            .endif
        .endr
    .endr
    .gadget_list_size \op, REP_LIST
.endr
# temporary
