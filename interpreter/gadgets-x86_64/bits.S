#include "gadgets.h"

.macro do_shift type, size, s, ss
    .irp arg, reg_c,imm
        .gadget \type\size\()_\arg
            .ifc \arg,imm
                movl %ecx, %r14d
                movb (%_ip), %cl
            .endif
            testb $31, %cl
            jz 1f
            .ifin(\type, rcl,rcr)
                btw $0, CPU_cf(%_cpu)
            .endifin
            \type\()\ss %cl, %tmp\s
            setf_oc
            .ifin(\type, shl,shr,sar)
                setf_zsp %tmp\s, \ss
                clearf_a
            .endifin
        1:
            .ifc \arg,imm
                movl %r14d, %ecx
            .endif
            .ifc \arg,imm
                gret 1
            .else
                gret
            .endif
    .endr
.endm

.irp type, shl,shr,sar,rol,ror,rcl,rcr
    .irp size, 8,16,32
        ss \size, do_shift, \type
    .endr
    .gadget_array \type
.endr

.macro do_shiftd op, arg
    .macro x name, reg
        .gadget \op\()_\arg\()32_\name
            .ifc \arg,imm
                .ifnc \name,reg_c
                    pushq %rcx
                .else
                    pushq %rdx
                    xchg %ecx, %edx
                .endif
                movb (%_ip), %cl
            .endif
            testb $(32 - 1), %cl
            jz 1f
            .ifc \name,reg_c
                \op %cl, %edx, %tmpd
            .else
                \op %cl, %\reg, %tmpd
            .endif
            setf_oc
            setf_zsp %tmpd, l
        1:
            .ifc \arg,imm
                .ifnc \name,reg_c
                    popq %rcx
                .else
                    xchg %ecx, %edx
                    popq %rdx
                .endif
                gret 1
            .else
                gret
            .endif
    .endm
    .each_reg x
    .purgem x
    .gadget_array \op\()_\arg
.endm
.irp op, shrd,shld
    .irp arg, imm,cl
        do_shiftd \op, \arg
    .endr
.endr

.macro do_bt_op op, arg, size, s, ss
    and\ss $(\size-1), %tmp\s
    \op\ss %tmp\s, \arg
    setf_c
.endm

.macro do_bt op, size, s, ss
    .gadget \op\size\()_mem
        mov\ss %tmp\s, %r14\s
        andl $~(\size-1), %r14d
        shrl $3, %r14d
        addl %r14d, %_addr
        read_prep \size, \op\size\()_mem
        do_bt_op \op, (%_addrq), \size, \s, \ss
        gret 1

    .macro x name reg
        .gadget \op\size\()_\name
            do_bt_op \op, %\reg, \size, \s, \ss
            gret
    .endm
    .each_reg_size \size, x
    .purgem x
.endm

.irp op, bt,btc,bts,btr
    .irp size, 16,32
        ss \size, do_bt, \op
    .endr
    .gadget_array \op
.endr

# atomic versions of the above

.macro do_bt_atomic op, size, s, ss
    .gadget atomic_\op\size\()_mem
        mov\ss %tmp\s, %r14\s
        andl $~(\size-1), %r14d
        shrl $3, %r14d
        addl %r14d, %_addr
        read_prep \size, atomic_\op\size\()_mem
        and\ss $(\size-1), %tmp\s
        lock \op\ss %tmp\s, (%_addrq)
        setf_c
        gret 1
.endm

.irp op, btc,bts,btr
    .irp size, 16,32
        ss \size, do_bt_atomic, \op
    .endr
    .gadget_array atomic_\op
.endr

.macro x name reg
    .gadget bswap_\name
        bswap %\reg
        gret
.endm
.each_reg x
.purgem x
.gadget_list bswap, REG_LIST
