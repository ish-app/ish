#include <math.h>
#include <stdio.h>
#include <emmintrin.h>
#include <xmmintrin.h>

#define CMP(imm) \
    x = _mm_loadu_ps(a); y = _mm_loadu_ps(b); \
    asm volatile("cmpps $" #imm ", %[y], %[x]" : [x] "+x" (x) : [y] "x" (y)); \
    _mm_storeu_si128((__m128i *) out, _mm_castps_si128(x)); \
    printf("cmpps %d: %08x %08x %08x %08x\n", imm, out[0], out[1], out[2], out[3]);

void main(void) {
    unsigned out[4];
    float a[4] = { 1.0, 2.0, 3.0, NAN };
    float b[4] = { 1.0, 3.0, 2.0, 1.0 };
    __m128 x, y;
    CMP(0) CMP(1) CMP(2) CMP(3) CMP(4) CMP(5) CMP(6) CMP(7)

    // cmppd, with the 66 prefix, compares two doubles: not four floats
    double c[2] = { 1.0, 5.0 };
    double d[2] = { 2.0, 4.0 };
    __m128d p = _mm_loadu_pd(c), q = _mm_loadu_pd(d);
    asm volatile("cmppd $1, %[q], %[p]" : [p] "+x" (p) : [q] "x" (q));
    _mm_storeu_si128((__m128i *) out, _mm_castpd_si128(p));
    printf("cmppd 1: %08x %08x %08x %08x\n", out[0], out[1], out[2], out[3]);
}
