#include <stdio.h>
#include <stdint.h>
#include <xmmintrin.h>

#define printout() printf("%016llx%016llx\n", (unsigned long long) out[0], (unsigned long long) out[1])

void main(void) {
	uint64_t out[2] = { 0, 0 };
	uint64_t buf0101[2] = {0x0000000011223344LL, 0x0000000011223344LL};
	uint64_t buf1111[2] = {0x1122334411223344LL, 0x1122334411223344LL};
	uint64_t buf0f00[2] = {0x00000000FFFFFFFFLL, 0x0000000000000000LL};

	// xmm0 Initially 0101
	__m128i xmm0 = _mm_load_si128((__m128i*) &buf0101);
	_mm_store_si128((__m128i*) out, xmm0);
	printout();

	// xmm1 Initially 1111
	__m128i xmm1 = _mm_load_si128((__m128i*) &buf1111);
	_mm_store_si128((__m128i*) out, xmm1);
	printout();

    // xmm2 0101 == 1111 -> 0f0f
    __m128i xmm2 = _mm_cmpeq_epi8(xmm0, xmm1);
	_mm_store_si128((__m128i*) out, xmm2);
	printout();

    // xmm3 Initially 0f00
	__m128i xmm3 = _mm_load_si128((__m128i*) &buf0f00);
	_mm_store_si128((__m128i*) out, xmm3);
	printout();

    // 0f0f == 0f00 -> fff0
	_mm_store_si128((__m128i*) out, _mm_cmpeq_epi8(xmm2, xmm3));
	printout();
}
