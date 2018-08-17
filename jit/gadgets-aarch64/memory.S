#include "gadgets.h"
#include "emu/interrupt.h"

.gadget push
    sub esp, esp, 4
    mov _addr, esp
    write_prep 32
    str _tmp, [_xaddr]
    write_done 32
    gret 1
.gadget pop
    mov _addr, esp
    read_prep 32
    ldr _tmp, [_xaddr]
    add esp, esp, 4
    gret 1

.macro x name, reg
    .gadget addr_\name
        ldr _addr, [_ip]
        add _addr, _addr, \reg
        gret 1
.endm
.each_reg x
.purgem x
.gadget addr_none
    ldr _addr, [_ip]
    gret 1
.gadget_list addr, REG_LIST

.macro x name, reg
    .irp times, 1,2,4,8
        .gadget si_\name\()_\times
            .ifc \times,1
                add _addr, _addr, \reg
            .else; .ifc \times,2
                add _addr, _addr, \reg, lsl 1
            .else; .ifc \times,4
                add _addr, _addr, \reg, lsl 2
            .else; .ifc \times,8
                add _addr, _addr, \reg, lsl 3
            .endif; .endif; .endif; .endif
            gret
    .endr
.endm
.each_reg x
.purgem x

.pushsection_rodata
.global si_gadgets
si_gadgets:
.irp reg, REG_LIST
    .irp times, 1,2,4,8
        .quad gadget_si_\reg\()_\times
    .endr
.endr
.popsection

.gadget seg_gs
    ldr w8, [_cpu, CPU_tls_ptr]
    add _addr, _addr, w8
    gret

.irp type, read,write
    .global handle_\type\()_miss
    handle_\type\()_miss:
        save_c
        # %tlb actually points to tlb->entries
        sub x0, _tlb, TLB_entries
        mov w1, _addr
        .ifc \type,read
            mov x2, xzr
        .else
            mov x2, 1
        .endif
        bl tlb_handle_miss
        mov x19, x0
        restore_c
        cbz x19, segfault
        mov _xaddr, x19
        ret
.endr

segfault:
    ldr eip, [_ip]
    mov x0, INT_GPF
    b jit_exit

.global crosspage_load
crosspage_load:
    save_c
    sub x0, _tlb, TLB_entries
    add x2, _cpu, LOCAL_value
    mov w1, _addr
    mov x3, x19
    bl __tlb_read_cross_page
    mov x19, x0
    restore_c
    cbz x19, segfault
    str _addr, [_cpu, LOCAL_value_addr]
    add _xaddr, _cpu, LOCAL_value
    ret

.global crosspage_store
crosspage_store:
    save_c
    sub x0, _tlb, TLB_entries
    add x2, _cpu, LOCAL_value
    ldr w1, [_cpu, LOCAL_value_addr]
    mov x3, x19
    bl __tlb_write_cross_page
    mov x19, x0
    restore_c
    cbz x19, segfault
    ret
