#include "gadgets.h"

.gadget call
    sub $4, %_esp
    movl %_esp, %_addr
    write_prep
    movl 8(%_ip), %_tmp
    movl %_tmp, (%_addrq)
    movl (%_ip), %_eip
    movl $-1, %_tmp
    jmp jit_exit
