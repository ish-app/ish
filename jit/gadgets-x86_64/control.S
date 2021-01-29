#include "gadgets.h"

.gadget call
    // save return address
    leal -4(%_esp), %_addr
    write_prep 32, call
    movl 16(%_ip), %r14d
    movl %r14d, (%_addrq)
    // push stack pointer
    subl $4, %_esp
    // save ip-to-arguments to return cache
    shrw $4, %r14w
    movzwl %r14w, %r14d
    movq %_ip, LOCAL_ret_cache(%_cpu, %r14, 8)
    write_done 32, call // clobbers r14
    // jump to target
    movq 32(%_ip), %_ip
    jmp jit_ret_chain

.gadget call_indir
    // save return address
    leal -4(%_esp), %_addr
    write_prep 32, call_indir
    movl 16(%_ip), %r14d
    movl %r14d, (%_addrq)
    // push stack pointer
    subl $4, %_esp
    // save ip-to-arguments to return cache
    shrw $4, %r14w
    movzwl %r14w, %r14d
    movq %_ip, LOCAL_ret_cache(%_cpu, %r14, 8)
    write_done 32, call_indir // clobbers r14
    // jump to target
    movl %_tmp, %_eip
    jmp jit_ret

.gadget ret
    movl %_esp, %_addr
    // load return address and save to _tmp
    read_prep 32, ret
    movl (%_addrq), %tmpd
    movl %tmpd, %r14d
    // pop stack pointer
    addl 8(%_ip), %_esp
    // load saved ip in return cache
    shrw $4, %r14w
    movzwq %r14w, %r14
    movq LOCAL_ret_cache(%_cpu, %r14, 8), %_ip
    // found?
    cmpq $0, %_ip
    jz 2f
    // check if we jumped to the correct CALL instruction
    movl 16(%_ip), %r14d
    movq 8(%_ip), %r15
    cmpl %r14d, %tmpd
    jnz 1f
    // good, now do return chaining, the logic is similar to `jit_ret_chain`
    movq 24(%_ip), %_ip
    btq $63, %_ip
    jc 1f
    leaq -JIT_BLOCK_code(%_ip), %r15
    movq %r15, LOCAL_last_block(%_cpu)
    gret
1:
    movq %r15, LOCAL_last_block(%_cpu)
    // fallthrough
2:
    movl %tmpd, %_eip
    jmp jit_ret

.gadget jmp_indir
    movl %_tmp, %_eip
    jmp jit_ret
.gadget jmp
    movq (%_ip), %_ip
    jmp jit_ret_chain
.gadget jcxz
    cmpl $0, %ecx
    jne 1f
    movq (%_ip), %_ip
    jmp jit_ret_chain
1:
    movq 8(%_ip), %_ip
    jmp jit_ret_chain

#define COND_LIST o,c,z,cz,s,p,sxo,sxoz

.macro check_res
    cmpl DOLLAR(0), CPU_res(%_cpu)
.endm
.macro check_cf
    cmpb DOLLAR(0), CPU_cf(%_cpu)
.endm

.macro res_or_flag type, resflag, flag, target
    testl $\resflag, CPU_flags_res(%_cpu)
    jz 2f
    check_res
    j\type \target
    jmp 3f
    2:
    testl $\flag, CPU_eflags(%_cpu)
    jnz \target
    3:
.endm

.macro do_jump cond, target
    # please tell me if you know a better way
    .ifc \cond,o
        cmpb $0, CPU_of(%_cpu)
        jnz \target
    .else; .ifc \cond,c
        check_cf
        jnz \target
    .else; .ifc \cond,z
        res_or_flag z, ZF_RES, ZF_FLAG, \target
    .else; .ifc \cond,cz
        check_cf
        jnz \target
        res_or_flag z, ZF_RES, ZF_FLAG, \target
    .else; .ifc \cond,s
        res_or_flag s, SF_RES, SF_FLAG, \target
    .else; .ifc \cond,p
        res_or_flag p, PF_RES, PF_FLAG, \target
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

.irp cond, COND_LIST
    .gadget jmp_\cond
        do_jump \cond, 1f
        movq 8(%_ip), %_ip
        jmp jit_ret_chain
    1:
        movq (%_ip), %_ip
        jmp jit_ret_chain

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

.gadget pushf
    save_c
    movq %_cpu, %rdi
    call NAME(helper_collapse_flags)
    restore_c

    sub $4, %_esp
    movl %_esp, %_addr
    write_prep 32, pushf
    movl CPU_eflags(%_cpu), %tmpd
    movl %tmpd, (%_addrq)
    write_done 32, pushf
    gret

.gadget popf
    movl %_esp, %_addr
    read_prep 32, popf
    movl (%_addrq), %tmpd
    movl %tmpd, CPU_eflags(%_cpu)
    add $4, %_esp

    save_c
    movq %_cpu, %rdi
    call NAME(helper_expand_flags)
    restore_c
    gret

.gadget sahf
    xchgb %ah, %al
    movb %al, CPU_eflags(%_cpu)
    xchgb %al, %ah
    save_c
    movq %_cpu, %rdi
    call NAME(helper_expand_flags)
    restore_c
    gret
