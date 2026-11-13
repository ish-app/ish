// Fuzz ish's 80-bit soft-float (emu/float80.c) against this machine's real x87.
// build: clang -O2 -I ~/src/ish64 -I ~/src/ish64/emu f80fuzz.c ~/src/ish64/emu/float80.c -lm -o f80fuzz
// run:   ./f80fuzz [millions of cases per operation and rounding mode]
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include "float80.h"

static uint64_t st[2] = {0x243f6a8885a308d3ull, 0x13198a2e03707344ull};
static uint64_t rnd(void) { uint64_t a = st[0], b = st[1]; st[0] = b; a ^= a << 23; st[1] = a ^ b ^ (a >> 17) ^ (b >> 26); return st[1] + b; }

union u { float80 f; long double ld; unsigned char raw[16]; };
static long double LD(float80 f) { union u u; memset(&u, 0, sizeof u); u.f = f; return u.ld; }
static float80 F8(long double ld) { union u u; memset(&u, 0, sizeof u); u.ld = ld; return u.f; }
static int same(float80 a, float80 b) { return a.signif == b.signif && a.signExp == b.signExp; }

static uint64_t rsig(void) {
    uint64_t s = rnd();
    switch (rnd() % 8) {
        case 0: s = 0; break;                            // power of two
        case 1: s = ~0ull; break;                        // all ones
        case 2: s &= 0xffff; break;                      // few low bits
        case 3: s &= ~0ull << (rnd() % 64); break;       // trailing zeros
        case 4: s = (1ull << (rnd() % 63)); break;       // one extra bit
        case 5: s |= ~0ull >> (rnd() % 64); break;       // trailing ones
    }
    return s | 0x8000000000000000ull;
}
// base exponent for a pair, so that related operands are common
static float80 rnum(int base) {
    float80 f;
    uint64_t r = rnd();
    f.sign = r & 1;
    switch ((r >> 1) % 24) {
        case 0: f.exp = 0; f.signif = 0; return f;                                    // zero
        case 1: f.exp = 0x7fff; f.signif = 0x8000000000000000ull; return f;           // infinity
        case 2: f.exp = 0x7fff; f.signif = 0xc000000000000000ull | (rnd() >> 3); return f; // quiet NaN
        case 3: f.exp = 0; f.signif = rnd() >> (1 + rnd() % 63); return f;            // denormal
        case 4: f.exp = 1 + rnd() % 70; break;                                        // near the bottom
        case 5: f.exp = 0x7ffe - rnd() % 70; break;                                   // near the top
        case 6: f.exp = 0x3fff + (int) (rnd() % 5) - 2; break;
        default: {
            static const int d[] = {0, 0, 1, -1, 2, 31, 32, 33, 62, 63, 64, 65, 66, 126, 127, 128, 129, 130, 200};
            int delta = d[rnd() % (sizeof d / sizeof d[0])];
            int e = base + ((rnd() & 1) ? delta : -delta);
            if (e < 1) e = 1;
            if (e > 0x7ffe) e = 0x7ffe;
            f.exp = e;
        }
    }
    f.signif = rsig();
    return f;
}

static const int modes[4] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
static const enum f80_rounding_mode fmodes[4] = {round_to_nearest, round_down, round_up, round_chop};
static const char *const mode_names[4] = {"nearest", "down", "up", "chop"};
static long fails[16], shown[16];
static void report(int op, const char *name, int mode, float80 a, float80 b, float80 got, float80 want) {
    fails[op]++;
    if (shown[op]++ < 4)
        printf("  %s (%s): %04x %016llx , %04x %016llx -> got %04x %016llx want %04x %016llx\n", name, mode_names[mode],
            a.signExp, (unsigned long long) a.signif, b.signExp, (unsigned long long) b.signif,
            got.signExp, (unsigned long long) got.signif, want.signExp, (unsigned long long) want.signif);
}

int main(int argc, char **argv) {
    long n = (argc > 1 ? atol(argv[1]) : 2) * 1000000L;
    static const char *const names[] = {"add", "sub", "mul", "div", "sqrt", "mod", "to_double", "from_double", "to_int", "from_int", "lt", "eq", "to_float", "round"};
    for (int m = 0; m < 4; m++) {
        fesetround(modes[m]);
        f80_rounding_mode = fmodes[m];
        for (long i = 0; i < n; i++) {
            int base = 0x3fff + (int) (rnd() % 2000) - 1000;
            if (rnd() % 16 == 0) base = 1 + (int) (rnd() % 0x7ffd);
            float80 a = rnum(base), b = rnum(base);
            volatile long double la = LD(a), lb = LD(b);
            volatile long double r;
            float80 g;
            r = la + lb; g = f80_add(a, b); if (!same(g, F8(r))) report(0, names[0], m, a, b, g, F8(r));
            r = la - lb; g = f80_sub(a, b); if (!same(g, F8(r))) report(1, names[1], m, a, b, g, F8(r));
            r = la * lb; g = f80_mul(a, b); if (!same(g, F8(r))) report(2, names[2], m, a, b, g, F8(r));
            r = la / lb; g = f80_div(a, b); if (!same(g, F8(r))) report(3, names[3], m, a, b, g, F8(r));
            r = sqrtl(la); g = f80_sqrt(a); if (!same(g, F8(r))) report(4, names[4], m, a, b, g, F8(r));
            if (!isnan(la) && !isnan(lb) && !isinf(la) && lb != 0) {
                // fprem result when the reduction completes in one step
                // (exponents of the normalized values: a denormal's is lower than its field says)
                int ea = (a.exp ? a.exp : 1) - (a.signif ? __builtin_clzl(a.signif) : 0), eb = (b.exp ? b.exp : 1) - __builtin_clzl(b.signif);
                if (ea - eb < 63) {
                    volatile long double x = la, y = lb, res;
                    __asm__ volatile("fldt %1\n fldt %0\n fprem\n fstp %%st(1)\n fstpt %2" : : "m"(x), "m"(y), "m"(res));
                    g = f80_mod(a, b); if (!same(g, F8(res))) report(5, names[5], m, a, b, g, F8(res));
                }
            }
            volatile double dd = (double) la; double gd = f80_to_double(a);
            if (memcmp((void *) &dd, &gd, 8) != 0 && !(isnan(dd) && isnan(gd))) { fails[6]++; if (shown[6]++ < 4) printf("  to_double (%s): %04x %016llx -> got %a want %a\n", mode_names[m], a.signExp, (unsigned long long) a.signif, gd, dd); }
            uint64_t bits = rnd(); double src; memcpy(&src, &bits, 8); if (rnd() & 1) src = (double) la;
            if (!isnan(src)) { volatile long double w = src; g = f80_from_double(src); if (!same(g, F8(w))) report(7, names[7], m, a, b, g, F8(w)); }
            if (!isnan(la) && fabsl(la) < 9.2e18L) { int64_t wi = llrintl(la), gi = f80_to_int(a); if (wi != gi) { fails[8]++; if (shown[8]++ < 4) printf("  to_int (%s): %04x %016llx -> got %lld want %lld\n", mode_names[m], a.signExp, (unsigned long long) a.signif, (long long) gi, (long long) wi); } }
            int64_t iv = (int64_t) rnd() >> (rnd() % 64); { volatile long double w = (long double) iv; g = f80_from_int(iv); if (!same(g, F8(w))) report(9, names[9], m, a, b, g, F8(w)); }
            { volatile float wf = (float) la; float gf = f80_to_float(a);
              if (memcmp((void *) &wf, &gf, 4) != 0 && !(isnan(wf) && isnan(gf))) { fails[12]++; if (shown[12]++ < 4) printf("  to_float (%s): %04x %016llx -> got %a want %a\n", mode_names[m], a.signExp, (unsigned long long) a.signif, gf, wf); } }
            { volatile long double w = rintl(la); g = f80_round(a); if (!same(g, F8(w)) && !isnan(la)) report(13, "round", m, a, b, g, F8(w)); }
            if (f80_lt(a, b) != (la < lb)) report(10, names[10], m, a, b, a, b);
            if (f80_eq(a, b) != (la == lb)) report(11, names[11], m, a, b, a, b);
        }
    }
    long total = 0;
    for (int i = 0; i < 14; i++) { printf("%-12s %ld failures of %ld\n", names[i], fails[i], n * 4); total += fails[i]; }
    return total != 0;
}
