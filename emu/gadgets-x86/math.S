#include "gadgets.h"
# asmsyntax=gas (for vim)

.gadget load_addr
    movl %_addr, %_tmp
    gret

# cool thing where I just write a few nested loops and it generates all the gadgets automatically

.macro do_op op arg
    .ifc \op,load
        movl \arg, %_tmp
    .else; .ifc \op,store
        movl %_tmp, \arg
    .else
        \op\()l \arg, %_tmp
    .endif; .endif
.endm

.irp op, load,store,add,sub
    .macro x reg
        do_op \op, %\reg
    .endm
    .reg_gadgets \op

    .gadget \op\()_imm
        do_op \op, (%_ip)
        gret 1

    .gadget \op\()_mem32
        .ifc \op,store
            write_prep
        .else
            read_prep
        .endif
        do_op \op, (%_addrq)
        gret

    .section .rodata
    .gadget_array \op
    .previous
.endr
