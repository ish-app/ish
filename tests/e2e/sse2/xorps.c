#include <stdio.h>
#include <xmmintrin.h>

#define printout() printf("%05.2E %05.2E %05.2E %05.2E\n", out[0], out[1], out[2], out[3])

void main(void) {
	float out[4] = { 0, 0, 0, 0 };
	float buf0000[4] = { 00.00, 00.00, 00.00, 00.00 };
	float buf1234[4] = { 11.11, 22.22, 33.33, 44.44 };
	float buf5678[4] = { 55.55, 66.66, 77.77, 88.88 };

	// xmm0 Initially 1234
	__m128 xmm0 = _mm_load_ps((float*) buf0000);
	_mm_store_ps((float*) out, xmm0);
	printout();

	// xmm1 Initially 1234
	__m128 xmm1 = _mm_load_ps((float*) buf1234);
	_mm_store_ps((float*) out, xmm1);
	printout();

	// xmm2 Initially 5678
	__m128 xmm2 = _mm_load_ps((float*) buf5678);
	_mm_store_ps((float*) out, xmm2);
	printout();

    // 0000 ^ 1234 = 1234
    _mm_store_ps((float*) out, _mm_xor_ps(xmm0, xmm1));
    printout();

    // 1234 ^ 1234 = 0000
    _mm_store_ps((float*) out, _mm_xor_ps(xmm1, xmm1));
    printout();

    // 1234 ^ 5678 = some known value
    _mm_store_ps((float*) out, _mm_xor_ps(xmm1, xmm2));
    printout();

    // 5678 ^ (1234 ^ 5678) = 1234
    _mm_store_ps((float*) out, _mm_xor_ps(xmm2, _mm_xor_ps(xmm1, xmm2)));
    printout();

    // setzero with xorps = 0000
    _mm_store_ps((float*) out, _mm_setzero_ps());
    printout();
}
