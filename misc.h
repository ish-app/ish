#ifndef MISC_H
#define MISC_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <assert.h>
#include <sys/types.h>
#include <stdnoreturn.h>
#include <stdbool.h>
#include <stdint.h>
#endif

// utility macros
#define glue(a, b) _glue(a, b)
#define _glue(a, b) a##b
#define glue3(a,b,c) glue(a, glue(b, c))
#define glue4(a,b,c,d) glue(a, glue3(b, c, d))

#define str(x) _str(x)
#define _str(x) #x

// compiler check
#define is_gcc(version) (__GNUC__ >= version)

#if !defined(__has_attribute)
#define has_attribute(x) 0
#else
#define has_attribute __has_attribute
#endif

#if !defined(__has_feature)
#define has_feature(x) 0
#else
#define has_feature __has_feature
#endif

// keywords
#define bitfield unsigned int
#define forceinline inline __attribute__((always_inline))
#if defined(NDEBUG) || defined(__KERNEL__)
#define posit __builtin_assume
#else
#define posit assert
#endif
#define must_check __attribute__((warn_unused_result))

#ifndef __KERNEL__
#define unlikely(x) __builtin_expect((x), 0)
#define typecheck(type, x) ({type _x = x; x;})
#define container_of(ptr, type, member) \
    ((type *) ((char *) (ptr) - offsetof(type, member)))
#if has_attribute(fallthrough)
#define fallthrough __attribute__((fallthrough))
#else
#define fallthrough
#endif
#endif

#if has_attribute(no_sanitize)
#define __no_instrument_msan
#if defined(__has_feature)
#if has_feature(memory_sanitizer)
#undef __no_instrument_msan
#define __no_instrument_msan __attribute__((no_sanitize("memory"))
#endif
#endif
#define __no_instrument __attribute__((no_sanitize("address", "thread", "undefined", "leak"))) __no_instrument_msan
#else
#define __no_instrument
#endif

#if has_attribute(nonstring)
#define __strncpy_safe __attribute__((nonstring))
#else
#define __strncpy_safe
#endif

#define zero_init(type) ((type[1]){}[0])
#define pun(type, x) (((union {typeof(x) _; type a;}) (x)).a)

#define UNUSED(x) UNUSED_##x __attribute__((unused))
static inline void __use(int dummy __attribute__((unused)), ...) {}
#define use(...) __use(0, ##__VA_ARGS__)

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

#ifndef __KERNEL__
#define array_size(arr) (sizeof(arr)/sizeof((arr)[0]))
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

#ifndef __KERNEL__
#define ERR_PTR(err) (void *) (intptr_t) (err)
#define PTR_ERR(ptr) (intptr_t) (ptr)
#define IS_ERR(ptr) ((uintptr_t) (ptr) > (uintptr_t) -0xfff)
#endif

#endif
