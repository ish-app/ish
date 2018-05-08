#include "gadgets.h"

.gadget push
    sub $4, %xsp
    movl %xsp, %addr
    write_prep
    movl %tmp, (%addrq)
    gret
.gadget pop
    movl %xsp, %addr
    read_prep
    movl (%addrq), %tmp
    add $4, %xsp
    gret

.macro g_load reg
    movl %\reg, %tmp
.endm
.reg_gadgets load
.macro g_store reg
    movl %tmp, %\reg
.endm
.reg_gadgets store

.gadget sub_imm
    subl (%ip), %tmp
    gret 1

.data
.gadget_array load
.gadget_array store
.gadget_array sub
