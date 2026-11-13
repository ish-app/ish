#include "kernel/calls.h"

#define FUTEX_WAIT_ 0
#define FUTEX_WAKE_ 1
#define FUTEX_REQUEUE_ 3
#define FUTEX_CMP_REQUEUE_ 4
#define FUTEX_WAIT_BITSET_ 9
#define FUTEX_WAKE_BITSET_ 10
#define FUTEX_PRIVATE_FLAG_ 128
#define FUTEX_CLOCK_REALTIME_ 256
#define FUTEX_CMD_MASK_ ~(FUTEX_PRIVATE_FLAG_ | FUTEX_CLOCK_REALTIME_)

struct futex {
    atomic_uint refcount;
    struct mem *mem;
    addr_t addr;
    struct list queue;
    struct list chain; // locked by futex_hash_lock
};

struct futex_wait {
    cond_t cond;
    struct futex *futex; // will be changed by a requeue
    struct list queue;
};

#define FUTEX_HASH_BITS 12
#define FUTEX_HASH_SIZE (1 << FUTEX_HASH_BITS)
static lock_t futex_lock = LOCK_INITIALIZER;
static struct list futex_hash[FUTEX_HASH_SIZE];

static void __attribute__((constructor)) init_futex_hash(void) {
    for (int i = 0; i < FUTEX_HASH_SIZE; i++)
        list_init(&futex_hash[i]);
}

static struct futex *futex_get_unlocked(addr_t addr) {
    int hash = (addr ^ (unsigned long) current->mem) % FUTEX_HASH_SIZE;
    struct list *bucket = &futex_hash[hash];
    struct futex *futex;
    list_for_each_entry(bucket, futex, chain) {
        if (futex->addr == addr && futex->mem == current->mem) {
            futex->refcount++;
            return futex;
        }
    }

    futex = malloc(sizeof(struct futex));
    if (futex == NULL) {
        unlock(&futex_lock);
        return NULL;
    }
    futex->refcount = 1;
    futex->mem = current->mem;
    futex->addr = addr;
    list_init(&futex->queue);
    list_add(bucket, &futex->chain);
    return futex;
}

// Returns the futex for the current process at the given addr, and locks it
// Unlocked variant is available for times when you need to get two futexes at once
static struct futex *futex_get(addr_t addr) {
    lock(&futex_lock);
    struct futex *futex = futex_get_unlocked(addr);
    if (futex == NULL)
        unlock(&futex_lock);
    return futex;
}

static void futex_put_unlocked(struct futex *futex) {
    if (--futex->refcount == 0) {
        assert(list_empty(&futex->queue));
        list_remove(&futex->chain);
        free(futex);
    }
}

// Must be called on the result of futex_get when you're done with it
// Also has an unlocked version, for releasing the result of futex_get_unlocked
static void futex_put(struct futex *futex) {
    futex_put_unlocked(futex);
    unlock(&futex_lock);
}

static int futex_load(struct futex *futex, dword_t *out) {
    assert(futex->mem == current->mem);
    read_wrlock(&current->mem->lock);
    dword_t *ptr = mem_ptr(current->mem, futex->addr, MEM_READ);
    read_wrunlock(&current->mem->lock);
    if (ptr == NULL)
        return 1;
    *out = *ptr;
    return 0;
}

static int futex_wait(addr_t uaddr, dword_t val, struct timespec *timeout) {
    struct futex *futex = futex_get(uaddr);
    int err = 0;
    dword_t tmp;
    if (futex_load(futex, &tmp))
        err = _EFAULT;
    else if (tmp != val)
        err = _EAGAIN;
    else {
        struct futex_wait wait;
        // cond_init, not COND_INITIALIZER: on Linux hosts timed waits are
        // measured on the monotonic clock, which only cond_init selects.
        // With the static initializer every timed wait expired at once.
        cond_init(&wait.cond);
        wait.futex = futex;
        list_add_tail(&futex->queue, &wait.queue);
        err = wait_for(&wait.cond, &futex_lock, timeout);
        futex = wait.futex;
        list_remove_safe(&wait.queue);
        cond_destroy(&wait.cond);
    }
    futex_put(futex);
    STRACE("%d end futex(FUTEX_WAIT)", current->pid);
    return err;
}

static int futex_wakelike(int op, addr_t uaddr, dword_t wake_max, dword_t requeue_max, addr_t requeue_addr) {
    struct futex *futex = futex_get(uaddr);

    struct futex_wait *wait, *tmp;
    unsigned woken = 0;
    list_for_each_entry_safe(&futex->queue, wait, tmp, queue) {
        if (woken >= wake_max)
            break;
        notify(&wait->cond);
        list_remove(&wait->queue);
        woken++;
    }

    if (op == FUTEX_REQUEUE_) {
        struct futex *futex2 = futex_get_unlocked(requeue_addr);
        unsigned requeued = 0;
        list_for_each_entry_safe(&futex->queue, wait, tmp, queue) {
            if (requeued >= requeue_max)
                break;
            // sketchy as hell
            list_remove(&wait->queue);
            list_add_tail(&futex2->queue, &wait->queue);
            assert(futex->refcount > 1); // should be true because this function keeps a reference
            futex->refcount--;
            futex2->refcount++;
            wait->futex = futex2;
            requeued++;
        }
        futex_put_unlocked(futex2);
        woken += requeued;
    }

    futex_put(futex);
    return woken;
}

int futex_wake(addr_t uaddr, dword_t wake_max) {
    return futex_wakelike(FUTEX_WAKE_, uaddr, wake_max, 0, 0);
}

dword_t sys_futex(addr_t uaddr, dword_t op, dword_t val, addr_t timeout_or_val2, addr_t uaddr2, dword_t val3) {
    if (!(op & FUTEX_PRIVATE_FLAG_)) {
        STRACE("!FUTEX_PRIVATE ");
    }
    struct timespec timeout = {0};
    int cmd = op & FUTEX_CMD_MASK_;
    if ((cmd == FUTEX_WAIT_ || cmd == FUTEX_WAIT_BITSET_) && timeout_or_val2) {
        struct timespec_ timeout_;
        if (user_get(timeout_or_val2, timeout_))
            return _EFAULT;
        timeout.tv_sec = timeout_.sec;
        timeout.tv_nsec = timeout_.nsec;
    }
    switch (cmd) {
        case FUTEX_WAIT_BITSET_: {
            // like FUTEX_WAIT, but the timeout is a point in time. The bit
            // mask (val3) is not implemented: every wake wakes every waiter.
            STRACE("futex(FUTEX_WAIT_BITSET, %#x, %d, 0x%x) = ...\n", uaddr, val, timeout_or_val2);
            if (val3 == 0)
                return _EINVAL;
            if (timeout_or_val2) {
                struct timespec now;
                clock_gettime((op & FUTEX_CLOCK_REALTIME_) ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
                timeout = timespec_subtract(timeout, now);
                if (!timespec_positive(timeout))
                    return _ETIMEDOUT;
            }
            return futex_wait(uaddr, val, timeout_or_val2 ? &timeout : NULL);
        }
        case FUTEX_WAKE_BITSET_:
            STRACE("futex(FUTEX_WAKE_BITSET, %#x, %d)", uaddr, val);
            return futex_wakelike(FUTEX_WAKE_, uaddr, val, 0, 0);
        case FUTEX_CMP_REQUEUE_: {
            STRACE("futex(FUTEX_CMP_REQUEUE, %#x, %d, %#x, %d)", uaddr, val, uaddr2, val3);
            dword_t now;
            if (user_get(uaddr, now))
                return _EFAULT;
            if (now != val3)
                return _EAGAIN;
            return futex_wakelike(FUTEX_REQUEUE_, uaddr, val, timeout_or_val2, uaddr2);
        }
        case FUTEX_WAIT_:
            STRACE("futex(FUTEX_WAIT, %#x, %d, 0x%x {%ds %dns}) = ...\n", uaddr, val, timeout_or_val2, timeout.tv_sec, timeout.tv_nsec);
            return futex_wait(uaddr, val, timeout_or_val2 ? &timeout : NULL);
        case FUTEX_WAKE_:
            STRACE("futex(FUTEX_WAKE, %#x, %d)", uaddr, val);
            return futex_wakelike(FUTEX_WAKE_, uaddr, val, 0, 0);
        case FUTEX_REQUEUE_:
            STRACE("futex(FUTEX_REQUEUE, %#x, %d, %#x)", uaddr, val, uaddr2);
            return futex_wakelike(FUTEX_REQUEUE_, uaddr, val, timeout_or_val2, uaddr2);
    }
    STRACE("futex(%#x, %d, %d, timeout=%#x, %#x, %d) ", uaddr, op, val, timeout_or_val2, uaddr2, val3);
    FIXME("unsupported futex operation %d", op);
    return _ENOSYS;
}

struct robust_list_head_ {
    addr_t list;
    dword_t offset;
    addr_t list_op_pending;
};

int_t sys_set_robust_list(addr_t robust_list, dword_t len) {
    STRACE("set_robust_list(%#x, %d)", robust_list, len);
    if (len != sizeof(struct robust_list_head_))
        return _EINVAL;
    current->robust_list = robust_list;
    return 0;
}

int_t sys_get_robust_list(pid_t_ pid, addr_t robust_list_ptr, addr_t len_ptr) {
    STRACE("get_robust_list(%d, %#x, %#x)", pid, robust_list_ptr, len_ptr);

    lock(&pids_lock);
    struct task *task = pid_get_task(pid);
    unlock(&pids_lock);
    if (task != current)
        return _EPERM;

    if (user_put(robust_list_ptr, current->robust_list))
        return _EFAULT;
    if (user_put(len_ptr, (int[]) {sizeof(struct robust_list_head_)}))
        return _EFAULT;
    return 0;
}
