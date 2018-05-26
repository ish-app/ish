#include "gadgets.h"

.gadget call
    subl $4, %_esp
    movl %_esp, %_addr
    write_prep
    movl 16(%_ip), %r14d
    movl %r14d, (%_addrq)
    movl 8(%_ip), %_eip
    jmp jit_ret

.gadget call_indir
    subl $4, %_esp
    movl %_esp, %_addr
    write_prep
    movl 8(%_ip), %r14d
    movl %r14d, (%_addrq)
    movl %_tmp, %_eip
    jmp jit_ret

.gadget ret
    movl %_esp, %_addr
    addl 8(%_ip), %_esp
    read_prep
    movl (%_addrq), %_eip
    jmp jit_ret

.gadget jmp_indir
    movl %_tmp, %_eip
    jmp jit_ret
.gadget jmp
    movl (%_ip), %_eip
    jmp jit_ret
.gadget jcxz
    cmpl $0, %ecx
    jne 1f
    movl (%_ip), %_eip
    jmp jit_ret
1:
    movl 8(%_ip), %_eip
    jmp jit_ret

#define COND_LIST o,c,z,cz,s,p,sxo,sxoz
#define IMPLEMENTED_CONDS c,z,cz,s,p,sxo,sxoz

.macro check_res
    cmpl $0, CPU_res(%_cpu)
.endm
.macro check_cf
    cmpb $0, CPU_cf(%_cpu)
.endm

.macro res_or_flag type, resflag, flag, target
    testl $\resflag, CPU_flags_res(%_cpu)
    jz 2f
    check_res
    j\type \target
    2:
    testl $\flag, CPU_eflags(%_cpu)
    jnz \target
.endm

.macro do_jump cond, target
    # please tell me if you know a better way
    .ifc \cond,o
    .else; .ifc \cond,c
        check_cf
        jnz \target
    .else; .ifc \cond,z
        res_or_flag z, ZF_RES, ZF_FLAG, \target
    .else; .ifc \cond,cz
        check_cf
        jnz \target
        check_res
        jz \target
    .else; .ifc \cond,s
        check_res
        js \target
    .else; .ifc \cond,p
        check_res
        jp \target
    .else; .ifc \cond,sxo
        check_res
        sets %r14b
        xorb CPU_of(%_cpu), %r14b
        jnz \target
    .else; .ifc \cond,sxoz
        check_res
        jz \target
        sets %r14b
        xorb CPU_of(%_cpu), %r14b
        jnz \target
    .endif; .endif; .endif; .endif; .endif; .endif; .endif; .endif
.endm

.irp cond, IMPLEMENTED_CONDS
    .gadget jmp_\cond
        do_jump \cond, 1f
        movl 8(%_ip), %_eip
        jmp jit_ret
    1:
        movl (%_ip), %_eip
        jmp jit_ret

    .gadget set_\cond
        do_jump \cond, 1f
        movl $0, %_tmp
        gret
    1:
        movl $1, %_tmp
        gret
    .gadget setn_\cond
        do_jump \cond, 1f
        movl $1, %_tmp
        gret
    1:
        movl $0, %_tmp
        gret

    .gadget skip_\cond
        do_jump \cond, 1f
        gret 1
    1:
        addq (%_ip), %_ip
        gret 1
    .gadget skipn_\cond
        do_jump \cond, 1f
        addq (%_ip), %_ip
    1:
        gret 1
.endr
.gadget_list jmp, COND_LIST
.gadget_list set, COND_LIST
.gadget_list setn, COND_LIST
.gadget_list skip, COND_LIST
.gadget_list skipn, COND_LIST
