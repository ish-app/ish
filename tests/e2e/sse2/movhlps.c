#include <stdio.h>
#include <xmmintrin.h>

#define printps() printf("%05.2f %05.2f %05.2f %05.2f\n", out[0], out[1], out[2], out[3])

void main(void) {
    float out[4];
    float a[4] = { 11.11, 22.22, 33.33, 44.44 };
    float b[4] = { 55.55, 66.66, 77.77, 88.88 };
    __m128 x, y;

    // the high half of y into the low half of x
    x = _mm_loadu_ps(a); y = _mm_loadu_ps(b);
    asm volatile("movhlps %[y], %[x]" : [x] "+x" (x) : [y] "x" (y));
    _mm_storeu_ps(out, x); printps();

    // the low half of y into the high half of x
    x = _mm_loadu_ps(a); y = _mm_loadu_ps(b);
    asm volatile("movlhps %[y], %[x]" : [x] "+x" (x) : [y] "x" (y));
    _mm_storeu_ps(out, x); printps();

    // with memory, the same opcodes are movlps and movhps, and still work
    x = _mm_loadu_ps(a);
    asm volatile("movlps %[m], %[x]" : [x] "+x" (x) : [m] "m" (b));
    _mm_storeu_ps(out, x); printps();
    x = _mm_loadu_ps(a);
    asm volatile("movhps %[m], %[x]" : [x] "+x" (x) : [m] "m" (b));
    _mm_storeu_ps(out, x); printps();
}
