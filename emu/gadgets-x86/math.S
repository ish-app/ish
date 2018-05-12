#include "gadgets.h"
# asmsyntax=gas (for vim)

.macro g_load reg
    movl %\reg, %_tmp
.endm
.reg_gadgets load
.gadget load_mem32
    read_prep
    movl (%_addrq), %_tmp
    gret

.gadget load_addr
    movl %_addr, %_tmp
    gret

.macro g_store reg
    movl %_tmp, %\reg
.endm
.reg_gadgets store

.irp op, load,store,add,sub

.gadget \op\()_imm
    .ifc \op,load
        movl (%_ip), %_tmp
    .else; .ifc \op,store
        movl %_tmp, (%_ip)
    .else
        \op\()l (%_ip), %_tmp
    .endif; .endif
    gret 1

.endr

.rodata
.gadget_array load
.gadget_array store
.gadget_array add
.gadget_array sub
