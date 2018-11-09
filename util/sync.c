#include <errno.h>
#include <limits.h>
#include "kernel/task.h"
#include "util/sync.h"
#include "debug.h"

#if USE_POSIX_SEM
static void _sem_init(_sem_t *sem, int init) {
    // cannot fail if value and pshared are 0
    sem_init(sem, 0, init);
}
static int _sem_wait(_sem_t *sem) {
    if (current != NULL && current->pending)
        return 1; // signal is waiting
    int err = sem_wait(sem);
    if (err < 0 && errno == EINTR)
        return 1;
    return 0;
}
static void _sem_post(_sem_t *sem) {
    sem_post(sem);
}
static void _sem_destroy(_sem_t *sem) {}

#else

static task_t main_task;
__attribute__((constructor)) static void get_main_task() {
    main_task = mach_task_self();
}

static void _sem_init(_sem_t *sem, int init) {
    semaphore_create(main_task, sem, SYNC_POLICY_FIFO, init);
}
static int _sem_wait(_sem_t *sem) {
    if (current != NULL && current->pending)
        return 1; // signal is waiting
    kern_return_t err = semaphore_wait(*sem);
    if (err == KERN_ABORTED)
        return 1;
    return 0;
}
static void _sem_post(_sem_t *sem) {
    semaphore_signal(*sem);
}
static void _sem_destroy(_sem_t *sem) {
    semaphore_destroy(main_task, *sem);
}
#endif

void cond_init(cond_t *cond) {
    cond->waiters = 0;
    lock_init(&cond->lock);
    _sem_init(&cond->waitsem, 0);
    _sem_init(&cond->donesem, 0);
}
void cond_destroy(cond_t *cond) {
    _sem_destroy(&cond->waitsem);
    _sem_destroy(&cond->donesem);
}

int wait_for(cond_t *cond, lock_t *lock) {
    lock(&cond->lock);
    cond->waiters++;
    unlock(&cond->lock);
    unlock(lock);

    int retval = _sem_wait(&cond->waitsem);
    _sem_post(&cond->donesem);
    lock(lock);
    return retval;
}

int notify_count(cond_t *cond, int count) {
    lock(&cond->lock);
    if (count > cond->waiters)
        count = cond->waiters;
    for (int i = 0; i < count; i++)
        _sem_post(&cond->waitsem);
    for (int i = 0; i < count; i++)
        _sem_wait(&cond->donesem);
    unlock(&cond->lock);
    return count;
}
void notify(cond_t *cond) {
    notify_count(cond, INT_MAX);
}
