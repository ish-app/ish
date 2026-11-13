// NaN / infinity / zero / denormal operand combinations through SSE and x87.
// Output must be identical natively and in ish on any host.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <immintrin.h>
static double D(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static float F(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint64_t UD(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static uint32_t UF(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
int main(void) {
    const uint64_t dv[] = {0x7ff8000000000123ull, 0xfff8000000000456ull, 0x7ff0000000000789ull /* sNaN */, 0x7ff0000000000000ull, 0xfff0000000000000ull,
        0, 0x8000000000000000ull, 0x3ff0000000000000ull, 0xc000000000000000ull, 0x0000000000000001ull, 0x7fefffffffffffffull, 0x0010000000000000ull};
    const uint32_t fv[] = {0x7fc00123, 0xffc00456, 0x7f800789 /* sNaN */, 0x7f800000, 0xff800000, 0, 0x80000000, 0x3f800000, 0xc0000000, 0x00000001, 0x7f7fffff, 0x00800000};
    int n = sizeof dv / sizeof dv[0];
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
        volatile double a = D(dv[i]), b = D(dv[j]);
        volatile float fa = F(fv[i]), fb = F(fv[j]);
        printf("d %d %d: %016llx %016llx %016llx %016llx %016llx %016llx %016llx | %d%d%d%d\n", i, j,
            (unsigned long long) UD(a + b), (unsigned long long) UD(a - b), (unsigned long long) UD(a * b), (unsigned long long) UD(a / b),
            (unsigned long long) UD(sqrt(a)), (unsigned long long) UD(_mm_cvtsd_f64(_mm_min_sd(_mm_set_sd(a), _mm_set_sd(b)))),
            (unsigned long long) UD(_mm_cvtsd_f64(_mm_max_sd(_mm_set_sd(a), _mm_set_sd(b)))), a < b, a == b, a != b, a >= b);
        printf("f %d %d: %08x %08x %08x %08x %08x %08x %08x | %d%d%d%d\n", i, j, UF(fa + fb), UF(fa - fb), UF(fa * fb), UF(fa / fb), UF(sqrtf(fa)),
            UF(_mm_cvtss_f32(_mm_min_ss(_mm_set_ss(fa), _mm_set_ss(fb)))), UF(_mm_cvtss_f32(_mm_max_ss(_mm_set_ss(fa), _mm_set_ss(fb)))), fa < fb, fa == fb, fa != fb, fa >= fb);
        __m128d pd = _mm_hadd_pd(_mm_set_pd(a, b), _mm_set_pd(b, a)); __m128 ps = _mm_dp_ps(_mm_set_ps(fa, fb, fa, 1), _mm_set_ps(fb, fa, 2, fa), 0xff);
        uint64_t q[2]; uint32_t w[4]; memcpy(q, &pd, 16); memcpy(w, &ps, 16);
        __m128i cm = _mm_castpd_si128(_mm_cmpunord_pd(_mm_set_pd(a, b), _mm_set_pd(b, 1.0))); uint64_t cq[2]; memcpy(cq, &cm, 16);
        printf("p %d %d: %016llx %016llx %08x %016llx %016llx\n", i, j, (unsigned long long) q[0], (unsigned long long) q[1], w[0], (unsigned long long) cq[0], (unsigned long long) cq[1]);
        volatile long double la = a, lb = b; volatile long double lr = la + lb, lm = la * lb, lq = la / lb, ls = sqrtl(la);
        uint64_t l[8] = {0}; memcpy(&l[0], (void *) &lr, 10); memcpy(&l[2], (void *) &lm, 10); memcpy(&l[4], (void *) &lq, 10); memcpy(&l[6], (void *) &ls, 10);
        printf("x %d %d: %04llx%016llx %04llx%016llx %04llx%016llx %04llx%016llx | %d%d\n", i, j, (unsigned long long) l[1], (unsigned long long) l[0], (unsigned long long) l[3], (unsigned long long) l[2],
            (unsigned long long) l[5], (unsigned long long) l[4], (unsigned long long) l[7], (unsigned long long) l[6], la < lb, la == lb);
    }
    for (int i = 0; i < n; i++) {
        volatile double a = D(dv[i]); volatile float fa = F(fv[i]);
        printf("c %d: %08x %016llx %lld %d %lld %d %016llx %08x %g %g %Lg\n", i, UF((float) a), (unsigned long long) UD((double) fa), (long long) a, (int) fa,
            (long long) llrint(a), (int) lrintf(fa), (unsigned long long) UD(_mm_cvtsd_f64(_mm_round_sd(_mm_set_sd(1), _mm_set_sd(a), 0))), UF(_mm_cvtss_f32(_mm_round_ss(_mm_set_ss(1), _mm_set_ss(fa), 1))),
            a, (double) fa, (long double) a);
    }
    volatile double zero = 0, inf = INFINITY;
    printf("made: %g %g %g %g %g\n", zero / zero, inf - inf, inf * zero, sqrt(-1.0 + zero), (double) (0.0f * (float) inf));
    return 0;
}
