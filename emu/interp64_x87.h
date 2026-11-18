// x87 for interp64.c (included there). The arithmetic itself is ish's
// existing soft-float code in emu/fpu.c and emu/float80.c.

static void x87_flags_from_cpu(struct ictx *c) {
    // fpu_comi leaves its result in cpu->zf/pf/cf
    struct cpu_state *cpu = c->cpu;
    c->cf = cpu->cf;
    c->zr = cpu->zf ? 0 : 1;
    c->pr = cpu->pf ? 0 : 1;
    c->of = 0; c->sn = 0; c->aux = 0;
}

static void x87_trunc_store(struct ictx *c, uint64_t addr, int sz) {
    struct cpu_state *cpu = c->cpu;
    enum f80_rounding_mode old = f80_rounding_mode;
    f80_rounding_mode = round_chop;
    int16_t i16; int32_t i32; int64_t i64;
    if (sz == 2) fpu_ist16(cpu, &i16);
    else if (sz == 4) fpu_ist32(cpu, &i32);
    else fpu_ist64(cpu, &i64);
    f80_rounding_mode = old;
    mwr(c, addr, sz, sz == 2 ? (uint64_t) (uint16_t) i16 : sz == 4 ? (uint64_t) (uint32_t) i32 : (uint64_t) i64);
}

static int exec_x87(struct ictx *c, const struct insn *in) {
    struct cpu_state *cpu = c->cpu;
    int op = in->op & 7, reg = in->reg & 7;
    if (in->mod != 3) {
        uint64_t addr = ea(c, in);
        float f; double dbl; int16_t i16; int32_t i32; int64_t i64; uint32_t u32; uint64_t u64;
        float80 f80;
        switch (op) {
            case 0: // D8: m32 real
                u32 = (uint32_t) mrd(c, addr, 4); memcpy(&f, &u32, 4);
                switch (reg) {
                    case 0: fpu_addm32(cpu, &f); break;
                    case 1: fpu_mulm32(cpu, &f); break;
                    case 2: fpu_comm32(cpu, &f); break;
                    case 3: fpu_comm32(cpu, &f); fpu_pop(cpu); break;
                    case 4: fpu_subm32(cpu, &f); break;
                    case 5: fpu_subrm32(cpu, &f); break;
                    case 6: fpu_divm32(cpu, &f); break;
                    default: fpu_divrm32(cpu, &f); break;
                }
                return INT_NONE;
            case 4: // DC: m64 real
                u64 = mrd(c, addr, 8); memcpy(&dbl, &u64, 8);
                switch (reg) {
                    case 0: fpu_addm64(cpu, &dbl); break;
                    case 1: fpu_mulm64(cpu, &dbl); break;
                    case 2: fpu_comm64(cpu, &dbl); break;
                    case 3: fpu_comm64(cpu, &dbl); fpu_pop(cpu); break;
                    case 4: fpu_subm64(cpu, &dbl); break;
                    case 5: fpu_subrm64(cpu, &dbl); break;
                    case 6: fpu_divm64(cpu, &dbl); break;
                    default: fpu_divrm64(cpu, &dbl); break;
                }
                return INT_NONE;
            case 2: // DA: m32 int
                i32 = (int32_t) mrd(c, addr, 4);
                switch (reg) {
                    case 0: fpu_iadd32(cpu, &i32); break;
                    case 1: fpu_imul32(cpu, &i32); break;
                    case 2: fpu_icom32(cpu, &i32); break;
                    case 3: fpu_icom32(cpu, &i32); fpu_pop(cpu); break;
                    case 4: fpu_isub32(cpu, &i32); break;
                    case 5: fpu_isubr32(cpu, &i32); break;
                    case 6: fpu_idiv32(cpu, &i32); break;
                    default: fpu_idivr32(cpu, &i32); break;
                }
                return INT_NONE;
            case 6: // DE: m16 int
                i16 = (int16_t) mrd(c, addr, 2);
                switch (reg) {
                    case 0: fpu_iadd16(cpu, &i16); break;
                    case 1: fpu_imul16(cpu, &i16); break;
                    case 2: fpu_icom16(cpu, &i16); break;
                    case 3: fpu_icom16(cpu, &i16); fpu_pop(cpu); break;
                    case 4: fpu_isub16(cpu, &i16); break;
                    case 5: fpu_isubr16(cpu, &i16); break;
                    case 6: fpu_idiv16(cpu, &i16); break;
                    default: fpu_idivr16(cpu, &i16); break;
                }
                return INT_NONE;
            case 1: // D9
                switch (reg) {
                    case 0:
                        u32 = (uint32_t) mrd(c, addr, 4); memcpy(&f, &u32, 4);
                        fpu_ldm32(cpu, &f);
                        return INT_NONE;
                    case 2: case 3:
                        fpu_stm32(cpu, &f); memcpy(&u32, &f, 4);
                        mwr(c, addr, 4, u32);
                        if (reg == 3) fpu_pop(cpu);
                        return INT_NONE;
                    case 4: { // fldenv
                        uint8_t env[28];
                        mread_n(c, addr, env, 28);
                        uint16_t w;
                        memcpy(&w, env, 2); cpu->fcw = w;
                        memcpy(&w, env + 4, 2); cpu->fsw = w;
                        f80_rounding_mode = cpu->rc;
                        return INT_NONE;
                    }
                    case 5:
                        cpu->fcw = (uint16_t) mrd(c, addr, 2);
                        f80_rounding_mode = cpu->rc;
                        return INT_NONE;
                    case 6: { // fnstenv
                        uint8_t env[28] = {0};
                        uint16_t w = cpu->fcw; memcpy(env, &w, 2);
                        w = cpu->fsw; memcpy(env + 4, &w, 2);
                        mwrite_n(c, addr, env, 28);
                        return INT_NONE;
                    }
                    case 7:
                        mwr(c, addr, 2, cpu->fcw);
                        return INT_NONE;
                }
                break;
            case 3: // DB
                switch (reg) {
                    case 0:
                        i32 = (int32_t) mrd(c, addr, 4);
                        fpu_ild32(cpu, &i32);
                        return INT_NONE;
                    case 1:
                        x87_trunc_store(c, addr, 4);
                        fpu_pop(cpu);
                        return INT_NONE;
                    case 2: case 3:
                        fpu_ist32(cpu, &i32);
                        mwr(c, addr, 4, (uint32_t) i32);
                        if (reg == 3) fpu_pop(cpu);
                        return INT_NONE;
                    case 5:
                        memset(&f80, 0, sizeof(f80));
                        mread_n(c, addr, &f80, 10);
                        fpu_ldm80(cpu, &f80);
                        return INT_NONE;
                    case 7:
                        memset(&f80, 0, sizeof(f80));
                        fpu_stm80(cpu, &f80);
                        mwrite_n(c, addr, &f80, 10);
                        fpu_pop(cpu);
                        return INT_NONE;
                }
                break;
            case 5: // DD
                switch (reg) {
                    case 0:
                        u64 = mrd(c, addr, 8); memcpy(&dbl, &u64, 8);
                        fpu_ldm64(cpu, &dbl);
                        return INT_NONE;
                    case 1:
                        x87_trunc_store(c, addr, 8);
                        fpu_pop(cpu);
                        return INT_NONE;
                    case 2: case 3:
                        fpu_stm64(cpu, &dbl); memcpy(&u64, &dbl, 8);
                        mwr(c, addr, 8, u64);
                        if (reg == 3) fpu_pop(cpu);
                        return INT_NONE;
                    case 7:
                        mwr(c, addr, 2, cpu->fsw);
                        return INT_NONE;
                }
                break;
            case 7: // DF
                switch (reg) {
                    case 0:
                        i16 = (int16_t) mrd(c, addr, 2);
                        fpu_ild16(cpu, &i16);
                        return INT_NONE;
                    case 1:
                        x87_trunc_store(c, addr, 2);
                        fpu_pop(cpu);
                        return INT_NONE;
                    case 2: case 3:
                        fpu_ist16(cpu, &i16);
                        mwr(c, addr, 2, (uint16_t) i16);
                        if (reg == 3) fpu_pop(cpu);
                        return INT_NONE;
                    case 5:
                        i64 = (int64_t) mrd(c, addr, 8);
                        fpu_ild64(cpu, &i64);
                        return INT_NONE;
                    case 7:
                        fpu_ist64(cpu, &i64);
                        mwr(c, addr, 8, (uint64_t) i64);
                        fpu_pop(cpu);
                        return INT_NONE;
                }
                break;
        }
        return undefined_insn(c, in);
    }

    int i = in->rm & 7;
    int full = (reg << 3) | i; // low 6 bits of the modrm byte
    switch (op) {
        case 0: // D8: st0 = st0 op sti
            switch (reg) {
                case 0: fpu_add(cpu, i, 0); break;
                case 1: fpu_mul(cpu, i, 0); break;
                case 2: fpu_com(cpu, i); break;
                case 3: fpu_com(cpu, i); fpu_pop(cpu); break;
                case 4: fpu_sub(cpu, i, 0); break;
                case 5: fpu_subr(cpu, i, 0); break;
                case 6: fpu_div(cpu, i, 0); break;
                default: fpu_divr(cpu, i, 0); break;
            }
            return INT_NONE;
        case 1: // D9
            switch (reg) {
                case 0: fpu_ld(cpu, i); return INT_NONE;
                case 1: fpu_xch(cpu, i); return INT_NONE;
                case 2: if (i == 0) return INT_NONE; break; // fnop
            }
            switch (full) {
                case 0x20: fpu_chs(cpu); return INT_NONE;
                case 0x21: fpu_abs(cpu); return INT_NONE;
                case 0x24: fpu_tst(cpu); return INT_NONE;
                case 0x25: fpu_xam(cpu); return INT_NONE;
                case 0x28: fpu_ldc(cpu, fconst_one); return INT_NONE;
                case 0x29: fpu_ldc(cpu, fconst_log2t); return INT_NONE;
                case 0x2a: fpu_ldc(cpu, fconst_log2e); return INT_NONE;
                case 0x2b: fpu_ldc(cpu, fconst_pi); return INT_NONE;
                case 0x2c: fpu_ldc(cpu, fconst_log2); return INT_NONE;
                case 0x2d: fpu_ldc(cpu, fconst_ln2); return INT_NONE;
                case 0x2e: fpu_ldc(cpu, fconst_zero); return INT_NONE;
                case 0x30: fpu_2xm1(cpu); return INT_NONE;
                case 0x31: fpu_yl2x(cpu); return INT_NONE;
                case 0x32: { // fptan
                    double v = tan(f80_to_double(ST_(cpu, 0)));
                    ST_(cpu, 0) = f80_from_double(v);
                    fpu_ldc(cpu, fconst_one);
                    cpu->c2 = 0;
                    return INT_NONE;
                }
                case 0x33: fpu_patan(cpu); return INT_NONE;
                case 0x34: fpu_xtract(cpu); return INT_NONE;
                case 0x35: // fprem1
                    ST_(cpu, 0) = f80_from_double(remainder(f80_to_double(ST_(cpu, 0)), f80_to_double(ST_(cpu, 1))));
                    cpu->c2 = 0;
                    return INT_NONE;
                case 0x36: cpu->top--; return INT_NONE;
                case 0x37: fpu_incstp(cpu); return INT_NONE;
                case 0x38: fpu_prem(cpu); return INT_NONE;
                case 0x39: { // fyl2xp1
                    double v = f80_to_double(ST_(cpu, 1)) * log2(1.0 + f80_to_double(ST_(cpu, 0)));
                    ST_(cpu, 1) = f80_from_double(v);
                    fpu_pop(cpu);
                    return INT_NONE;
                }
                case 0x3a: fpu_sqrt(cpu); return INT_NONE;
                case 0x3b: { // fsincos
                    double v = f80_to_double(ST_(cpu, 0));
                    ST_(cpu, 0) = f80_from_double(sin(v));
                    cpu->top--;
                    ST_(cpu, 0) = f80_from_double(cos(v));
                    cpu->c2 = 0;
                    return INT_NONE;
                }
                case 0x3c: fpu_rndint(cpu); return INT_NONE;
                case 0x3d: fpu_scale(cpu); return INT_NONE;
                case 0x3e: fpu_sin(cpu); return INT_NONE;
                case 0x3f: fpu_cos(cpu); return INT_NONE;
            }
            break;
        case 2: // DA
            if (reg < 4) {
                bool t = reg == 0 ? c->cf : reg == 1 ? c->zr == 0 : reg == 2 ? (c->cf || c->zr == 0) : !__builtin_parity(c->pr);
                if (t) ST_(cpu, 0) = ST_(cpu, i);
                return INT_NONE;
            }
            if (full == 0x29) { // fucompp
                fpu_com(cpu, 1); fpu_pop(cpu); fpu_pop(cpu);
                return INT_NONE;
            }
            break;
        case 3: // DB
            if (reg < 4) {
                bool t = reg == 0 ? c->cf : reg == 1 ? c->zr == 0 : reg == 2 ? (c->cf || c->zr == 0) : !__builtin_parity(c->pr);
                if (!t) ST_(cpu, 0) = ST_(cpu, i);
                return INT_NONE;
            }
            if (full == 0x22) { fpu_clex(cpu); return INT_NONE; }
            if (full == 0x23) { // fninit
                cpu->fcw = 0x37f; cpu->fsw = 0;
                f80_rounding_mode = round_to_nearest;
                return INT_NONE;
            }
            if (reg == 5 || reg == 6) {
                fpu_comi(cpu, i);
                x87_flags_from_cpu(c);
                return INT_NONE;
            }
            break;
        case 4: // DC: sti = sti op st0
            switch (reg) {
                case 0: fpu_add(cpu, 0, i); break;
                case 1: fpu_mul(cpu, 0, i); break;
                case 2: fpu_com(cpu, i); break;
                case 3: fpu_com(cpu, i); fpu_pop(cpu); break;
                case 4: fpu_subr(cpu, 0, i); break;
                case 5: fpu_sub(cpu, 0, i); break;
                case 6: fpu_divr(cpu, 0, i); break;
                default: fpu_div(cpu, 0, i); break;
            }
            return INT_NONE;
        case 5: // DD
            switch (reg) {
                case 0: return INT_NONE; // ffree: no tag word here
                case 2: fpu_st(cpu, i); return INT_NONE;
                case 3: fpu_st(cpu, i); fpu_pop(cpu); return INT_NONE;
                case 4: fpu_com(cpu, i); return INT_NONE;
                case 5: fpu_com(cpu, i); fpu_pop(cpu); return INT_NONE;
            }
            break;
        case 6: // DE: sti = sti op st0, pop
            switch (reg) {
                case 0: fpu_add(cpu, 0, i); break;
                case 1: fpu_mul(cpu, 0, i); break;
                case 3:
                    if (i != 1) return undefined_insn(c, in);
                    fpu_com(cpu, 1); fpu_pop(cpu); // fcompp
                    break;
                case 4: fpu_subr(cpu, 0, i); break;
                case 5: fpu_sub(cpu, 0, i); break;
                case 6: fpu_divr(cpu, 0, i); break;
                case 7: fpu_div(cpu, 0, i); break;
                default: return undefined_insn(c, in);
            }
            fpu_pop(cpu);
            return INT_NONE;
        case 7: // DF
            if (full == 0x20) {
                cpu->ax = cpu->fsw;
                return INT_NONE;
            }
            if (reg == 5 || reg == 6) {
                fpu_comi(cpu, i);
                x87_flags_from_cpu(c);
                fpu_pop(cpu);
                return INT_NONE;
            }
            break;
    }
    return undefined_insn(c, in);
}
