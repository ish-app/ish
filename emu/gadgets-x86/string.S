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
        .ifnc \rep,cmps; .ifnc \rep,scas
            .exitm
        .endif; .endif
    .endif

    .gadget \op\size\()_\rep
        .ifc \rep,rep
            testl %ecx, %ecx
            jz 2f
    1:
        .endif
        movl $-(\size/8), CPU_df_offset(%_cpu)
        testl $DF_FLAG, CPU_eflags(%_cpu)
        jnz 3f
        negl CPU_df_offset(%_cpu)
    3:
        .ifc \op,stos
            movl %edi, %_addr
            write_prep
            mov\ss %\a, (%_addrq)
        .else; .ifc \op,movs
            movl %esi, %_addr
            read_prep
            mov\ss (%_addrq), %tmp\s
            movl %edi, %_addr
            write_prep
            mov\ss %tmp\s, (%_addrq)
        .endif; .endif
        .ifin(\op, lods,movs,cmps,scas)
            addl CPU_df_offset(%_cpu), %esi
        .endifin
        .ifin(\op, movs,stos,cmps)
            addl CPU_df_offset(%_cpu), %edi
        .endifin
        .ifc \rep,rep
            decl %ecx
            jnz 1b
    2:
        .endif
        gret 1
.endm

.irp op, movs,stos
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
.gadget_list_size lods, REP_LIST
.gadget_list_size scas, REP_LIST
.gadget_list_size cmps, REP_LIST
