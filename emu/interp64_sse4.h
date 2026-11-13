// The three-byte opcode maps 0F 38 and 0F 3A for interp64.c (included there):
// SSSE3, SSE4.1, SSE4.2, movbe and crc32. Decoded as op 0x200|byte and
// 0x300|byte.

static forceinline uint32_t crc32c_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int k = 0; k < 8; k++)
        crc = (crc >> 1) ^ (0x82f63b78u & (0u - (crc & 1)));
    return crc;
}

static double round_mode(double v, int imm) {
    switch (imm & 4 ? 4 : imm & 3) {
        case 1: return floor(v);
        case 2: return ceil(v);
        case 3: return trunc(v);
        case 4: return nearbyint(v); // the mode in MXCSR, which is the host's mode
        default: {
            // to nearest even whatever the current mode is
            int old = fegetround();
            if (old == FE_TONEAREST)
                return nearbyint(v);
            fesetround(FE_TONEAREST);
            double r = nearbyint(v);
            fesetround(old);
            return r;
        }
    }
}
static float round_modef(float v, int imm) {
    switch (imm & 4 ? 4 : imm & 3) {
        case 1: return floorf(v);
        case 2: return ceilf(v);
        case 3: return truncf(v);
        case 4: return nearbyintf(v);
        default: {
            int old = fegetround();
            if (old == FE_TONEAREST)
                return nearbyintf(v);
            fesetround(FE_TONEAREST);
            float r = nearbyintf(v);
            fesetround(old);
            return r;
        }
    }
}

// SSSE3 operations on 128 bits (MMX forms use the low half; see the callers)
static bool ssse3_op(int op, union xmm_reg *d, const union xmm_reg *tp, const union xmm_reg *sp, bool mmx) {
    const union xmm_reg t = *tp, s = *sp;
    *d = t;
    int half = mmx ? 2 : 4; // word pairs per source in the horizontal operations
    switch (op) {
        case 0x00: // pshufb
            for (int k = 0; k < (mmx ? 8 : 16); k++)
                d->u8[k] = (s.u8[k] & 0x80) ? 0 : t.u8[s.u8[k] & (mmx ? 7 : 15)];
            break;
        case 0x01: case 0x03: case 0x05: case 0x07: // phaddw, phaddsw, phsubw, phsubsw
            for (int k = 0; k < half; k++) {
                int32_t a0 = (int16_t) t.u16[2 * k], a1 = (int16_t) t.u16[2 * k + 1];
                int32_t b0 = (int16_t) s.u16[2 * k], b1 = (int16_t) s.u16[2 * k + 1];
                int32_t ra = op < 0x04 ? a0 + a1 : a0 - a1, rb = op < 0x04 ? b0 + b1 : b0 - b1;
                bool sat = op == 0x03 || op == 0x07;
                d->u16[k] = sat ? (uint16_t) sat16(ra) : (uint16_t) ra;
                d->u16[half + k] = sat ? (uint16_t) sat16(rb) : (uint16_t) rb;
            }
            break;
        case 0x02: case 0x06: // phaddd, phsubd
            for (int k = 0; k < half / 2; k++) {
                d->u32[k] = op == 0x02 ? t.u32[2 * k] + t.u32[2 * k + 1] : t.u32[2 * k] - t.u32[2 * k + 1];
                d->u32[half / 2 + k] = op == 0x02 ? s.u32[2 * k] + s.u32[2 * k + 1] : s.u32[2 * k] - s.u32[2 * k + 1];
            }
            break;
        case 0x04: // pmaddubsw
            for (int k = 0; k < 8; k++)
                d->u16[k] = (uint16_t) sat16((int32_t) t.u8[2 * k] * (int8_t) s.u8[2 * k] + (int32_t) t.u8[2 * k + 1] * (int8_t) s.u8[2 * k + 1]);
            break;
        case 0x08: LANES_S(16, u8, int8_t, y < 0 ? -x : y == 0 ? 0 : x); break;
        case 0x09: LANES_S(8, u16, int16_t, y < 0 ? -x : y == 0 ? 0 : x); break;
        case 0x0a: LANES_S(4, u32, int32_t, y < 0 ? (int32_t) (0u - (uint32_t) x) : y == 0 ? 0 : x); break;
        case 0x0b: LANES_S(8, u16, int16_t, (((int32_t) x * y >> 14) + 1) >> 1); break;
        case 0x1c: for (int k = 0; k < 16; k++) d->u8[k] = (uint8_t) abs((int8_t) s.u8[k]); break;
        case 0x1d: for (int k = 0; k < 8; k++) d->u16[k] = (uint16_t) abs((int16_t) s.u16[k]); break;
        case 0x1e: for (int k = 0; k < 4; k++) d->u32[k] = (int32_t) s.u32[k] < 0 ? 0u - s.u32[k] : s.u32[k]; break;
        default:
            return false;
    }
    return true;
}

// pcmpestri/pcmpestrm/pcmpistri/pcmpistrm. Returns the result mask IntRes2
// and sets the flags.
static uint32_t pcmpxstrx(struct ictx *c, const union xmm_reg *a, const union xmm_reg *b, int la, int lb, int imm) {
    bool words = imm & 1, sgn = imm & 2;
    int n = words ? 8 : 16;
    if (la > n) la = n;
    if (lb > n) lb = n;
#define EL(v, i) (words ? (sgn ? (int) (int16_t) (v)->u16[i] : (int) (v)->u16[i]) : (sgn ? (int) (int8_t) (v)->u8[i] : (int) (v)->u8[i]))
    uint32_t res = 0;
    switch ((imm >> 2) & 3) {
        case 0: // equal any: is b[j] one of the characters in a
            for (int j = 0; j < lb; j++)
                for (int i = 0; i < la; i++)
                    if (EL(a, i) == EL(b, j)) { res |= 1u << j; break; }
            break;
        case 1: // ranges: a holds pairs of bounds
            for (int j = 0; j < lb; j++)
                for (int i = 0; i + 1 < la; i += 2)
                    if (EL(b, j) >= EL(a, i) && EL(b, j) <= EL(a, i + 1)) { res |= 1u << j; break; }
            break;
        case 2: // equal each: string compare
            for (int j = 0; j < n; j++) {
                bool av = j < la, bv = j < lb;
                if (av && bv ? EL(a, j) == EL(b, j) : !av && !bv)
                    res |= 1u << j;
            }
            break;
        default: // equal ordered: substring search of a in b
            for (int j = 0; j < n; j++) {
                bool match = true;
                for (int i = 0; i < la && match; i++) {
                    int k = j + i;
                    if (k >= lb) {
                        if (k < n) match = false; // b ended inside the vector: a cannot continue
                        break; // past the vector: a partial match counts
                    }
                    if (EL(a, i) != EL(b, k)) match = false;
                }
                if (match)
                    res |= 1u << j;
            }
            break;
    }
#undef EL
    uint32_t all = n == 16 ? 0xffffu : 0xffu;
    switch ((imm >> 4) & 3) {
        case 1: res ^= all; break;
        case 3: res ^= (lb >= n ? all : (1u << lb) - 1); break;
    }
    res &= all;
    c->cf = res != 0;
    c->zr = lb < n ? 0 : 1; // ZF: b has an end
    c->sn = la < n ? -1 : 0; // SF: a has an end
    c->of = res & 1;
    c->aux = 0;
    c->pr = 1; // PF = 0
    return res;
}
static int implicit_len(const union xmm_reg *v, bool words) {
    int n = words ? 8 : 16;
    for (int i = 0; i < n; i++)
        if ((words ? v->u16[i] : v->u8[i]) == 0)
            return i;
    return n;
}

static int exec_0f38(struct ictx *c, const struct insn *in) {
    struct cpu_state *cpu = c->cpu;
    int op = in->op & 0xff;
    bool mem = in->mod != 3;
    int p = (in->pfx & P_F3) ? 2 : (in->pfx & P_F2) ? 3 : (in->pfx & P_66) ? 1 : 0;
    union xmm_reg *d = &cpu->xmm[in->reg];
    union xmm_reg s, t;

    if (op == 0xf0 || op == 0xf1) {
        int sz = op == 0xf0 ? 1 : (in->pfx & P_66) && in->opsz != 8 ? 2 : in->opsz;
        if (in->pfx & P_F2) { // crc32 r, r/m
            uint64_t v = eget(c, in, sz);
            uint32_t crc = (uint32_t) cpu->regs[in->reg];
            for (int k = 0; k < sz; k++)
                crc = crc32c_byte(crc, (uint8_t) (v >> (8 * k)));
            cpu->regs[in->reg] = crc;
            return INT_NONE;
        }
        if (!mem || (in->pfx & P_F3))
            return undefined_insn(c, in);
        // movbe
        sz = in->opsz;
        if (op == 0xf0) {
            uint64_t v = mrd(c, ea(c, in), sz);
            v = sz == 8 ? __builtin_bswap64(v) : sz == 4 ? __builtin_bswap32((uint32_t) v) : __builtin_bswap16((uint16_t) v);
            rset(cpu, in, in->reg, sz, v);
        } else {
            uint64_t v = rget(cpu, in, in->reg, sz);
            v = sz == 8 ? __builtin_bswap64(v) : sz == 4 ? __builtin_bswap32((uint32_t) v) : __builtin_bswap16((uint16_t) v);
            mwr(c, ea(c, in), sz, v);
        }
        return INT_NONE;
    }

    if (op <= 0x0b || (op >= 0x1c && op <= 0x1e)) {
        if (p == 0) { // MMX form
            union mm_reg *md = &cpu->mm[in->reg & 7];
            union xmm_reg r;
            t.qw[0] = md->qw; t.qw[1] = 0;
            s.qw[0] = mem ? mrd(c, ea(c, in), 8) : cpu->mm[in->rm & 7].qw; s.qw[1] = 0;
            if (!ssse3_op(op, &r, &t, &s, true))
                return undefined_insn(c, in);
            md->qw = r.qw[0];
            return INT_NONE;
        }
        if (p != 1)
            return undefined_insn(c, in);
        xload(c, in, &s, 16);
        t = *d;
        if (!ssse3_op(op, d, &t, &s, false)) {
            *d = t;
            return undefined_insn(c, in);
        }
        return INT_NONE;
    }
    if (p != 1)
        return undefined_insn(c, in);

    switch (op) {
        case 0x10: // pblendvb: mask is xmm0
            xload(c, in, &s, 16);
            for (int k = 0; k < 16; k++) if (cpu->xmm[0].u8[k] & 0x80) d->u8[k] = s.u8[k];
            return INT_NONE;
        case 0x14:
            xload(c, in, &s, 16);
            for (int k = 0; k < 4; k++) if (cpu->xmm[0].u32[k] >> 31) d->u32[k] = s.u32[k];
            return INT_NONE;
        case 0x15:
            xload(c, in, &s, 16);
            for (int k = 0; k < 2; k++) if (cpu->xmm[0].qw[k] >> 63) d->qw[k] = s.qw[k];
            return INT_NONE;
        case 0x17: // ptest
            xload(c, in, &s, 16);
            c->zr = ((s.qw[0] & d->qw[0]) | (s.qw[1] & d->qw[1])) != 0;
            c->cf = ((s.qw[0] & ~d->qw[0]) | (s.qw[1] & ~d->qw[1])) == 0;
            c->of = 0; c->sn = 0; c->aux = 0; c->pr = 1;
            return INT_NONE;
        case 0x20: xload(c, in, &s, 8); for (int k = 7; k >= 0; k--) d->u16[k] = (uint16_t) (int8_t) s.u8[k]; return INT_NONE;
        case 0x21: xload(c, in, &s, 4); for (int k = 3; k >= 0; k--) d->u32[k] = (uint32_t) (int8_t) s.u8[k]; return INT_NONE;
        case 0x22: xload(c, in, &s, 2); for (int k = 1; k >= 0; k--) d->qw[k] = (uint64_t) (int8_t) s.u8[k]; return INT_NONE;
        case 0x23: xload(c, in, &s, 8); for (int k = 3; k >= 0; k--) d->u32[k] = (uint32_t) (int16_t) s.u16[k]; return INT_NONE;
        case 0x24: xload(c, in, &s, 4); for (int k = 1; k >= 0; k--) d->qw[k] = (uint64_t) (int16_t) s.u16[k]; return INT_NONE;
        case 0x25: xload(c, in, &s, 8); for (int k = 1; k >= 0; k--) d->qw[k] = (uint64_t) (int32_t) s.u32[k]; return INT_NONE;
        case 0x30: xload(c, in, &s, 8); for (int k = 7; k >= 0; k--) d->u16[k] = s.u8[k]; return INT_NONE;
        case 0x31: xload(c, in, &s, 4); for (int k = 3; k >= 0; k--) d->u32[k] = s.u8[k]; return INT_NONE;
        case 0x32: xload(c, in, &s, 2); for (int k = 1; k >= 0; k--) d->qw[k] = s.u8[k]; return INT_NONE;
        case 0x33: xload(c, in, &s, 8); for (int k = 3; k >= 0; k--) d->u32[k] = s.u16[k]; return INT_NONE;
        case 0x34: xload(c, in, &s, 4); for (int k = 1; k >= 0; k--) d->qw[k] = s.u16[k]; return INT_NONE;
        case 0x35: xload(c, in, &s, 8); for (int k = 1; k >= 0; k--) d->qw[k] = s.u32[k]; return INT_NONE;
        case 0x2a: // movntdqa
            if (!mem) break;
            xload(c, in, &s, 16);
            *d = s;
            return INT_NONE;
    }
    xload(c, in, &s, 16);
    t = *d;
    switch (op) {
        case 0x28: for (int k = 0; k < 2; k++) d->qw[k] = (uint64_t) ((int64_t) (int32_t) t.u32[2 * k] * (int32_t) s.u32[2 * k]); break;
        case 0x29: LANES(2, qw, x == y ? ~0ULL : 0); break;
        case 0x2b: for (int k = 0; k < 4; k++) { d->u16[k] = satu16((int32_t) t.u32[k]); d->u16[4 + k] = satu16((int32_t) s.u32[k]); } break;
        case 0x37: LANES_S(2, qw, int64_t, x > y ? ~0ULL : 0); break;
        case 0x38: LANES_S(16, u8, int8_t, x < y ? x : y); break;
        case 0x39: LANES_S(4, u32, int32_t, x < y ? x : y); break;
        case 0x3a: LANES(8, u16, x < y ? x : y); break;
        case 0x3b: LANES(4, u32, x < y ? x : y); break;
        case 0x3c: LANES_S(16, u8, int8_t, x > y ? x : y); break;
        case 0x3d: LANES_S(4, u32, int32_t, x > y ? x : y); break;
        case 0x3e: LANES(8, u16, x > y ? x : y); break;
        case 0x3f: LANES(4, u32, x > y ? x : y); break;
        case 0x40: LANES(4, u32, x * y); break;
        case 0x41: { // phminposuw
            int best = 0;
            for (int k = 1; k < 8; k++) if (s.u16[k] < s.u16[best]) best = k;
            d->qw[0] = (uint64_t) s.u16[best] | ((uint64_t) best << 16);
            d->qw[1] = 0;
            break;
        }
        default:
            return undefined_insn(c, in);
    }
    return INT_NONE;
}

static int exec_0f3a(struct ictx *c, const struct insn *in) {
    struct cpu_state *cpu = c->cpu;
    int op = in->op & 0xff;
    bool mem = in->mod != 3;
    int p = (in->pfx & P_F3) ? 2 : (in->pfx & P_F2) ? 3 : (in->pfx & P_66) ? 1 : 0;
    union xmm_reg *d = &cpu->xmm[in->reg];
    union xmm_reg s, t;
    int imm = (int) in->imm & 0xff;

    if (op == 0x0f) { // palignr
        if (p == 0) {
            union mm_reg *md = &cpu->mm[in->reg & 7];
            uint64_t lo = mem ? mrd(c, ea(c, in), 8) : cpu->mm[in->rm & 7].qw, hi = md->qw;
            unsigned sh = (unsigned) imm * 8;
            md->qw = sh == 0 ? lo : sh < 64 ? (lo >> sh) | (hi << (64 - sh)) : sh < 128 ? hi >> (sh - 64) : 0;
            return INT_NONE;
        }
        if (p != 1)
            return undefined_insn(c, in);
        xload(c, in, &s, 16);
        t = *d;
        for (int k = 0; k < 16; k++) {
            int idx = k + imm;
            d->u8[k] = idx < 16 ? s.u8[idx] : idx < 32 ? t.u8[idx - 16] : 0;
        }
        return INT_NONE;
    }
    if (p != 1)
        return undefined_insn(c, in);

    switch (op) {
        case 0x08: xload(c, in, &s, 16); for (int k = 0; k < 4; k++) d->f32[k] = round_modef(s.f32[k], imm); return INT_NONE;
        case 0x09: xload(c, in, &s, 16); for (int k = 0; k < 2; k++) d->f64[k] = round_mode(s.f64[k], imm); return INT_NONE;
        case 0x0a: xload(c, in, &s, 4); d->f32[0] = round_modef(s.f32[0], imm); return INT_NONE;
        case 0x0b: xload(c, in, &s, 8); d->f64[0] = round_mode(s.f64[0], imm); return INT_NONE;
        case 0x0c: xload(c, in, &s, 16); for (int k = 0; k < 4; k++) if (imm & (1 << k)) d->u32[k] = s.u32[k]; return INT_NONE;
        case 0x0d: xload(c, in, &s, 16); for (int k = 0; k < 2; k++) if (imm & (1 << k)) d->qw[k] = s.qw[k]; return INT_NONE;
        case 0x0e: xload(c, in, &s, 16); for (int k = 0; k < 8; k++) if (imm & (1 << k)) d->u16[k] = s.u16[k]; return INT_NONE;
        case 0x14: // pextrb r/m8, xmm
            if (mem) mwr(c, ea(c, in), 1, d->u8[imm & 15]); else cpu->regs[in->rm] = d->u8[imm & 15];
            return INT_NONE;
        case 0x15:
            if (mem) mwr(c, ea(c, in), 2, d->u16[imm & 7]); else cpu->regs[in->rm] = d->u16[imm & 7];
            return INT_NONE;
        case 0x16:
            if (in->opsz == 8) {
                if (mem) mwr(c, ea(c, in), 8, d->qw[imm & 1]); else cpu->regs[in->rm] = d->qw[imm & 1];
            } else {
                if (mem) mwr(c, ea(c, in), 4, d->u32[imm & 3]); else cpu->regs[in->rm] = d->u32[imm & 3];
            }
            return INT_NONE;
        case 0x17: // extractps
            if (mem) mwr(c, ea(c, in), 4, d->u32[imm & 3]); else cpu->regs[in->rm] = d->u32[imm & 3];
            return INT_NONE;
        case 0x20: {
            uint64_t v = mem ? mrd(c, ea(c, in), 1) : cpu->regs[in->rm];
            d->u8[imm & 15] = (uint8_t) v;
            return INT_NONE;
        }
        case 0x21: { // insertps
            uint32_t v;
            if (mem) v = (uint32_t) mrd(c, ea(c, in), 4);
            else v = cpu->xmm[in->rm].u32[(imm >> 6) & 3];
            d->u32[(imm >> 4) & 3] = v;
            for (int k = 0; k < 4; k++) if (imm & (1 << k)) d->u32[k] = 0;
            return INT_NONE;
        }
        case 0x22:
            if (in->opsz == 8) {
                uint64_t v = eget(c, in, 8);
                d->qw[imm & 1] = v;
            } else {
                uint64_t v = eget(c, in, 4);
                d->u32[imm & 3] = (uint32_t) v;
            }
            return INT_NONE;
        case 0x40: { // dpps
            xload(c, in, &s, 16);
            float sum, prod[4], lo, hi;
            for (int k = 0; k < 4; k++) prod[k] = (imm & (0x10 << k)) ? nf(d->f32[k] * s.f32[k], d->f32[k], s.f32[k]) : 0;
            lo = nf(prod[0] + prod[1], prod[0], prod[1]);
            hi = nf(prod[2] + prod[3], prod[2], prod[3]);
            sum = nf(lo + hi, lo, hi);
            for (int k = 0; k < 4; k++) d->f32[k] = (imm & (1 << k)) ? sum : 0;
            return INT_NONE;
        }
        case 0x41: { // dppd
            xload(c, in, &s, 16);
            double p0 = (imm & 0x10) ? nd(d->f64[0] * s.f64[0], d->f64[0], s.f64[0]) : 0;
            double p1 = (imm & 0x20) ? nd(d->f64[1] * s.f64[1], d->f64[1], s.f64[1]) : 0;
            double sum = nd(p0 + p1, p0, p1);
            d->f64[0] = (imm & 1) ? sum : 0;
            d->f64[1] = (imm & 2) ? sum : 0;
            return INT_NONE;
        }
        case 0x42: { // mpsadbw
            xload(c, in, &s, 16);
            t = *d;
            int soff = (imm & 3) * 4, doff = (imm & 4) ? 4 : 0;
            for (int k = 0; k < 8; k++) {
                unsigned sum = 0;
                for (int j = 0; j < 4; j++) sum += (unsigned) abs((int) t.u8[doff + k + j] - (int) s.u8[soff + j]);
                d->u16[k] = (uint16_t) sum;
            }
            return INT_NONE;
        }
        case 0x60: case 0x61: case 0x62: case 0x63: {
            xload(c, in, &s, 16);
            bool words = imm & 1, explicit_len = op < 0x62;
            int n = words ? 8 : 16;
            int la, lb;
            if (explicit_len) {
                int64_t ra = in->opsz == 8 ? (int64_t) cpu->rax : (int32_t) cpu->eax;
                int64_t rd = in->opsz == 8 ? (int64_t) cpu->rdx : (int32_t) cpu->edx;
                if (ra < 0) ra = -ra;
                if (rd < 0) rd = -rd;
                la = ra > n ? n : (int) ra;
                lb = rd > n ? n : (int) rd;
            } else {
                la = implicit_len(d, words);
                lb = implicit_len(&s, words);
            }
            uint32_t res = pcmpxstrx(c, d, &s, la, lb, imm);
            if (op == 0x61 || op == 0x63) { // index into ecx
                uint32_t idx;
                if (res == 0) idx = (uint32_t) n;
                else if (imm & 0x40) idx = 31 - (uint32_t) __builtin_clz(res);
                else idx = (uint32_t) __builtin_ctz(res);
                cpu->rcx = idx;
            } else { // mask into xmm0
                union xmm_reg *m = &cpu->xmm[0];
                if (imm & 0x40) {
                    for (int k = 0; k < n; k++) {
                        if (words) m->u16[k] = (res >> k) & 1 ? 0xffff : 0;
                        else m->u8[k] = (res >> k) & 1 ? 0xff : 0;
                    }
                } else {
                    m->qw[0] = res; m->qw[1] = 0;
                }
            }
            return INT_NONE;
        }
    }
    return undefined_insn(c, in);
}
