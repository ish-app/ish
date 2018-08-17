#include "emu/interrupt.h"
#include "gadgets.h"

.global jit_enter
.type jit_enter,function
jit_enter:
    stp x18, x19, [sp, -0x60]!
    stp x20, x21, [sp, 0x10]
    stp x22, x23, [sp, 0x20]
    stp x24, x25, [sp, 0x30]
    stp x26, x27, [sp, 0x40]
    stp x28, x29, [sp, 0x50]
    add _ip, x0, JIT_BLOCK_code
    # cpu is already x1
    add _tlb, x2, TLB_entries
    ldr _eax, [_cpu, CPU_eax]
    ldr _ebx, [_cpu, CPU_ebx]
    ldr _ecx, [_cpu, CPU_ecx]
    ldr _edx, [_cpu, CPU_edx]
    ldr _esi, [_cpu, CPU_esi]
    ldr _ebp, [_cpu, CPU_ebp]
    ldr _esp, [_cpu, CPU_esp]
    gret

.global jit_ret_chain
jit_ret_chain:
    cmp _ip, 0
    b.lt jit_ret
    gret

.global jit_ret
jit_ret:
    # load -1
    mvn _tmp, wzr
    # fallthrough

.global jit_exit
jit_exit:
    str _eax, [_cpu, CPU_eax]
    str _ebx, [_cpu, CPU_ebx]
    str _ecx, [_cpu, CPU_ecx]
    str _edx, [_cpu, CPU_edx]
    str _esi, [_cpu, CPU_esi]
    str _ebp, [_cpu, CPU_ebp]
    str _esp, [_cpu, CPU_esp]
    str _eip, [_cpu, CPU_eip]
    ldp x28, x29, [sp, 0x50]
    ldp x26, x27, [sp, 0x40]
    ldp x24, x25, [sp, 0x30]
    ldp x22, x23, [sp, 0x20]
    ldp x20, x21, [sp, 0x10]
    ldp x18, x19, [sp], 0x60
    # _tmp is already x0
    ret

.gadget interrupt
    ldr _tmp, [_ip]
    ldr _eip, [_ip, 8]
    b jit_exit

.gadget exit
    ldr _eip, [_ip]
    b jit_ret
