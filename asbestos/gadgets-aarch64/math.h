.macro do_add op, dst, src, s
    # setting flags: a horror story
    .ifb \s
        # for 32-bit operands, we can just do the operation and the chip
        # will set v and c right, which we copy
        \op\()s \dst, \dst, \src
        cset w10, vs
        strb w10, [_cpu, CPU_of]
        .ifin(\op, add,adc)
            cset w10, cs
        .endifin
        .ifin(\op, sub,sbc)
            cset w10, cc
        .endifin
        strb w10, [_cpu, CPU_cf]
    .else
        # for 16 or 8 bit operands...
        # first figure out unsigned overflow
        uxt\s w10, \dst
        .ifin(\op, add,sub)
            \op w10, w10, \src, uxt\s
        .endifin
        .ifin(\op, adc,sbc)
            uxt\s w9, \src
            \op w10, w10, w9
        .endifin
        .ifc \s,b
            lsr w10, w10, 8
        .else
            lsr w10, w10, 16
        .endif
        strb w10, [_cpu, CPU_cf]
        # now signed overflow
        sxt\s w10, \dst
        .ifin(\op, add,sub)
            \op \dst, w10, \src, sxt\s
        .endifin
        .ifin(\op, adc,sbc)
            # help me
            sxt\s w9, \src
            \op \dst, w10, w9
        .endifin
        cmp \dst, \dst, sxt\s
        cset w10, ne
        strb w10, [_cpu, CPU_of]
    .endif
.endm

# vim: ft=gas
