#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum {
    DUP_THREADS = 4,
    DUP_ITERS = 100000,
};

static int target_fd = -1;
static pthread_barrier_t start_barrier;
static volatile int failed = 0;
static volatile int active_dupers = DUP_THREADS;
static volatile long ebadf_count = 0;
static const char *target_path = "fd_race.c";

static void fail_errno(const char *op) {
    perror(op);
    failed = 1;
}

static int check_duplicate(int fd, const char *op) {
    if (fd < 0) {
        if (errno == EBADF) {
            __sync_fetch_and_add(&ebadf_count, 1);
            fprintf(stderr, "%s saw EBADF while fd %d was being atomically replaced\n", op, target_fd);
        } else {
            perror(op);
        }
        failed = 1;
        return -1;
    }
    if (close(fd) < 0) {
        fail_errno("close(duplicate)");
        return -1;
    }
    return failed ? -1 : 0;
}

static void *replace_thread(void *arg) {
    (void) arg;
    pthread_barrier_wait(&start_barrier);
    while (!failed && __sync_fetch_and_add(&active_dupers, 0) > 0) {
        int temp = open(target_path, O_RDONLY);
        if (temp < 0) {
            fail_errno("open");
            break;
        }
        if (dup2(temp, target_fd) < 0) {
            int err = errno;
            close(temp);
            errno = err;
            fail_errno("dup2");
            break;
        }
        if (close(temp) < 0) {
            fail_errno("close(temp)");
            break;
        }
        sched_yield();
    }
    return NULL;
}

static void *dup_thread(void *arg) {
    long id = (long) arg;
    pthread_barrier_wait(&start_barrier);
    for (int i = 0; i < DUP_ITERS && !failed; i++) {
        errno = 0;
        int duplicate;
        const char *op;
        if (((i + id) & 1) == 0) {
            op = "dup";
            duplicate = dup(target_fd);
        } else {
            op = "fcntl(F_DUPFD_CLOEXEC)";
            duplicate = fcntl(target_fd, F_DUPFD_CLOEXEC, 32);
        }
        if (check_duplicate(duplicate, op) < 0)
            break;
        if ((i & 0xff) == 0)
            sched_yield();
    }
    __sync_sub_and_fetch(&active_dupers, 1);
    return NULL;
}

int main(void) {
    target_fd = open(target_path, O_RDONLY);
    if (target_fd < 0) {
        perror("open target");
        return 1;
    }

    pthread_t replacer;
    pthread_t dupers[DUP_THREADS];
    if (pthread_barrier_init(&start_barrier, NULL, DUP_THREADS + 1) != 0) {
        perror("pthread_barrier_init");
        close(target_fd);
        return 1;
    }

    if (pthread_create(&replacer, NULL, replace_thread, NULL) != 0) {
        perror("pthread_create replacer");
        close(target_fd);
        return 1;
    }
    for (long i = 0; i < DUP_THREADS; i++) {
        if (pthread_create(&dupers[i], NULL, dup_thread, (void *) i) != 0) {
            perror("pthread_create duper");
            failed = 1;
            __sync_sub_and_fetch(&active_dupers, 1);
            break;
        }
    }

    for (int i = 0; i < DUP_THREADS; i++)
        pthread_join(dupers[i], NULL);
    pthread_join(replacer, NULL);
    pthread_barrier_destroy(&start_barrier);
    close(target_fd);

    if (failed) {
        fprintf(stderr, "fd race detected, ebadf_count=%ld\n", ebadf_count);
        return 1;
    }

    printf("ok\n");
    return 0;
}