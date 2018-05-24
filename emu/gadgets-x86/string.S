#include "gadgets.h"

#define REP_LIST once,rep,repnz

.gadget cld
    andl $~DF_FLAG, CPU_eflags(%_cpu)
    gret

.macro do_strop op, size, rep
    # repnz is only a thing for cmps and scas
    .ifc \rep,repnz
        .ifnc \rep,cmps; .ifnc \rep,scas
            .exitm
        .endif; .endif
    .endif

    .gadget \op\size\()_\rep
    1:
        .ifc \op,stos
            movl %edi, %_addr
            write_prep
            .if \size == 8
                movb %al, (%_addrq)
            .elseif \size == 16
                movw %ax, (%_addrq)
            .elseif \size == 32
                movl %eax, (%_addrq)
            .endif
            addl $(\size/8), %edi
        .endif
        .ifc \rep,rep
            decl %ecx
            jnz 1b
        .endif
        gret
.endm

.irp op, stos
    .irp size, 8,16,32
        .irp rep, REP_LIST
            do_strop \op, \size, \rep
        .endr
    .endr
    .gadget_list_size \op, REP_LIST
.endr
# temporary
.gadget_list_size movs, REP_LIST
.gadget_list_size lods, REP_LIST
.gadget_list_size scas, REP_LIST
.gadget_list_size cmps, REP_LIST
