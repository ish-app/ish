#ifndef UTIL_SYNC_H
#define UTIL_SYNC_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>
#include <setjmp.h>

// locks, implemented using pthread

#define LOCK_DEBUG 0

typedef struct {
    pthread_mutex_t m;
    pthread_t owner;
#if LOCK_DEBUG
    const char *file;
    int line;
    int pid;
#endif
} lock_t;
#define lock_init(lock) pthread_mutex_init(&(lock)->m, NULL)
#if LOCK_DEBUG
#define LOCK_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0, 0}
#else
#define LOCK_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, 0}
#endif
static inline void __lock(lock_t *lock, __attribute__((unused)) const char *file, __attribute__((unused)) int line) {
    pthread_mutex_lock(&lock->m);
    lock->owner = pthread_self();
#if LOCK_DEBUG
    lock->file = file;
    lock->line = line;
    extern int current_pid(void);
    lock->pid = current_pid();
#endif
}
#define lock(lock) __lock(lock, __FILE__, __LINE__)
static inline void unlock(lock_t *lock) {
    pthread_mutex_unlock(&lock->m);
#if LOCK_DEBUG
    lock->file = NULL;
    lock->line = 0;
    lock->pid = 0;
#endif
}
#define unlock(lock) pthread_mutex_unlock(&(lock)->m)

// conditions, implemented using pthread conditions but hacked so you can also
// be woken by a signal

typedef struct {
    pthread_cond_t cond;
} cond_t;
#define COND_INITIALIZER {PTHREAD_COND_INITIALIZER}

// Must call before using the condition
void cond_init(cond_t *cond);
// Must call when finished with the condition (currently doesn't do much but might do something important eventually I guess)
void cond_destroy(cond_t *cond);
// Releases the lock, waits for the condition, and reacquires the lock.
// Returns _EINTR if waiting stopped because the thread received a signal,
// _ETIMEDOUT if waiting stopped because the timout expired, 0 otherwise.
int wait_for(cond_t *cond, lock_t *lock, struct timespec *timeout);
// Same as wait_for, except it will never return _EINTR
int wait_for_ignore_signals(cond_t *cond, lock_t *lock, struct timespec *timeout);
// Wake up all waiters.
void notify(cond_t *cond);
// Wake up one waiter.
void notify_once(cond_t *cond);

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
#define wrlock_destroy(lock) pthread_rwlock_destroy(lock)
#define read_wrlock(lock) pthread_rwlock_rdlock(lock)
#define read_wrunlock(lock) pthread_rwlock_unlock(lock)
#define write_wrlock(lock) pthread_rwlock_wrlock(lock)
#define write_wrunlock(lock) pthread_rwlock_unlock(lock)

extern __thread sigjmp_buf unwind_buf;
extern __thread bool should_unwind;
static inline int sigunwind_start() {
    if (sigsetjmp(unwind_buf, 1)) {
        should_unwind = false;
        return 1;
    } else {
        should_unwind = true;
        return 0;
    }
}
static inline void sigunwind_end() {
    should_unwind = false;
}

#endif
