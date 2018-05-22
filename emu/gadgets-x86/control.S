#include "gadgets.h"

.gadget call
    subl $4, %_esp
    movl %_esp, %_addr
    write_prep
    movl 8(%_ip), %_tmp
    movl %_tmp, (%_addrq)
    movl (%_ip), %_eip
    jmp jit_ret

.gadget ret
    movl %_esp, %_addr
    addl $4, %_esp
    read_prep
    movl (%_addrq), %_eip
    jmp jit_ret

.gadget jmp_indir
    movl %_tmp, %_eip
    jmp jit_ret
.gadget jmp
    movl (%_ip), %_eip
    jmp jit_ret

#define COND_LIST o,c,z,cz,s,p,sxo,sxoz
.irp cond, c,z,cz
    .gadget jmp_\cond
        # please tell me if you know a better way
        .ifc \cond,o
        .else; .ifc \cond,c
            cmpl $0, CPU_cf(%_cpu)
            jnz 1f
        .else; .ifc \cond,z
            cmpl $0, CPU_res(%_cpu)
            jz 1f
        .else; .ifc \cond,cz
            cmpl $0, CPU_res(%_cpu)
            je 1f
            cmpl $0, CPU_cf(%_cpu)
            jnz 1f
        .else; .ifc \cond,s
        .else; .ifc \cond,p
        .else; .ifc \cond,sxo
        .else; .ifc \cond,sxoz
        .endif; .endif; .endif; .endif; .endif; .endif; .endif; .endif
        movl 8(%_ip), %_eip
        jmp jit_ret
    1:
        movl (%_ip), %_eip
        jmp jit_ret
.endr

.gadget_list jmp, COND_LIST
