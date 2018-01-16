#ifndef BITS_H
#define BITS_H

typedef void bits_t;
#define BITS_SIZE(bits) ((((bits) - 1) / 8) + 1)

static inline bool bit_test(size_t i, bits_t *data) {
    char *c = data;
    return c[i >> 3] & (1 << (i & 7)) ? 1 : 0;
}

static inline void bit_set(size_t i, bits_t *data) {
    char *c = data;
    c[i >> 3] |= 1 << (i & 7);
}

static inline void bit_clear(size_t i, bits_t *data) {
    char *c = data;
    c[i >> 3] &= ~(1 << (i & 7));
}

#endif
