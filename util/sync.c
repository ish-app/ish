#include <errno.h>
#include <limits.h>
#include "kernel/task.h"
#include "util/sync.h"
#include "debug.h"
#include "kernel/errno.h"

void cond_init(cond_t *cond) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
#if __linux__
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    pthread_cond_init(&cond->cond, &attr);
}
void cond_destroy(cond_t *cond) {
    pthread_cond_destroy(&cond->cond);
}

static bool is_signal_pending(lock_t *lock) {
    if (!current)
        return false;
    if (lock != &current->sighand->lock)
        lock(&current->sighand->lock);
    bool pending = !!(current->pending & ~current->blocked);
    if (lock != &current->sighand->lock)
        unlock(&current->sighand->lock);
    return pending;
}

int wait_for(cond_t *cond, lock_t *lock, struct timespec *timeout) {
    if (is_signal_pending(lock))
        return _EINTR;
    int err = wait_for_ignore_signals(cond, lock, timeout);
    if (err < 0)
        return _ETIMEDOUT;
    if (is_signal_pending(lock))
        return _EINTR;
    return 0;
}

int wait_for_ignore_signals(cond_t *cond, lock_t *lock, struct timespec *timeout) {
    if (current) {
        lock(&current->waiting_cond_lock);
        current->waiting_cond = cond;
        current->waiting_lock = lock;
        unlock(&current->waiting_cond_lock);
    }
    int rc = 0;
#if LOCK_DEBUG
    struct lock_debug lock_tmp = lock->debug;
    lock->debug = (struct lock_debug) { .initialized = lock->debug.initialized };
#endif
    if (!timeout) {
        pthread_cond_wait(&cond->cond, &lock->m);
    } else {
#if __linux__
        struct timespec abs_timeout;
        clock_gettime(CLOCK_MONOTONIC, &abs_timeout);
        abs_timeout.tv_sec += timeout->tv_sec;
        abs_timeout.tv_nsec += timeout->tv_nsec;
        if (abs_timeout.tv_nsec > 1000000000) {
            abs_timeout.tv_sec++;
            abs_timeout.tv_nsec -= 1000000000;
        }
        rc = pthread_cond_timedwait(&cond->cond, &lock->m, &abs_timeout);
#elif __APPLE__
        rc = pthread_cond_timedwait_relative_np(&cond->cond, &lock->m, timeout);
#else
#error Unimplemented pthread_cond_wait relative timeout.
#endif
    }
#if LOCK_DEBUG
    lock->debug = lock_tmp;
#endif

    if (current) {
        lock(&current->waiting_cond_lock);
        current->waiting_cond = NULL;
        current->waiting_lock = NULL;
        unlock(&current->waiting_cond_lock);
    }
    if (rc == ETIMEDOUT)
        return _ETIMEDOUT;
    return 0;
}

void notify(cond_t *cond) {
    pthread_cond_broadcast(&cond->cond);
}
void notify_once(cond_t *cond) {
    pthread_cond_signal(&cond->cond);
}

__thread sigjmp_buf unwind_buf;
__thread bool should_unwind = false;

void sigusr1_handler() {
    if (should_unwind) {
        should_unwind = false;
        siglongjmp(unwind_buf, 1);
    }
}

// This is how you would mitigate the unlock/wait race if the wait
// is async signal safe. wait_for *should* be safe from this race
// because of synchronization involving the waiting_cond_lock.
#if 0
    sigset_t sigusr1;
    sigemptyset(&sigusr1);
    sigaddset(&sigusr1, SIGUSR1);

    if (current) {
        if (sigsetjmp(unwind_buf, 1)) {
            return _EINTR;
        }
        should_unwind = true;
        sigprocmask(SIG_BLOCK, &sigusr1, NULL);
        if (lock != &current->sighand->lock)
            lock(&current->sighand->lock);
        bool pending = !!(current->pending & ~current->blocked);
        if (lock != &current->sighand->lock)
            unlock(&current->sighand->lock);
        sigprocmask(SIG_UNBLOCK, &sigusr1, NULL);
        if (pending) {
            should_unwind = false;
            return _EINTR;
        }
    }
#endif

