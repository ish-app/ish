#ifndef TYPES_H
#define TYPES_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

// word_t will be 64-bit to read 64-bit elves
typedef uint32_t dword_t;
typedef uint16_t word_t;
typedef uint8_t byte_t;

typedef dword_t addr_t;
typedef dword_t page_t;

#define TODO(message) { \
    fprintf(stderr, "TODO: %s\n", message); \
    abort(); \
}

#endif
