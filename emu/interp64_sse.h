// SSE/SSE2 for interp64.c (included there; not a standalone header).

static __thread int host_rc = 0;
static void apply_rounding(int rc) {
    static const int modes[4] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    fesetround(modes[rc & 3]);
    host_rc = rc & 3;
}
static forceinline void sync_rounding(struct cpu_state *cpu) {
    int rc = (cpu->mxcsr >> 13) & 3;
    if (unlikely(rc != host_rc))
        apply_rounding(rc);
}
static void set_mxcsr(struct ictx *c, uint32_t v) {
    c->cpu->mxcsr = v & 0xffff;
    sync_rounding(c->cpu);
}

#define ST_(cpu, i) (cpu)->fp[((cpu)->top + (i)) & 7]

static void fxsave(struct ictx *c, uint64_t addr) {
    struct cpu_state *cpu = c->cpu;
    uint8_t buf[512] = {0};
    uint16_t w = cpu->fcw; memcpy(buf, &w, 2);
    w = cpu->fsw; memcpy(buf + 2, &w, 2);
    buf[4] = 0xff; // abridged tag word: everything valid
    uint32_t d = cpu->mxcsr; memcpy(buf + 24, &d, 4);
    d = 0xffff; memcpy(buf + 28, &d, 4);
    for (int i = 0; i < 8; i++)
        memcpy(buf + 32 + 16 * i, &ST_(cpu, i), 10);
    memcpy(buf + 160, cpu->xmm, 16 * 16);
    mwrite_n(c, addr, buf, 512);
}
static void fxrstor(struct ictx *c, uint64_t addr) {
    struct cpu_state *cpu = c->cpu;
    uint8_t buf[512];
    mread_n(c, addr, buf, 512);
    uint16_t w;
    memcpy(&w, buf, 2); cpu->fcw = w;
    memcpy(&w, buf + 2, 2); cpu->fsw = w;
    f80_rounding_mode = cpu->rc;
    uint32_t d; memcpy(&d, buf + 24, 4);
    for (int i = 0; i < 8; i++)
        memcpy(&ST_(cpu, i), buf + 32 + 16 * i, 10);
    memcpy(cpu->xmm, buf + 160, 16 * 16);
    set_mxcsr(c, d);
}

// NaN results the way x86 SSE produces them, whatever the host CPU does (arm64
// picks operands differently and its default NaN is positive):
// a NaN in the first operand wins, then one in the second (made quiet); an
// invalid operation on numbers gives the negative default NaN.
static __attribute__((noinline)) float x86_nanf(float a, float b) {
    uint32_t u;
    if (a != a) { memcpy(&u, &a, 4); u |= 0x00400000; }
    else if (b != b) { memcpy(&u, &b, 4); u |= 0x00400000; }
    else u = 0xffc00000;
    float r; memcpy(&r, &u, 4);
    return r;
}
static __attribute__((noinline)) double x86_nan(double a, double b) {
    uint64_t u;
    if (a != a) { memcpy(&u, &a, 8); u |= 0x0008000000000000ull; }
    else if (b != b) { memcpy(&u, &b, 8); u |= 0x0008000000000000ull; }
    else u = 0xfff8000000000000ull;
    double r; memcpy(&r, &u, 8);
    return r;
}
static forceinline float nf(float r, float a, float b) {
    return unlikely(r != r) ? x86_nanf(a, b) : r;
}
static forceinline double nd(double r, double a, double b) {
    return unlikely(r != r) ? x86_nan(a, b) : r;
}

// read n bytes of the xmm/m operand into t (the rest of t is zero for memory)
static forceinline void xload(struct ictx *c, const struct insn *in, union xmm_reg *t, unsigned n) {
    if (in->mod == 3) {
        *t = c->cpu->xmm[in->rm];
    } else {
        uint64_t addr = ea(c, in);
        t->qw[0] = t->qw[1] = 0;
        if (likely((addr & (PAGE_SIZE - 1)) + n <= PAGE_SIZE))
            memcpy(t, xlat_r(c, addr), n);
        else
            mread_n(c, addr, t, n);
    }
}
static forceinline void xstore(struct ictx *c, const struct insn *in, const union xmm_reg *t, unsigned n) {
    uint64_t addr = ea(c, in);
    if (likely((addr & (PAGE_SIZE - 1)) + n <= PAGE_SIZE)) {
        void *p = xlat_w(c, addr);
        if (unlikely(c->log))
            wlog_add(c, addr, n);
        memcpy(p, t, n);
    } else {
        mwrite_n(c, addr, t, n);
    }
}

static forceinline int32_t f2i32(double v, bool trunc) {
    if (!trunc)
        v = nearbyint(v);
    if (!(v > -2147483649.0 && v < 2147483648.0))
        return INT32_MIN;
    return (int32_t) v;
}
static forceinline int64_t f2i64(double v, bool trunc) {
    if (!trunc)
        v = nearbyint(v);
    if (!(v >= -9223372036854775808.0 && v < 9223372036854775808.0))
        return INT64_MIN;
    return (int64_t) v;
}

static forceinline int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t) v; }
static forceinline int8_t sat8(int32_t v) { return v > 127 ? 127 : v < -128 ? -128 : (int8_t) v; }
static forceinline uint8_t satu8(int32_t v) { return v > 255 ? 255 : v < 0 ? 0 : (uint8_t) v; }
static forceinline uint16_t satu16(int32_t v) { return v > 65535 ? 65535 : v < 0 ? 0 : (uint16_t) v; }

static void sse_compare(struct ictx *c, double x, double y) {
    c->of = 0; c->sn = 0; c->aux = 0;
    if (isnan(x) || isnan(y)) {
        c->zr = 0; c->pr = 0; c->cf = 1;
    } else {
        c->zr = x == y ? 0 : 1;
        c->pr = 1;
        c->cf = x < y;
    }
}

static forceinline bool cmp_pred(int pred, double x, double y) {
    bool un = isnan(x) || isnan(y);
    switch (pred & 7) {
        case 0: return !un && x == y;
        case 1: return !un && x < y;
        case 2: return !un && x <= y;
        case 3: return un;
        case 4: return un || x != y;
        case 5: return un || !(x < y);
        case 6: return un || !(x <= y);
        default: return !un;
    }
}

#define LANES(n, field, EXPR) do { for (int k = 0; k < (n); k++) { __typeof__(d->field[0]) x = d->field[k], y = s.field[k]; (void) x; (void) y; d->field[k] = (EXPR); } } while (0)
#define LANES_S(n, field, T, EXPR) do { for (int k = 0; k < (n); k++) { T x = (T) d->field[k], y = (T) s.field[k]; (void) x; (void) y; d->field[k] = (__typeof__(d->field[0])) (EXPR); } } while (0)

// Packed integer operations shared by SSE2 (all 128 bits) and MMX (the low 64
// bits; the callers arrange the halves). *d receives t OP s.
static bool pint_op(int op, union xmm_reg *d, const union xmm_reg *tp, const union xmm_reg *sp) {
    const union xmm_reg t = *tp, s = *sp;
    *d = t;
    switch (op) {
        case 0x60: for (int k = 0; k < 8; k++) { d->u8[2 * k] = t.u8[k]; d->u8[2 * k + 1] = s.u8[k]; } break;
        case 0x61: for (int k = 0; k < 4; k++) { d->u16[2 * k] = t.u16[k]; d->u16[2 * k + 1] = s.u16[k]; } break;
        case 0x62: for (int k = 0; k < 2; k++) { d->u32[2 * k] = t.u32[k]; d->u32[2 * k + 1] = s.u32[k]; } break;
        case 0x68: for (int k = 0; k < 8; k++) { d->u8[2 * k] = t.u8[8 + k]; d->u8[2 * k + 1] = s.u8[8 + k]; } break;
        case 0x69: for (int k = 0; k < 4; k++) { d->u16[2 * k] = t.u16[4 + k]; d->u16[2 * k + 1] = s.u16[4 + k]; } break;
        case 0x6a: for (int k = 0; k < 2; k++) { d->u32[2 * k] = t.u32[2 + k]; d->u32[2 * k + 1] = s.u32[2 + k]; } break;
        case 0x6c: d->qw[1] = s.qw[0]; break;
        case 0x6d: d->qw[0] = t.qw[1]; d->qw[1] = s.qw[1]; break;
        case 0x63: for (int k = 0; k < 8; k++) { d->u8[k] = (uint8_t) sat8((int16_t) t.u16[k]); d->u8[8 + k] = (uint8_t) sat8((int16_t) s.u16[k]); } break;
        case 0x67: for (int k = 0; k < 8; k++) { d->u8[k] = satu8((int16_t) t.u16[k]); d->u8[8 + k] = satu8((int16_t) s.u16[k]); } break;
        case 0x6b: for (int k = 0; k < 4; k++) { d->u16[k] = (uint16_t) sat16((int32_t) t.u32[k]); d->u16[4 + k] = (uint16_t) sat16((int32_t) s.u32[k]); } break;
        case 0x64: LANES_S(16, u8, int8_t, x > y ? 0xff : 0); break;
        case 0x65: LANES_S(8, u16, int16_t, x > y ? 0xffff : 0); break;
        case 0x66: LANES_S(4, u32, int32_t, x > y ? 0xffffffffu : 0); break;
        case 0x74: LANES(16, u8, x == y ? 0xff : 0); break;
        case 0x75: LANES(8, u16, x == y ? 0xffff : 0); break;
        case 0x76: LANES(4, u32, x == y ? 0xffffffffu : 0); break;
        case 0xd1: LANES(8, u16, s.qw[0] > 15 ? 0 : x >> s.qw[0]); break;
        case 0xd2: LANES(4, u32, s.qw[0] > 31 ? 0 : x >> s.qw[0]); break;
        case 0xd3: LANES(2, qw, s.qw[0] > 63 ? 0 : x >> s.qw[0]); break;
        case 0xf1: LANES(8, u16, s.qw[0] > 15 ? 0 : x << s.qw[0]); break;
        case 0xf2: LANES(4, u32, s.qw[0] > 31 ? 0 : x << s.qw[0]); break;
        case 0xf3: LANES(2, qw, s.qw[0] > 63 ? 0 : x << s.qw[0]); break;
        case 0xe1: LANES_S(8, u16, int16_t, x >> (s.qw[0] > 15 ? 15 : s.qw[0])); break;
        case 0xe2: LANES_S(4, u32, int32_t, x >> (s.qw[0] > 31 ? 31 : s.qw[0])); break;
        case 0xd4: LANES(2, qw, x + y); break;
        case 0xd5: LANES_S(8, u16, int16_t, (int32_t) x * y); break;
        case 0xd8: LANES(16, u8, x > y ? x - y : 0); break;
        case 0xd9: LANES(8, u16, x > y ? x - y : 0); break;
        case 0xda: LANES(16, u8, x < y ? x : y); break;
        case 0xdb: d->qw[0] = t.qw[0] & s.qw[0]; d->qw[1] = t.qw[1] & s.qw[1]; break;
        case 0xdc: LANES(16, u8, satu8((int32_t) x + y)); break;
        case 0xdd: LANES(8, u16, satu16((int32_t) x + y)); break;
        case 0xde: LANES(16, u8, x > y ? x : y); break;
        case 0xdf: d->qw[0] = ~t.qw[0] & s.qw[0]; d->qw[1] = ~t.qw[1] & s.qw[1]; break;
        case 0xe0: LANES(16, u8, (x + y + 1) >> 1); break;
        case 0xe3: LANES(8, u16, ((uint32_t) x + y + 1) >> 1); break;
        case 0xe4: LANES(8, u16, ((uint32_t) x * y) >> 16); break;
        case 0xe5: LANES_S(8, u16, int16_t, ((int32_t) x * y) >> 16); break;
        case 0xe8: LANES_S(16, u8, int8_t, sat8((int32_t) x - y)); break;
        case 0xe9: LANES_S(8, u16, int16_t, sat16((int32_t) x - y)); break;
        case 0xea: LANES_S(8, u16, int16_t, x < y ? x : y); break;
        case 0xeb: d->qw[0] = t.qw[0] | s.qw[0]; d->qw[1] = t.qw[1] | s.qw[1]; break;
        case 0xec: LANES_S(16, u8, int8_t, sat8((int32_t) x + y)); break;
        case 0xed: LANES_S(8, u16, int16_t, sat16((int32_t) x + y)); break;
        case 0xee: LANES_S(8, u16, int16_t, x > y ? x : y); break;
        case 0xef: d->qw[0] = t.qw[0] ^ s.qw[0]; d->qw[1] = t.qw[1] ^ s.qw[1]; break;
        case 0xf4: for (int k = 0; k < 2; k++) d->qw[k] = (uint64_t) t.u32[2 * k] * s.u32[2 * k]; break;
        case 0xf5: for (int k = 0; k < 4; k++) d->u32[k] = (uint32_t) ((int32_t) (int16_t) t.u16[2 * k] * (int16_t) s.u16[2 * k] + (int32_t) (int16_t) t.u16[2 * k + 1] * (int16_t) s.u16[2 * k + 1]); break;
        case 0xf6:
            for (int h = 0; h < 2; h++) {
                unsigned sum = 0;
                for (int k = 0; k < 8; k++) sum += (unsigned) abs((int) t.u8[8 * h + k] - (int) s.u8[8 * h + k]);
                d->qw[h] = sum;
            }
            break;
        case 0xf8: LANES(16, u8, x - y); break;
        case 0xf9: LANES(8, u16, x - y); break;
        case 0xfa: LANES(4, u32, x - y); break;
        case 0xfb: LANES(2, qw, x - y); break;
        case 0xfc: LANES(16, u8, x + y); break;
        case 0xfd: LANES(8, u16, x + y); break;
        case 0xfe: LANES(4, u32, x + y); break;
        default:
            return false;
    }
    return true;
}

// shifts by an immediate (0F 71/72/73). g is the modrm reg field.
static bool pshift_imm(int op, int g, unsigned n, union xmm_reg *d, bool sse) {
    if (op == 0x71) {
        for (int k = 0; k < 8; k++) {
            if (g == 2) d->u16[k] = n > 15 ? 0 : (uint16_t) (d->u16[k] >> n);
            else if (g == 4) d->u16[k] = (uint16_t) ((int16_t) d->u16[k] >> (n > 15 ? 15 : n));
            else if (g == 6) d->u16[k] = n > 15 ? 0 : (uint16_t) (d->u16[k] << n);
            else return false;
        }
    } else if (op == 0x72) {
        for (int k = 0; k < 4; k++) {
            if (g == 2) d->u32[k] = n > 31 ? 0 : d->u32[k] >> n;
            else if (g == 4) d->u32[k] = (uint32_t) ((int32_t) d->u32[k] >> (n > 31 ? 31 : n));
            else if (g == 6) d->u32[k] = n > 31 ? 0 : d->u32[k] << n;
            else return false;
        }
    } else {
        if (g == 2) { for (int k = 0; k < 2; k++) d->qw[k] = n > 63 ? 0 : d->qw[k] >> n; }
        else if (g == 6) { for (int k = 0; k < 2; k++) d->qw[k] = n > 63 ? 0 : d->qw[k] << n; }
        else if (g == 3 && sse) { d->u128 = n > 15 ? 0 : d->u128 >> (8 * n); }
        else if (g == 7 && sse) { d->u128 = n > 15 ? 0 : d->u128 << (8 * n); }
        else return false;
    }
    return true;
}

// MMX: the same opcodes as the SSE2 integer set without a prefix, on the
// 64-bit mm registers. Operations are computed in the low half of a 128-bit
// value so the SSE2 code can be shared.
static int exec_mmx(struct ictx *c, const struct insn *in) {
    struct cpu_state *cpu = c->cpu;
    int op = in->op & 0xff;
    bool mem = in->mod != 3;
    union mm_reg *md = &cpu->mm[in->reg & 7];
    int gsz = in->opsz == 8 ? 8 : 4;
    union xmm_reg t, s, d;
    t.qw[0] = md->qw; t.qw[1] = 0;
    s.qw[1] = 0;

    switch (op) {
        case 0x77: // emms
            return INT_NONE;
        case 0x6e:
            md->qw = eget(c, in, gsz);
            return INT_NONE;
        case 0x7e:
            eset(c, in, gsz, md->qw);
            return INT_NONE;
        case 0x6f:
            md->qw = mem ? mrd(c, ea(c, in), 8) : cpu->mm[in->rm & 7].qw;
            return INT_NONE;
        case 0x7f: case 0xe7:
            if (mem) mwr(c, ea(c, in), 8, md->qw);
            else if (op == 0x7f) cpu->mm[in->rm & 7].qw = md->qw;
            else break;
            return INT_NONE;
        case 0x70: { // pshufw
            s.qw[0] = mem ? mrd(c, ea(c, in), 8) : cpu->mm[in->rm & 7].qw;
            unsigned i = (unsigned) in->imm;
            for (int k = 0; k < 4; k++) d.u16[k] = s.u16[(i >> (2 * k)) & 3];
            md->qw = d.qw[0];
            return INT_NONE;
        }
        case 0x71: case 0x72: case 0x73:
            if (mem) break;
            d.qw[0] = cpu->mm[in->rm & 7].qw; d.qw[1] = 0;
            if (!pshift_imm(op, in->reg & 7, (unsigned) in->imm & 0xff, &d, false))
                break;
            cpu->mm[in->rm & 7].qw = d.qw[0];
            return INT_NONE;
        case 0xc4: {
            uint64_t v = in->mod == 3 ? rget(cpu, in, in->rm, 4) : mrd(c, ea(c, in), 2);
            t.u16[in->imm & 3] = (uint16_t) v;
            md->qw = t.qw[0];
            return INT_NONE;
        }
        case 0xc5:
            if (mem) break;
            s.qw[0] = cpu->mm[in->rm & 7].qw;
            cpu->regs[in->reg] = s.u16[in->imm & 3];
            return INT_NONE;
        case 0xd7: {
            if (mem) break;
            s.qw[0] = cpu->mm[in->rm & 7].qw;
            uint32_t m = 0;
            for (int k = 0; k < 8; k++) m |= (uint32_t) (s.u8[k] >> 7) << k;
            cpu->regs[in->reg] = m;
            return INT_NONE;
        }
        case 0xf7: { // maskmovq
            if (mem) break;
            s.qw[0] = cpu->mm[in->rm & 7].qw;
            uint64_t addr = ((in->pfx & P_ADDR32) ? (uint32_t) cpu->rdi : cpu->rdi) + segbase(c, in);
            for (int k = 0; k < 8; k++)
                if (s.u8[k] & 0x80)
                    mwr(c, addr + k, 1, t.u8[k]);
            return INT_NONE;
        }
        case 0x6c: case 0x6d: case 0xd6:
            return undefined_insn(c, in); // SSE2 only
    }
    if (op == 0x71 || op == 0x72 || op == 0x73 || op == 0x7f || op == 0xe7 || op == 0xc5 || op == 0xd7 || op == 0xf7)
        return undefined_insn(c, in);

    s.qw[0] = mem ? mrd(c, ea(c, in), 8) : cpu->mm[in->rm & 7].qw;
    switch (op) {
        case 0x68: case 0x69: case 0x6a:
            // the high half of an mm register is the second quarter of the 128-bit view
            if (!pint_op(op - 8, &d, &t, &s))
                return undefined_insn(c, in);
            md->qw = d.qw[1];
            return INT_NONE;
        case 0x63: case 0x67:
            pint_op(op, &d, &t, &s);
            md->qw = (uint64_t) d.u32[0] | ((uint64_t) d.u32[2] << 32);
            return INT_NONE;
        case 0x6b:
            pint_op(op, &d, &t, &s);
            md->qw = (uint64_t) d.u16[0] | ((uint64_t) d.u16[1] << 16) | ((uint64_t) d.u16[4] << 32) | ((uint64_t) d.u16[5] << 48);
            return INT_NONE;
    }
    if (!pint_op(op, &d, &t, &s))
        return undefined_insn(c, in);
    md->qw = d.qw[0];
    return INT_NONE;
}

static int exec_sse(struct ictx *c, const struct insn *in) {
    struct cpu_state *cpu = c->cpu;
    int op = in->op & 0xff;
    int p = (in->pfx & P_F3) ? 2 : (in->pfx & P_F2) ? 3 : (in->pfx & P_66) ? 1 : 0;
    union xmm_reg *d = &cpu->xmm[in->reg];
    union xmm_reg s;
    bool mem = in->mod != 3;
    int gsz = in->opsz == 8 ? 8 : 4;

    switch (op) {
        case 0x10:
            if (p < 2) {
                xload(c, in, &s, 16);
                *d = s;
            } else if (p == 2) {
                xload(c, in, &s, 4);
                if (mem) *d = s; else d->u32[0] = s.u32[0];
            } else {
                xload(c, in, &s, 8);
                if (mem) *d = s; else d->qw[0] = s.qw[0];
            }
            return INT_NONE;
        case 0x11:
            if (!mem) {
                union xmm_reg *e = &cpu->xmm[in->rm];
                if (p < 2) *e = *d;
                else if (p == 2) e->u32[0] = d->u32[0];
                else e->qw[0] = d->qw[0];
            } else {
                xstore(c, in, d, p < 2 ? 16 : p == 2 ? 4 : 8);
            }
            return INT_NONE;
        case 0x12:
            if (p == 3) { // movddup
                xload(c, in, &s, 8);
                d->qw[0] = d->qw[1] = s.qw[0];
                return INT_NONE;
            }
            if (p == 2) { // movsldup
                xload(c, in, &s, 16);
                d->u32[0] = d->u32[1] = s.u32[0];
                d->u32[2] = d->u32[3] = s.u32[2];
                return INT_NONE;
            }
            xload(c, in, &s, 8);
            d->qw[0] = (mem || p == 1) ? s.qw[0] : s.qw[1];
            return INT_NONE;
        case 0x13:
            if (p > 1 || !mem) break;
            xstore(c, in, d, 8);
            return INT_NONE;
        case 0x14:
            if (p > 1) break;
            xload(c, in, &s, 16);
            if (p == 0) {
                d->u32[3] = s.u32[1]; d->u32[2] = d->u32[1];
                d->u32[1] = s.u32[0];
            } else {
                d->qw[1] = s.qw[0];
            }
            return INT_NONE;
        case 0x15:
            if (p > 1) break;
            xload(c, in, &s, 16);
            if (p == 0) {
                d->u32[0] = d->u32[2]; d->u32[1] = s.u32[2];
                d->u32[2] = d->u32[3]; d->u32[3] = s.u32[3];
            } else {
                d->qw[0] = d->qw[1]; d->qw[1] = s.qw[1];
            }
            return INT_NONE;
        case 0x16:
            if (p == 3) break;
            if (p == 2) { // movshdup
                xload(c, in, &s, 16);
                d->u32[0] = d->u32[1] = s.u32[1];
                d->u32[2] = d->u32[3] = s.u32[3];
                return INT_NONE;
            }
            xload(c, in, &s, 8);
            d->qw[1] = s.qw[0];
            return INT_NONE;
        case 0x7c: case 0x7d: { // haddpd/ps, hsubpd/ps
            if (p != 1 && p != 3) break;
            xload(c, in, &s, 16);
            union xmm_reg t = *d;
#define HOP(a, b) (op == 0x7c ? nd((a) + (b), a, b) : nd((a) - (b), a, b))
#define HOPF(a, b) (op == 0x7c ? nf((a) + (b), a, b) : nf((a) - (b), a, b))
            if (p == 1) {
                d->f64[0] = HOP(t.f64[0], t.f64[1]);
                d->f64[1] = HOP(s.f64[0], s.f64[1]);
            } else {
                d->f32[0] = HOPF(t.f32[0], t.f32[1]);
                d->f32[1] = HOPF(t.f32[2], t.f32[3]);
                d->f32[2] = HOPF(s.f32[0], s.f32[1]);
                d->f32[3] = HOPF(s.f32[2], s.f32[3]);
            }
#undef HOP
#undef HOPF
            return INT_NONE;
        }
        case 0xd0: // addsubpd/ps
            if (p != 1 && p != 3) break;
            xload(c, in, &s, 16);
            if (p == 1) {
                d->f64[0] = nd(d->f64[0] - s.f64[0], d->f64[0], s.f64[0]);
                d->f64[1] = nd(d->f64[1] + s.f64[1], d->f64[1], s.f64[1]);
            } else {
                for (int k = 0; k < 4; k++)
                    d->f32[k] = (k & 1) ? nf(d->f32[k] + s.f32[k], d->f32[k], s.f32[k]) : nf(d->f32[k] - s.f32[k], d->f32[k], s.f32[k]);
            }
            return INT_NONE;
        case 0xf0: // lddqu
            if (p != 3 || !mem) break;
            xload(c, in, &s, 16);
            *d = s;
            return INT_NONE;
        case 0x17:
            if (p > 1 || !mem) break;
            s.qw[0] = d->qw[1];
            xstore(c, in, &s, 8);
            return INT_NONE;
        case 0x28: case 0x6f:
            if (op == 0x6f && p != 1 && p != 2) break;
            xload(c, in, &s, 16);
            *d = s;
            return INT_NONE;
        case 0x29: case 0x7f:
            if (op == 0x7f && p != 1 && p != 2) break;
            if (mem) xstore(c, in, d, 16); else cpu->xmm[in->rm] = *d;
            return INT_NONE;
        case 0x2b: case 0xe7:
            if (!mem || (op == 0xe7 && p != 1)) break;
            xstore(c, in, d, 16);
            return INT_NONE;
        case 0x2a: {
            if (p < 2) { // cvtpi2ps / cvtpi2pd: two integers from mm/m64
                union mm_reg m;
                if (mem) m.qw = mrd(c, ea(c, in), 8); else m = cpu->mm[in->rm & 7];
                if (p == 0) {
                    d->f32[0] = (float) (int32_t) m.dw[0]; d->f32[1] = (float) (int32_t) m.dw[1];
                } else {
                    d->f64[0] = (int32_t) m.dw[0]; d->f64[1] = (int32_t) m.dw[1];
                }
                return INT_NONE;
            }
            uint64_t v = eget(c, in, gsz);
            if (p == 2)
                d->f32[0] = gsz == 8 ? (float) (int64_t) v : (float) (int32_t) v;
            else
                d->f64[0] = gsz == 8 ? (double) (int64_t) v : (double) (int32_t) v;
            return INT_NONE;
        }
        case 0x2c: case 0x2d: {
            if (p < 2) { // cvt(t)ps2pi / cvt(t)pd2pi: two integers into mm
                xload(c, in, &s, p == 0 ? 8 : 16);
                union mm_reg *m = &cpu->mm[in->reg & 7];
                for (int k = 0; k < 2; k++)
                    m->dw[k] = (uint32_t) f2i32(p == 0 ? (double) s.f32[k] : s.f64[k], op == 0x2c);
                return INT_NONE;
            }
            xload(c, in, &s, p == 2 ? 4 : 8);
            double v = p == 2 ? (double) s.f32[0] : s.f64[0];
            if (gsz == 8)
                cpu->regs[in->reg] = (uint64_t) f2i64(v, op == 0x2c);
            else
                cpu->regs[in->reg] = (uint32_t) f2i32(v, op == 0x2c);
            return INT_NONE;
        }
        case 0x2e: case 0x2f:
            if (p > 1) break;
            xload(c, in, &s, p == 0 ? 4 : 8);
            if (p == 0) sse_compare(c, d->f32[0], s.f32[0]);
            else sse_compare(c, d->f64[0], s.f64[0]);
            return INT_NONE;
        case 0x50: {
            if (p > 1 || mem) break;
            union xmm_reg *e = &cpu->xmm[in->rm];
            uint32_t m = 0;
            if (p == 0) for (int k = 0; k < 4; k++) m |= (e->u32[k] >> 31) << k;
            else for (int k = 0; k < 2; k++) m |= (uint32_t) (e->qw[k] >> 63) << k;
            cpu->regs[in->reg] = m;
            return INT_NONE;
        }
        case 0x51: case 0x58: case 0x59: case 0x5c: case 0x5d: case 0x5e: case 0x5f: {
            xload(c, in, &s, p == 2 ? 4 : p == 3 ? 8 : 16);
#define FOP(x, y) (op == 0x51 ? nd(__builtin_sqrt(y), y, y) : op == 0x58 ? nd(x + y, x, y) : op == 0x59 ? nd(x * y, x, y) : \
        op == 0x5c ? nd(x - y, x, y) : op == 0x5e ? nd(x / y, x, y) : op == 0x5d ? (x < y ? x : y) : (x > y ? x : y))
#define FOPF(x, y) (op == 0x51 ? nf(__builtin_sqrtf(y), y, y) : op == 0x58 ? nf(x + y, x, y) : op == 0x59 ? nf(x * y, x, y) : \
        op == 0x5c ? nf(x - y, x, y) : op == 0x5e ? nf(x / y, x, y) : op == 0x5d ? (x < y ? x : y) : (x > y ? x : y))
            if (p == 0) LANES(4, f32, FOPF(x, y));
            else if (p == 1) LANES(2, f64, FOP(x, y));
            else if (p == 2) LANES(1, f32, FOPF(x, y));
            else LANES(1, f64, FOP(x, y));
            return INT_NONE;
        }
        case 0x52: case 0x53:
            if (p != 0 && p != 2) break;
            xload(c, in, &s, p == 2 ? 4 : 16);
            LANES(p == 0 ? 4 : 1, f32, op == 0x52 ? nf(1.0f / __builtin_sqrtf(y), y, y) : nf(1.0f / y, y, y));
            return INT_NONE;
        case 0x54: case 0xdb:
            if (op == 0xdb && p != 1) break;
            xload(c, in, &s, 16);
            d->qw[0] &= s.qw[0]; d->qw[1] &= s.qw[1];
            return INT_NONE;
        case 0x55: case 0xdf:
            if (op == 0xdf && p != 1) break;
            xload(c, in, &s, 16);
            d->qw[0] = ~d->qw[0] & s.qw[0]; d->qw[1] = ~d->qw[1] & s.qw[1];
            return INT_NONE;
        case 0x56: case 0xeb:
            if (op == 0xeb && p != 1) break;
            xload(c, in, &s, 16);
            d->qw[0] |= s.qw[0]; d->qw[1] |= s.qw[1];
            return INT_NONE;
        case 0x57: case 0xef:
            if (op == 0xef && p != 1) break;
            xload(c, in, &s, 16);
            d->qw[0] ^= s.qw[0]; d->qw[1] ^= s.qw[1];
            return INT_NONE;
        case 0x5a:
            if (p == 0) {
                xload(c, in, &s, 8);
                d->f64[1] = s.f32[1]; d->f64[0] = s.f32[0];
            } else if (p == 1) {
                xload(c, in, &s, 16);
                d->f32[0] = (float) s.f64[0]; d->f32[1] = (float) s.f64[1];
                d->qw[1] = 0;
            } else if (p == 2) {
                xload(c, in, &s, 4);
                d->f64[0] = s.f32[0];
            } else {
                xload(c, in, &s, 8);
                d->f32[0] = (float) s.f64[0];
            }
            return INT_NONE;
        case 0x5b:
            if (p == 3) break;
            xload(c, in, &s, 16);
            for (int k = 0; k < 4; k++) {
                if (p == 0) d->f32[k] = (float) (int32_t) s.u32[k];
                else d->u32[k] = (uint32_t) f2i32(s.f32[k], p == 2);
            }
            return INT_NONE;
        case 0xe6:
            if (p == 0) break;
            if (p == 2) {
                xload(c, in, &s, 8);
                d->f64[1] = (int32_t) s.u32[1]; d->f64[0] = (int32_t) s.u32[0];
            } else {
                xload(c, in, &s, 16);
                d->u32[0] = (uint32_t) f2i32(s.f64[0], p == 1);
                d->u32[1] = (uint32_t) f2i32(s.f64[1], p == 1);
                d->qw[1] = 0;
            }
            return INT_NONE;
        case 0xc2: {
            xload(c, in, &s, p == 2 ? 4 : p == 3 ? 8 : 16);
            int pred = (int) in->imm;
            if (p == 0) LANES(4, u32, cmp_pred(pred, d->f32[k], s.f32[k]) ? 0xffffffffu : 0);
            else if (p == 1) LANES(2, qw, cmp_pred(pred, d->f64[k], s.f64[k]) ? ~0ULL : 0);
            else if (p == 2) d->u32[0] = cmp_pred(pred, d->f32[0], s.f32[0]) ? 0xffffffffu : 0;
            else d->qw[0] = cmp_pred(pred, d->f64[0], s.f64[0]) ? ~0ULL : 0;
            return INT_NONE;
        }
        case 0xc6: {
            if (p > 1) break;
            xload(c, in, &s, 16);
            unsigned i = (unsigned) in->imm;
            union xmm_reg t = *d;
            if (p == 0) {
                d->u32[0] = t.u32[i & 3]; d->u32[1] = t.u32[(i >> 2) & 3];
                d->u32[2] = s.u32[(i >> 4) & 3]; d->u32[3] = s.u32[(i >> 6) & 3];
            } else {
                d->qw[0] = t.qw[i & 1]; d->qw[1] = s.qw[(i >> 1) & 1];
            }
            return INT_NONE;
        }
        case 0x7e:
            if (p == 1) {
                eset(c, in, gsz, d->qw[0]);
                return INT_NONE;
            }
            if (p != 2) break;
            xload(c, in, &s, 8);
            d->qw[0] = s.qw[0]; d->qw[1] = 0;
            return INT_NONE;
        case 0xc3: // movnti is handled by exec()
            break;
    }

    if (op == 0xd6 && !mem && p >= 2) {
        if (p == 2) { // movq2dq xmm, mm
            d->qw[0] = cpu->mm[in->rm & 7].qw; d->qw[1] = 0;
        } else { // movdq2q mm, xmm
            cpu->mm[in->reg & 7].qw = cpu->xmm[in->rm].qw[0];
        }
        return INT_NONE;
    }
    // Everything below is packed integer: SSE2 with the 66 prefix, MMX without.
    if (p == 0)
        return exec_mmx(c, in);
    if (p != 1 && op != 0x70)
        return undefined_insn(c, in);

    switch (op) {
        case 0x6e:
            d->qw[0] = eget(c, in, gsz); d->qw[1] = 0;
            return INT_NONE;
        case 0xd6:
            if (mem) {
                xstore(c, in, d, 8);
            } else {
                cpu->xmm[in->rm].qw[0] = d->qw[0]; cpu->xmm[in->rm].qw[1] = 0;
            }
            return INT_NONE;
        case 0xd7: {
            if (mem) break;
            union xmm_reg *e = &cpu->xmm[in->rm];
            uint32_t m = 0;
            for (int k = 0; k < 16; k++) m |= (uint32_t) (e->u8[k] >> 7) << k;
            cpu->regs[in->reg] = m;
            return INT_NONE;
        }
        case 0xc4: {
            uint64_t v = in->mod == 3 ? rget(cpu, in, in->rm, 4) : mrd(c, ea(c, in), 2);
            d->u16[in->imm & 7] = (uint16_t) v;
            return INT_NONE;
        }
        case 0xc5:
            if (mem) break;
            cpu->regs[in->reg] = cpu->xmm[in->rm].u16[in->imm & 7];
            return INT_NONE;
        case 0x70: {
            xload(c, in, &s, 16);
            unsigned i = (unsigned) in->imm;
            if (p == 1) {
                for (int k = 0; k < 4; k++) d->u32[k] = s.u32[(i >> (2 * k)) & 3];
            } else if (p == 2) {
                d->qw[0] = s.qw[0];
                for (int k = 0; k < 4; k++) d->u16[4 + k] = s.u16[4 + ((i >> (2 * k)) & 3)];
            } else {
                for (int k = 0; k < 4; k++) d->u16[k] = s.u16[(i >> (2 * k)) & 3];
                d->qw[1] = s.qw[1];
            }
            return INT_NONE;
        }
        case 0x71: case 0x72: case 0x73:
            if (mem) break;
            if (!pshift_imm(op, in->reg & 7, (unsigned) in->imm & 0xff, &cpu->xmm[in->rm], true))
                return undefined_insn(c, in);
            return INT_NONE;
        case 0xf7: { // maskmovdqu
            if (mem) break;
            union xmm_reg *e = &cpu->xmm[in->rm];
            uint64_t addr = cpu->rdi + segbase(c, in);
            if (in->pfx & P_ADDR32) addr = (uint32_t) cpu->rdi + segbase(c, in);
            for (int k = 0; k < 16; k++)
                if (e->u8[k] & 0x80)
                    mwr(c, addr + k, 1, d->u8[k]);
            return INT_NONE;
        }
    }

    xload(c, in, &s, 16);
    union xmm_reg t = *d;
    if (!pint_op(op, d, &t, &s)) {
        *d = t;
        return undefined_insn(c, in);
    }
    return INT_NONE;
}
