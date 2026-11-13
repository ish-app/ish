// Deterministic CPU test: prints checksums of many operations. The same static
// binary is run natively on the host and inside ish; outputs must match.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <setjmp.h>
#include <pthread.h>
#include <stdatomic.h>

static uint64_t s[2] = {0x243f6a8885a308d3ull, 0x13198a2e03707344ull};
static uint64_t rnd(void) {
    uint64_t a = s[0], b = s[1];
    s[0] = b; a ^= a << 23; s[1] = a ^ b ^ (a >> 17) ^ (b >> 26);
    return s[1] + b;
}
static uint64_t h;
static void mix(uint64_t v) { h = (h ^ v) * 0x100000001b3ull; h ^= h >> 29; }
static void mixd(double d) { uint64_t v; memcpy(&v, &d, 8); mix(v); }
static void mixld(long double d) { uint64_t v[2] = {0, 0}; memcpy(v, &d, 10); mix(v[0]); mix(v[1]); }
#define REPORT(name) do { printf("%-12s %016llx\n", name, (unsigned long long) h); h = 0; } while (0)

__attribute__((noinline)) static void ints(void) {
    for (int i = 0; i < 200000; i++) {
        uint64_t a = rnd(), b = rnd();
        unsigned c = b & 63;
        mix(a + b); mix(a - b); mix(a * b); mix(a ^ b); mix(a & b); mix(a | b);
        mix(a << c); mix(a >> c); mix((int64_t) a >> c);
        mix((a << c) | (a >> ((64 - c) & 63)));
        mix((uint32_t) a << (c & 31)); mix((uint32_t) a >> (c & 31)); mix((uint64_t) ((int32_t) a >> (c & 31)));
        mix((uint16_t) (a * 3)); mix((uint8_t) (a + b)); mix((int8_t) a * (int8_t) b); mix((int16_t) a * (int16_t) b);
        if (b) { mix(a / b); mix(a % b); mix((uint32_t) a / ((uint32_t) b | 1)); mix((int32_t) a % ((int32_t) b | 1)); }
        if ((int64_t) b != -1 && b) { mix((uint64_t) ((int64_t) a / (int64_t) b)); mix((uint64_t) ((int64_t) a % (int64_t) b)); }
        unsigned __int128 w = (unsigned __int128) a * b; mix((uint64_t) w); mix((uint64_t) (w >> 64));
        __int128 sw = (__int128) (int64_t) a * (int64_t) b; mix((uint64_t) sw); mix((uint64_t) ((unsigned __int128) sw >> 64));
        if (b >> 32) { unsigned __int128 q = w / (b >> 32); mix((uint64_t) q); mix((uint64_t) (q >> 64)); }
        mix(__builtin_bswap64(a)); mix(__builtin_bswap32((uint32_t) a)); mix(__builtin_popcountll(a));
        if (a) { mix(__builtin_ctzll(a)); mix(__builtin_clzll(a)); }
        if ((uint32_t) a) { mix(__builtin_ctz((uint32_t) a)); mix(__builtin_clz((uint32_t) a)); }
        mix(a < b); mix((int64_t) a < (int64_t) b); mix((uint32_t) a <= (uint32_t) b); mix((int8_t) a > (int8_t) b);
        uint64_t r; mix(__builtin_add_overflow(a, b, &r)); mix(r);
        int64_t sr; mix(__builtin_sub_overflow((int64_t) a, (int64_t) b, &sr)); mix((uint64_t) sr);
        mix(__builtin_mul_overflow((int64_t) a >> 20, (int64_t) b >> 20, &sr)); mix((uint64_t) sr);
        int32_t s32; mix(__builtin_add_overflow((int32_t) a, (int32_t) b, &s32)); mix((uint64_t) s32);
        mix(a > b ? a : b); mix((a & 1) ? a * 5 + b : a + b * 9);
    }
}

__attribute__((noinline)) static void floats(void) {
    for (int i = 0; i < 100000; i++) {
        uint64_t ra = rnd(), rb = rnd();
        double a = (double) (int64_t) ra / 65536.0, b = (double) (int64_t) rb / 3.0e9;
        float fa = (float) a, fb = (float) b;
        mixd(a + b); mixd(a - b); mixd(a * b); mixd(b != 0 ? a / b : 0); mixd(sqrt(fabs(a)));
        mixd(fa + fb); mixd(fa * fb); mixd(fb != 0 ? fa / fb : 0); mixd(sqrtf(fabsf(fa)));
        mix(a < b); mix(a == b); mix(fa >= fb); mix(a != a);
        mix((uint64_t) (int64_t) (b)); mix((uint64_t) (int32_t) (fb)); mix((uint64_t) llrint(b)); mix((uint64_t) lrintf(fb * 1e-9f));
        mixd((double) (uint64_t) ra); mixd((double) (uint32_t) rb); mixd((float) (int32_t) ra); mixd((double) (float) a);
        mixd(floor(b)); mixd(ceil(b)); mixd(trunc(b)); mixd(fmin(a, b)); mixd(fmax(a, b)); mixd(copysign(a, b));
        mixd(fmod(a, b == 0 ? 1 : b)); mixd(ldexp(b, (int) (ra & 31) - 16)); int e; mixd(frexp(a, &e)); mix((uint64_t) e);
        double bits; memcpy(&bits, &ra, 8); // arbitrary bit patterns: NaN, inf, denormals
        mix(bits < a); mix(bits == bits); mixd(bits * 0.5 + (bits != bits ? 0 : 0)); mix(isnan(bits)); mix(isinf(bits));
        mix((uint64_t) (int64_t) (isfinite(bits) && fabs(bits) < 1e18 ? bits : 0));
    }
}

__attribute__((noinline)) static void longdoubles(void) {
    for (int i = 0; i < 50000; i++) {
        long double a = (long double) (int64_t) rnd() / 1048576.0L, b = (long double) (int64_t) rnd() / 7.0e8L;
        mixld(a + b); mixld(a - b); mixld(a * b); mixld(b != 0 ? a / b : 0); mixld(sqrtl(fabsl(a)));
        mix(a < b); mix(a == b); mix((uint64_t) (int64_t) b); mix((uint64_t) (int32_t) (b / 1e6L)); mix((uint64_t) llrintl(b));
        mixld((long double) (double) a); mixld((long double) (float) b); mixld(fabsl(b)); mixld(-a);
        mixld(floorl(b)); mixld(truncl(b)); mixld(fmodl(a, b == 0 ? 1 : b)); mixld(ldexpl(b, 5)); mixld(a * a * a - b * b);
    }
    char buf[256];
    for (int i = 0; i < 20000; i++) {
        double d = (double) (int64_t) rnd() / (double) ((rnd() & 0xffffff) + 1);
        int n = snprintf(buf, sizeof buf, "%.17g %e %f %a %.3Lf", d, d, d / 1e3, d, (long double) d / 3.0L);
        for (int k = 0; k < n; k++) mix((uint8_t) buf[k]);
        mixd(strtod(buf, NULL)); mixld(strtold(buf, NULL));
    }
}

__attribute__((noinline)) static void transcendental(void) {
    // rounded: ish computes some x87 functions through double
    double acc = 0;
    for (int i = 1; i < 20000; i++) {
        double x = i * 0.001;
        acc += sin(x) + cos(x) + exp(-x) + log(x) + pow(x, 1.5) + atan(x) + tanh(x) + cbrt(x) + log2(x) + exp2(x / 30);
    }
    printf("%-12s %.9g\n", "transc", acc);
    long double lacc = 0;
    for (int i = 1; i < 5000; i++) {
        long double x = i * 0.002L;
        lacc += sinl(x) + cosl(x) + expl(-x) + logl(x) + powl(x, 1.5L) + atanl(x) + log2l(x) + exp2l(x / 30) + log1pl(x) + expm1l(-x);
    }
    printf("%-12s %.9Lg\n", "transc-ld", lacc);
}

__attribute__((noinline)) static void strings(void) {
    static unsigned char a[9000], b[9000];
    for (int i = 0; i < 9000; i++) a[i] = (unsigned char) rnd();
    for (int i = 0; i < 30000; i++) {
        size_t off1 = rnd() % 4200, off2 = rnd() % 4200, len = rnd() % 4500;
        switch (rnd() % 6) {
            case 0: memcpy(b + off1, a + off2, len); break;
            case 1: memmove(a + off1, a + off2, len); break;
            case 2: memset(b + off1, (int) rnd(), len); break;
            case 3: mix((uint64_t) memcmp(a + off1, b + off2, len % 64 + 1) > 0); break;
            case 4: { a[off1 + len % 300] = 0; mix(strlen((char *) a + off1)); a[off1 + len % 300] = 1; break; }
            case 5: { void *p = memchr(a + off1, (int) (rnd() & 0xff), len); mix(p ? (uint64_t) ((unsigned char *) p - a) : 0); break; }
        }
    }
    for (int i = 0; i < 9000; i++) { mix(a[i]); mix(b[i]); }
    char t[128];
    for (int i = 0; i < 20000; i++) {
        int n = snprintf(t, sizeof t, "%llu:%lld:%x:%o:%5.3s|%-8d|%+d", (unsigned long long) rnd(), (long long) rnd(), (unsigned) rnd(), (unsigned) rnd() & 0xfff, "abcdef", (int) rnd(), (int) rnd());
        for (int k = 0; k < n; k++) mix((uint8_t) t[k]);
        mix(strtoull(t, NULL, 10)); mix((uint64_t) atoi(t + 3));
    }
}

static int cmp64(const void *x, const void *y) { uint64_t a = *(const uint64_t *) x, b = *(const uint64_t *) y; return a < b ? -1 : a > b; }
static jmp_buf jb;
__attribute__((noinline)) static void deep(int n) { volatile char pad[100]; pad[0] = (char) n; if (n == 0) longjmp(jb, 7); deep(n - 1); pad[1] = pad[0]; }
__attribute__((noinline)) static void misc(void) {
    uint64_t *v = malloc(50000 * 8);
    for (int i = 0; i < 50000; i++) v[i] = rnd();
    qsort(v, 50000, 8, cmp64);
    for (int i = 0; i < 50000; i += 7) mix(v[i]);
    free(v);
    for (int i = 0; i < 100; i++) { int r = setjmp(jb); if (r == 0) deep(i * 3); else mix((uint64_t) r + (uint64_t) i); }
    // bitfields, switch tables, function pointers
    struct { unsigned a:3, b:11, c:18; int d:7; } bf = {0};
    for (int i = 0; i < 100000; i++) { uint64_t r = rnd(); bf.a = r; bf.b = r >> 3; bf.c = r >> 14; bf.d = (int) (r >> 40); mix(bf.a + bf.b * 3 + bf.c * 5 + (uint64_t) bf.d);
        switch (r % 9) { case 0: mix(1); break; case 1: mix(r); break; case 2: mix(r * r); break; case 3: mix(~r); break; case 4: mix(r >> 7); break; case 5: mix(r << 9); break; case 6: mix(-r); break; case 7: mix(r ^ 0x55); break; default: mix(0); } }
}

static atomic_ulong counter; static atomic_int flag; static unsigned long plain, plain2;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static void *worker(void *arg) {
    (void) arg;
    for (int i = 0; i < 200000; i++) {
        atomic_fetch_add(&counter, 3);
        int expected = 0;
        while (!atomic_compare_exchange_weak(&flag, &expected, 1)) expected = 0;
        plain++;
        atomic_store(&flag, 0);
        if ((i & 1023) == 0) { pthread_mutex_lock(&mtx); plain2 += 2; pthread_mutex_unlock(&mtx); }
    }
    return NULL;
}
__attribute__((noinline)) static void threads(void) {
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, worker, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("%-12s %lu %lu %lu\n", "threads", (unsigned long) counter, plain, plain2);
}

int main(int argc, char **argv) {
#define WANT(n) (argc < 2 || strcmp(argv[1], n) == 0)
    if (WANT("ints")) { ints(); REPORT("ints"); }
    if (WANT("floats")) { floats(); REPORT("floats"); }
    if (WANT("longdouble")) { longdoubles(); REPORT("longdouble"); }
    if (WANT("transc")) transcendental();
    if (WANT("strings")) { strings(); REPORT("strings"); }
    if (WANT("misc")) { misc(); REPORT("misc"); }
    if (WANT("threads")) threads();
    return 0;
}
