#include <stdio.h>
#include <xmmintrin.h>

#define printout() printf("%05.2f %05.2f %05.2f %05.2f\n", out[0], out[1], out[2], out[3])

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

	// Move xmm2 onto xmm1
	asm volatile(	"movaps %[vec2], %[vec1]\n\t"
		: [vec1] "+x" (xmm1)
		: [vec2] "x" (xmm2));

	_mm_store_ps((float*) out, xmm1);
    printout();
} 
