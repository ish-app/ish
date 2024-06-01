#include "emu/interrupt.h"
#include "gadgets.h"

.global NAME(fiber_enter)
.type_compat fiber_enter,function
NAME(fiber_enter):
    stp x18, x19, [sp, -0x70]!
    stp x20, x21, [sp, 0x10]
    stp x22, x23, [sp, 0x20]
    stp x24, x25, [sp, 0x30]
    stp x26, x27, [sp, 0x40]
    stp x28, x29, [sp, 0x50]
    str lr, [sp, 0x60]
    add _ip, x0, FIBER_BLOCK_code
    # cpu is already x1
    add _tlb, x2, TLB_entries
    load_regs
    gret

.global fiber_ret_chain
fiber_ret_chain:
    cmp _ip, 0
    b.lt fiber_ret
    ldr x8, [_cpu, CPU_poked_ptr]
    ldrb w8, [x8]
    cmp w8, 0
    b.ne poke
    sub x8, _ip, FIBER_BLOCK_code
    str x8, [_cpu, LOCAL_last_block]
    gret

poke:
    ldr eip, [_ip, -FIBER_BLOCK_code+FIBER_BLOCK_addr]
    # fallthrough

.global fiber_ret
fiber_ret:
    # load -1
    mvn _tmp, wzr
    # fallthrough

.global fiber_exit
fiber_exit:
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
    ldr w8, [_ip, 16]
    str w8, [_cpu, CPU_segfault_addr]
    ldr eip, [_ip, 8]
    strb wzr, [_cpu, CPU_segfault_was_write]
    b fiber_exit

.gadget exit
    ldr eip, [_ip]
    b fiber_ret
