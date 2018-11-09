#ifndef UTIL_SYNC_H
#define UTIL_SYNC_H

#if __APPLE__
#define USE_DARWIN_SEM 1
#elif __linux__
#define USE_POSIX_SEM 1
#else
#error "unsupported platform"
#endif

#include <stdatomic.h>
#include <pthread.h>

// locks, implemented using pthread
typedef pthread_mutex_t lock_t;
#define lock_init(lock) pthread_mutex_init(lock, NULL)
#define LOCK_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define lock(lock) pthread_mutex_lock(lock)
#define unlock(lock) pthread_mutex_unlock(lock)

// conditions, implemented using semaphores so EINTR and arbitrary numbers of
// wakes can be a thing

#if USE_DARWIN_SEM
#include <mach/mach.h>
typedef semaphore_t _sem_t;
#elif USE_POSIX_SEM
#include <semaphore.h>
typedef sem_t _sem_t;
#endif

typedef struct {
    lock_t lock;
    int waiters;
    _sem_t waitsem;
    _sem_t donesem;
} cond_t;

// Must call before using the condition
void cond_init(cond_t *cond);
// TODO we might need: void cond_destroy(cond_t *cond);
// Releases the lock, waits for the condition, and reacquires the lock. Return
// 0 if waiting stopped because another thread called notify, or 1 if waiting
// stopped because the thread received a signal.
int wait_for(cond_t *cond, lock_t *lock);
void notify(cond_t *cond); // equivalent to notify_count(cond, INT_MAX)
// Wake up at most the given number of waiters. Return the number of waiters
// actually woken up.
int notify_count(cond_t *cond, int count);

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

#endif
