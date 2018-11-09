#include <errno.h>
#include <limits.h>
#include "kernel/task.h"
#include "util/sync.h"
#include "debug.h"

void cond_init(cond_t *cond) {
    pthread_cond_init(&cond->cond, NULL);
}
void cond_destroy(cond_t *cond) {
    pthread_cond_destroy(&cond->cond);
}

int wait_for(cond_t *cond, lock_t *lock) {
    if (current && current->pending)
        return 1;
    if (current) {
        lock(&current->waiting_cond_lock);
        current->waiting_cond = cond;
        current->waiting_lock = lock;
        unlock(&current->waiting_cond_lock);
    }
    pthread_cond_wait(&cond->cond, lock);
    if (current) {
        lock(&current->waiting_cond_lock);
        current->waiting_cond = NULL;
        current->waiting_lock = NULL;
        unlock(&current->waiting_cond_lock);
    }
    if (current && current->pending)
        return 1;
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
