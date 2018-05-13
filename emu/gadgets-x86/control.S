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

#define COND_LIST o,c,z,cz,s,p,sxo,sxoz
.irp cond, z
    .gadget jmp_\cond
        # please tell me if you know a better way
        .ifc \cond,o
        .else; .ifc \cond,c
        .else; .ifc \cond,z
            testl $-1, CPU_res(%_cpu)
            jz 1f
        .else; .ifc \cond,cz
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

.section .rodata
.gadget_array_list jmp, COND_LIST
