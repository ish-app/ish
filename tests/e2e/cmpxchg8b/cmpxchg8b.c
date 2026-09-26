// lock cmpxchg8b on 64-bit values that are only 4-byte aligned, the way the
// i386 ABI lays out uint64_t struct fields: one mid-page and one straddling a
// page boundary, each incremented from several threads at once.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#define THREADS 4
#define ITERS 20000

static void *bump(void *p) {
    for (int i = 0; i < ITERS; i++)
        __atomic_fetch_add((uint64_t *) p, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

static unsigned long long run(char *where) {
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++)
        pthread_create(&threads[i], NULL, bump, where);
    for (int i = 0; i < THREADS; i++)
        pthread_join(threads[i], NULL);
    return *(uint64_t *) where;
}

int main(void) {
    char *pages = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("mid-page: %llu\n", run(pages + 100));
    printf("page boundary: %llu\n", run(pages + 4092));
    return 0;
}
