#ifndef MISC_H
#define MISC_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <sys/types.h>
#include <pthread.h>

// utility macros
#define CONCAT(a, b) _CONCAT(a, b)
#define _CONCAT(a, b) a##b
#define CONCAT3(a,b,c) CONCAT(a, CONCAT(b, c))
#define CONCAT4(a,b,c,d) CONCAT(a, CONCAT3(b, c, d))

#define STR(x) _STR(x)
#define _STR(x) #x

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
#define must_check __attribute__((warn_unused_result))

#if defined(__i386__) || defined(__x86_64__)
#define debugger __asm__("int3")
#elif defined(__arm__)
#define debugger __asm__("trap")
#endif

// types
typedef uint64_t qword_t;
typedef uint32_t dword_t;
typedef int32_t sdword_t;
typedef uint16_t word_t;
typedef uint8_t byte_t;

typedef dword_t addr_t;
typedef dword_t uint_t;
typedef sdword_t int_t;

typedef dword_t pid_t_;
typedef word_t mode_t_;

#define uint(size) CONCAT3(uint,size,_t)
#define sint(size) CONCAT3(int,size,_t)

#define lock_init(mutex) pthread_mutex_init(mutex, NULL)
#define lock(thing) pthread_mutex_lock(&(thing)->lock)
#define unlock(thing) pthread_mutex_unlock(&(thing)->lock)
#define wait_for(thing, what) pthread_cond_wait(&(thing)->what, &(thing)->lock)
#define notify(thing, what) pthread_cond_broadcast(&(thing)->what)

#define ERR_PTR(err) (void *) (intptr_t) (err)
#define PTR_ERR(ptr) (intptr_t) (ptr)
#define IS_ERR(ptr) ((uintptr_t) (ptr) > (uintptr_t) -0xfff)

#endif
