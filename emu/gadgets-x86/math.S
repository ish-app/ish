#include "gadgets.h"

.gadget push
    sub $4, %_esp
    movl %_esp, %_addr
    write_prep
    movl %_tmp, (%_addrq)
    gret
.gadget pop
    movl %_esp, %_addr
    read_prep
    movl (%_addrq), %_tmp
    add $4, %_esp
    gret

.macro g_load reg
    movl %\reg, %_tmp
.endm
.reg_gadgets load
.macro g_store reg
    movl %_tmp, %\reg
.endm
.reg_gadgets store

.gadget sub_imm
    subl (%_ip), %_tmp
    gret 1

.data
.gadget_array load
.gadget_array store
.gadget_array sub
