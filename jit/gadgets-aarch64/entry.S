#include "emu/interrupt.h"
#include "gadgets.h"

.global NAME(jit_enter)
.type_compat jit_enter,function
NAME(jit_enter):
    stp x18, x19, [sp, -0x70]!
    stp x20, x21, [sp, 0x10]
    stp x22, x23, [sp, 0x20]
    stp x24, x25, [sp, 0x30]
    stp x26, x27, [sp, 0x40]
    stp x28, x29, [sp, 0x50]
    str lr, [sp, 0x60]
    add _ip, x0, JIT_BLOCK_code
    # cpu is already x1
    add _tlb, x2, TLB_entries
    load_regs
    gret

.global jit_ret_chain
jit_ret_chain:
    cmp _ip, 0
    b.lt jit_ret
    sub x8, _ip, JIT_BLOCK_code
    str x8, [_cpu, LOCAL_last_block]
    gret

.global jit_ret
jit_ret:
    # load -1
    mvn _tmp, wzr
    # fallthrough

.global jit_exit
jit_exit:
    save_regs
    ldr lr, [sp, 0x60]
    ldp x28, x29, [sp, 0x50]
    ldp x26, x27, [sp, 0x40]
    ldp x24, x25, [sp, 0x30]
    ldp x22, x23, [sp, 0x20]
    ldp x20, x21, [sp, 0x10]
    ldp x18, x19, [sp], 0x70
    # _tmp is already x0
    ret

.gadget interrupt
    ldr _tmp, [_ip]
    ldr eip, [_ip, 8]
    b jit_exit

.gadget exit
    ldr eip, [_ip]
    b jit_ret
