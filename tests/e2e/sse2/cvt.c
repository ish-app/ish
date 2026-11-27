#include <stdio.h>
#include <emmintrin.h>
#include <xmmintrin.h>

void main(void) {
    float outs[4];
    double outd[2];
    int ints[4] = { 1, -2, 16777217, -2147483647 - 1 };
    float floats[4] = { 1.25, -3.5, 1e30, 0.0 };

    __m128i i = _mm_loadu_si128((__m128i *) ints);
    __m128 f;
    asm volatile("cvtdq2ps %[i], %[f]" : [f] "=x" (f) : [i] "x" (i));
    _mm_storeu_ps(outs, f);
    printf("%.1f %.1f %.1f %.1f\n", outs[0], outs[1], outs[2], outs[3]);

    __m128d d;
    asm volatile("cvtdq2pd %[i], %[d]" : [d] "=x" (d) : [i] "x" (i));
    _mm_storeu_pd(outd, d);
    printf("%.1f %.1f\n", outd[0], outd[1]);

    f = _mm_loadu_ps(floats);
    asm volatile("cvtps2pd %[f], %[d]" : [d] "=x" (d) : [f] "x" (f));
    _mm_storeu_pd(outd, d);
    printf("%.2f %.2f\n", outd[0], outd[1]);

    // the same register as source and destination
    i = _mm_loadu_si128((__m128i *) ints);
    asm volatile("cvtdq2pd %[r], %[r]" : [r] "+x" (i));
    _mm_storeu_pd(outd, _mm_castsi128_pd(i));
    printf("%.1f %.1f\n", outd[0], outd[1]);
}
