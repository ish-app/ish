#include <stdio.h>
#include <stdint.h>
#include <xmmintrin.h>

void main(void) {
	uint64_t buf0123[2] = {0x0011223344556677LL, 0x8899aabbccddeeffLL};

	// xmm0 Initially 0101
	printf("%08x\n", _mm_movemask_epi8(_mm_load_si128((__m128i*) &buf0123)));
}


