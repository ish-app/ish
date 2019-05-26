#include <stdio.h>
#include <xmmintrin.h>

#define NOINLINE __attribute__ ((noinline))
#define printout() printf("%05.2f %05.2f %05.2f %05.2f\n", out[0], out[1], out[2], out[3])

void move5(__m128 *xmm1, __m128 *xmm2);
void move1612(__m128 *xmm1, float fa);
void store1612(__m128 *xmm1, float *fa);

void main(void) {
	float out[4] = { 0, 0, 0, 0 };
	float buf1234[4] = { 11.11, 22.22, 33.33, 44.44 };
	float buf5678[4] = { 55.55, 66.66, 77.77, 88.88 };
    float fa = 16.12;

	// xmm1 Initially 1234
	__m128 xmm1 = _mm_load_ps((float*) buf1234);
	_mm_store_ps((float*) out, xmm1);
	printout();

	// xmm2 Initially 5678
	__m128 xmm2 = _mm_load_ps((float*) buf5678);
	_mm_store_ps((float*) out, xmm2);
	printout();

    //move5(&xmm1, &xmm2);
	__m128 xmm3 = _mm_move_ss(xmm1, xmm2);
	_mm_store_ps((float*) out, xmm3);
    printout();

    move1612(&xmm1, fa);
	_mm_store_ps((float*) out, xmm1);
    printout();

    fa = 00.00;
    store1612(&xmm1, &fa);
    printf("%05.2f\n", fa);
}

void NOINLINE move5(__m128 *xmm1, __m128 *xmm2) {
	// Move the 5 from 5678, rest should remain: 5234.
	//*xmm1 = _mm_move_ss(*xmm1, *xmm2);
	asm volatile(	"movss %[vec2], %[vec1]\n\t"
		: [vec1] "+x" (*xmm1)
		: [vec2] "x" (*xmm2));
}

void NOINLINE move1612(__m128 *xmm1, float fa) {
    // Move the 16.12 into first position of xmm1.
    // This is mem, so rest should be zeroed.
	asm volatile(	"movss %[flt], %[vec]\n\t"
		: [vec] "+x" (*xmm1)
		: [flt] "m" (fa));
}

void NOINLINE store1612(__m128 *xmm1, float *fa) {
    // Store the 16.12 into float.
	asm volatile(	"movss %[vec], %[flt]\n\t"
		: [flt] "+m" (*fa)
		: [vec] "x" (*xmm1));
}
