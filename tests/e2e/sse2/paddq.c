#include <stdint.h>
#include <stdio.h>
#include <emmintrin.h>
#include <xmmintrin.h>

#define printout() printf("%05lld %05lld\n", (long long) out[0], (long long) out[1])

void main(void) {
    int64_t out[2] = { 0, 0 };
    int64_t buf1234[2] = {  1234,  5678 };
    int64_t buf1111[2] = { 11111, 11111 };

    // xmm1 Initially 1234
    __m128i xmm1 = _mm_load_si128((__m128i*) buf1234);
    _mm_store_si128((__m128i*) out, xmm1);
    printout();

    // xmm2 Initially 1111
    __m128i xmm2 = _mm_load_si128((__m128i*) buf1111);
    _mm_store_si128((__m128i*) out, xmm2);
    printout();

    // Result is just each added by 1
    asm volatile(	"paddq %[vec2], %[vec1]\n\t"
            : [vec1] "+x" (xmm1), [vec2] "+x" (xmm2)
            :);
    _mm_store_si128((__m128i*) out, xmm1);
    printout();
}
