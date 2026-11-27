#include <math.h>
#include <stdio.h>
#include <xmmintrin.h>

void main(void) {
    float v[4][4] = {
        { 1.0, 2.0, 3.0, 4.0 },
        { -1.0, 2.0, -3.0, 4.0 },
        { -0.0, -INFINITY, 0.0, -NAN },
        { -1.0, -2.0, -3.0, -4.0 },
    };
    for (int k = 0; k < 4; k++) {
        __m128 x = _mm_loadu_ps(v[k]);
        int mask;
        asm volatile("movmskps %[x], %[m]" : [m] "=r" (mask) : [x] "x" (x));
        printf("%x\n", mask);
    }
}
