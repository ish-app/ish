#include "emu/interrupt.h"
#include "gadgets.h"

.global NAME(jit_enter)
.type_compat NAME(jit_enter),function
NAME(jit_enter):
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    # make stack 16 byte aligned
    # this is preserved elsewhere by only doing even numbers of pushes
    sub $0x8, %rsp
    leaq JIT_BLOCK_code(%rdi), %_ip
    movq %rsi, %_cpu
    movq %rsp, LOCAL_bp(%_cpu)
    leaq TLB_entries(%rdx), %_tlb
    load_regs
    gret

.global jit_ret_chain
jit_ret_chain:
    btq $63, %_ip
    jc 1f
    leaq -JIT_BLOCK_code(%_ip), %r10
    mov %r10, LOCAL_last_block(%_cpu)
    gret
1:
.global jit_ret
jit_ret:
    movl $-1, %_tmp
    jmp jit_exit

.global jit_exit
jit_exit:
    save_regs
    movl %_eip, CPU_eip(%_cpu)
    movq LOCAL_bp(%_cpu), %rsp
    add $0x8, %rsp # keep stack 16 byte aligned
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    mov %_tmp, %eax
    ret

.gadget interrupt
    movl (%_ip), %_tmp
    movl 8(%_ip), %_eip
    jmp jit_exit

.gadget exit
    movl (%_ip), %_eip
    jmp jit_ret
