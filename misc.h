#ifndef MISC_H
#define MISC_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <stdatomic.h>
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
#define typecheck(type, x) ({type _x = x; x;})
#define must_check __attribute__((warn_unused_result))
#if defined(__has_attribute) && __has_attribute(no_sanitize)
#define __no_instrument __attribute__((no_sanitize("address", "thread", "undefined", "leak", "memory")))
#else
#define __no_instrument
#endif

#if defined(__x86_64__)
#define rdtsc() ({ \
        uint32_t low, high; \
        __asm__ volatile("rdtsc" : "=a" (high), "=d" (low)); \
        ((uint64_t) high) << 32 | low; \
    })
#elif defined(__arm64__)
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

typedef dword_t pid_t_;
typedef dword_t uid_t_;
typedef word_t mode_t_;
typedef sqword_t off_t_;

#define uint(size) CONCAT3(uint,size,_t)
#define sint(size) CONCAT3(int,size,_t)

typedef pthread_mutex_t lock_t;
#define lock_init(lock) pthread_mutex_init(lock, NULL)
#define LOCK_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define lock(lock) pthread_mutex_lock(lock)
#define unlock(lock) pthread_mutex_unlock(lock)

// this is a read-write lock that prefers writers, i.e. if there are any
// writers waiting a read lock will block.
// on darwin pthread_rwlock_t is already like this, on linux you can configure
// it to prefer writers. not worrying about anything else right now.
typedef pthread_rwlock_t wrlock_t;
static inline void wrlock_init(wrlock_t *lock) {
    pthread_rwlockattr_t *pattr = NULL;
#if defined(__GLIBC__)
    pthread_rwlockattr_t attr;
    pattr = &attr;
    pthread_rwlockattr_init(pattr);
    pthread_rwlockattr_setkind_np(pattr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
    pthread_rwlock_init(lock, pattr);
}
#define read_wrlock(lock) pthread_rwlock_rdlock(lock)
#define read_wrunlock(lock) pthread_rwlock_unlock(lock)
#define write_wrlock(lock) pthread_rwlock_wrlock(lock)
#define write_wrunlock(lock) pthread_rwlock_unlock(lock)

#define wait_for(cond, lock) pthread_cond_wait(cond, lock)
#define notify(cond) pthread_cond_broadcast(cond)

#define ERR_PTR(err) (void *) (intptr_t) (err)
#define PTR_ERR(ptr) (intptr_t) (ptr)
#define IS_ERR(ptr) ((uintptr_t) (ptr) > (uintptr_t) -0xfff)

#endif
