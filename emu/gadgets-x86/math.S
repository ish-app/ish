#include "gadgets.h"

.macro g_load reg
    movl %\reg, %_tmp
.endm
.reg_gadgets load
.gadget load_mem32
    read_prep
    movl (%_addrq), %_tmp
    gret

.macro g_store reg
    movl %_tmp, %\reg
.endm
.reg_gadgets store

.gadget sub_imm
    subl (%_ip), %_tmp
    gret 1

.rodata
.gadget_array load
.gadget_array store
.gadget_array sub
