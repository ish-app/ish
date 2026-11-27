#include <math.h>
#include <stdio.h>
#include <emmintrin.h>
#include <xmmintrin.h>

#define printps() printf("%05.2f %05.2f %05.2f %05.2f\n", outs[0], outs[1], outs[2], outs[3])
#define printpd() printf("%05.2f %05.2f\n", outd[0], outd[1])

void main(void) {
    float outs[4];
    double outd[2];
    float a[4] = { 1.5, -2.0, 0.0, 8.0 };
    float b[4] = { 2.5, -3.0, -0.0, 2.0 };
    double c[2] = { 7.0, -1.5 };
    double d[2] = { 2.0, 4.0 };

    __m128 x, y;
    x = _mm_loadu_ps(a); y = _mm_loadu_ps(b);
    asm volatile("minps %[y], %[x]" : [x] "+x" (x) : [y] "x" (y));
    _mm_storeu_ps(outs, x); printps();
    // min(+0, -0) is the second operand: the sign shows which one came back
    printf("%d\n", signbit(outs[2]) != 0);

    x = _mm_loadu_ps(a); y = _mm_loadu_ps(b);
    asm volatile("maxps %[y], %[x]" : [x] "+x" (x) : [y] "x" (y));
    _mm_storeu_ps(outs, x); printps();

    x = _mm_loadu_ps(a); y = _mm_loadu_ps(b);
    asm volatile("divps %[y], %[x]" : [x] "+x" (x) : [y] "x" (y));
    _mm_storeu_ps(outs, x);
    printf("%05.2f %05.2f %05.2f\n", outs[0], outs[1], outs[3]);

    // a NaN in either operand gives the second
    float n[4] = { NAN, 1.0, 1.0, 1.0 };
    x = _mm_loadu_ps(n); y = _mm_loadu_ps(b);
    asm volatile("minps %[y], %[x]" : [x] "+x" (x) : [y] "x" (y));
    _mm_storeu_ps(outs, x); printps();

    __m128d p, q;
    p = _mm_loadu_pd(c); q = _mm_loadu_pd(d);
    asm volatile("minpd %[q], %[p]" : [p] "+x" (p) : [q] "x" (q));
    _mm_storeu_pd(outd, p); printpd();
    p = _mm_loadu_pd(c); q = _mm_loadu_pd(d);
    asm volatile("maxpd %[q], %[p]" : [p] "+x" (p) : [q] "x" (q));
    _mm_storeu_pd(outd, p); printpd();
    p = _mm_loadu_pd(c); q = _mm_loadu_pd(d);
    asm volatile("divpd %[q], %[p]" : [p] "+x" (p) : [q] "x" (q));
    _mm_storeu_pd(outd, p); printpd();
}
