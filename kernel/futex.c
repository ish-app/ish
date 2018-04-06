#include "kernel/calls.h"

struct futex {
    atomic_uint refcount;
    struct mem *mem;
    addr_t addr;
    lock_t lock;
    pthread_cond_t cond;
    struct list chain; // locked by futex_hash_lock
};

#define FUTEX_HASH_BITS 12
#define FUTEX_HASH_SIZE (1 << FUTEX_HASH_BITS)
static lock_t futex_hash_lock;
static struct list futex_hash[FUTEX_HASH_SIZE];

static void __attribute__((constructor)) init_futex_hash() {
    for (int i = 0; i < FUTEX_HASH_SIZE; i++)
        list_init(&futex_hash[i]);
}

// returns the futex for the current process at the given addr, and locks it
static struct futex *futex_get(addr_t addr) {
    int hash = (addr ^ (unsigned long) current->cpu.mem) % FUTEX_HASH_SIZE;
    lock(&futex_hash_lock);
    struct list *bucket = &futex_hash[hash];
    struct futex *futex;
    list_for_each_entry(bucket, futex, chain) {
        if (futex->addr == addr) {
            futex->refcount++;
            goto have_futex;
        }
    }
    
    futex = malloc(sizeof(struct futex));
    if (futex == NULL)
        return NULL;
    futex->refcount = 0;
    futex->mem = current->cpu.mem;
    futex->addr = addr;
    lock_init(&futex->lock);
    pthread_cond_init(&futex->cond, NULL);
    list_add(bucket, &futex->chain);

have_futex:
    lock(&futex->lock);
    unlock(&futex_hash_lock);
    return futex;
}

// must be called on the result of futex_get when you're done with it
static void futex_put(struct futex *futex) {
    unlock(&futex->lock);
    if (--futex->refcount == 0) {
        lock(&futex_hash_lock);
        list_remove(&futex->chain);
        unlock(&futex_hash_lock);
    }
}

static int futex_load(struct futex *futex, dword_t *out) {
    dword_t *ptr = mem_ptr(futex->mem, futex->addr, MEM_READ);
    if (ptr == NULL)
        return 1;
    *out = *ptr;
    return 0;
}

int futex_wait(addr_t uaddr, dword_t val) {
    struct futex *futex = futex_get(uaddr);
    int err = 0;
    dword_t tmp;
    if (futex_load(futex, &tmp))
        err = _EFAULT;
    else if (tmp != val)
        err = _EAGAIN;
    else
        pthread_cond_wait(&futex->cond, &futex->lock);
    futex_put(futex);
    return err;
}

int futex_wake(addr_t uaddr, dword_t val) {
    struct futex *futex = futex_get(uaddr);
    if (val == 1)
        pthread_cond_signal(&futex->cond);
    else if (val != 0x7fffffff)
        pthread_cond_broadcast(&futex->cond);
    else
        TODO("invalid number of futex wakes %d", val);
    futex_put(futex);
    return val; // FIXME wrong if val is INT_MAX
}

#define FUTEX_WAIT_ 0
#define FUTEX_WAKE_ 1
#define FUTEX_PRIVATE_FLAG_ 128
#define FUTEX_CMD_MASK_ ~(FUTEX_PRIVATE_FLAG_)

dword_t sys_futex(addr_t uaddr, dword_t op, dword_t val) {
    if (!(op & FUTEX_PRIVATE_FLAG_)) {
        FIXME("no support for shared futexes");
        return _ENOSYS;
    }
    switch (op & FUTEX_CMD_MASK_) {
        case FUTEX_WAIT_:
            STRACE("futex_wait(0x%x, %d)", uaddr, val);
            return futex_wait(uaddr, val);
        case FUTEX_WAKE_:
            STRACE("futex_wake(0x%x, %d)", uaddr, val);
            return futex_wake(uaddr, val);
    }
    FIXME("unsupported futex operation %d", op);
    return _ENOSYS;
}
