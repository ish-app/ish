#include "gadgets.h"

.gadget rdtsc
    rdtsc
    gret

.gadget cpuid
    # regrettable
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    subq $0x10, %rsp
    movl %eax, 0xc(%rsp)
    movl %ebx, 0x8(%rsp)
    movl %ecx, 0x4(%rsp)
    movl %edx, 0x0(%rsp)
    leaq 0xc(%rsp), %rdi
    leaq 0x8(%rsp), %rsi
    leaq 0x4(%rsp), %rdx
    leaq 0x0(%rsp), %rcx
    call helper_cpuid
    movl 0xc(%rsp), %eax
    movl 0x8(%rsp), %ebx
    movl 0x4(%rsp), %ecx
    movl 0x0(%rsp), %edx
    addq $0x10, %rsp
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    gret

.gadget cmpxchg32_mem
    write_prep 32
    cmpxchg %tmpd, (%_addrq)
    write_done 32
    setf_oc
    # god help us
    setp %r10b
    seta %r13b
    setz %r14b
    sets %r15b
    shlb $2, %r10b
    shlb $4, %r13b
    shlb $6, %r14b
    shlb $7, %r15b
    orb %r10b, %r15b
    orb %r13b, %r15b
    orb %r14b, %r15b
    andl $~(PF_RES|ZF_RES|SF_RES|AF_OPS), CPU_flags_res(%_cpu)
    movb %r15b, CPU_eflags(%_cpu)
    gret 1
.gadget_array cmpxchg

.macro do_helper type, size=
    .gadget helper_\type\size
        .ifin(\type, read,write)
            \type\()_prep (\size*4)
        .endifin
        save_c
        movq %_cpu, %rdi
        .ifc \type,1
            movq 8(%_ip), %rsi
        .endif
        .ifc \type,2
            movq 8(%_ip), %rsi
            movq 16(%_ip), %rdx
        .endif
        .ifin(\type, read,write)
            movq %_addrq, %rsi
        .endifin
        callq *(%_ip)
        restore_c
        .ifc \type,write
            write_done (\size*4)
        .endif
        .ifc \type,0
            gret 1
        .else; .ifc \type,2
            gret 3
        .else
            gret 2
        .endif; .endif
.endm
do_helper 0
do_helper 1
do_helper 2
.irp size, SIZE_LIST,64,80
    do_helper read, \size
    do_helper write, \size
.endr

.gadget fstsw_ax
    movw CPU_fsw(%_cpu), %ax
    gret
