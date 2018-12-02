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

int wait_for(cond_t *cond, lock_t *lock, struct timespec *timeout) {
    if (current && current->pending)
        return _EINTR;
    int err = wait_for_ignore_signals(cond, lock, timeout);
    if (err < 0)
        return _ETIMEDOUT;
    if (current && current->pending)
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

/*
int foo() {
    if (sigunwind_start()) {
        int retval = syscall();
        sigunwind_end();
        return retval;
    } else {
        return _EINTR;
    }
}
*/
