#ifndef MISC_H
#define MISC_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <sys/types.h>

// utility macros
#define glue(a, b) _glue(a, b)
#define _glue(a, b) a##b
#define glue3(a,b,c) glue(a, glue(b, c))
#define glue4(a,b,c,d) glue(a, glue3(b, c, d))

#define str(x) _str(x)
#define _str(x) #x

// keywords
#define bits unsigned int
#define forceinline inline __attribute__((always_inline))
#define flatten __attribute__((flatten))
#ifdef NDEBUG
#define postulate __builtin_assume
#else
#define postulate assert
#endif
#define unlikely(x) __builtin_expect((x), 0)
#define typecheck(type, x) ({type _x = x; x;})
#define must_check __attribute__((warn_unused_result))
#if defined(__has_attribute) && __has_attribute(no_sanitize)
#define __no_instrument __attribute__((no_sanitize("address", "thread", "undefined", "leak", "memory")))
#else
#define __no_instrument
#endif
#define UNUSED(x) UNUSED_##x __attribute__((unused))

#if defined(__x86_64__)
#define rdtsc() ({ \
        uint32_t low, high; \
        __asm__ volatile("rdtsc" : "=a" (high), "=d" (low)); \
        ((uint64_t) high) << 32 | low; \
    })
#elif defined(__arm64__) || defined(__aarch64__)
#define rdtsc() ({ \
        uint64_t tsc; \
        __asm__ volatile("mrs %0, PMCCNTR_EL0" : "=r" (tsc)); \
        tsc; \
    })
#endif

// types
typedef int64_t sqword_t;
typedef uint64_t qword_t;
typedef uint32_t dword_t;
typedef int32_t sdword_t;
typedef uint16_t word_t;
typedef uint8_t byte_t;

typedef dword_t addr_t;
typedef dword_t uint_t;
typedef sdword_t int_t;

typedef sdword_t pid_t_;
typedef dword_t uid_t_;
typedef word_t mode_t_;
typedef sqword_t off_t_;
typedef dword_t time_t_;
typedef dword_t clock_t_;

#define uint(size) glue3(uint,size,_t)
#define sint(size) glue3(int,size,_t)

#define ERR_PTR(err) (void *) (intptr_t) (err)
#define PTR_ERR(ptr) (intptr_t) (ptr)
#define IS_ERR(ptr) ((uintptr_t) (ptr) > (uintptr_t) -0xfff)

#endif
