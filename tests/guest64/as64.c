// Address space above 4 GB: every line must end in OK.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <fcntl.h>

#define CHECK(name, cond) printf("%-34s %s\n", name, (cond) ? "OK" : "FAIL")
#define GB (1024ul * 1024 * 1024)

static sigjmp_buf jb;
static volatile uintptr_t fault_addr;
static void on_segv(int sig, siginfo_t *info, void *ctx) { (void) sig; (void) ctx; fault_addr = (uintptr_t) info->si_addr; siglongjmp(jb, 1); }

static pthread_mutex_t *mtx; static unsigned long *shared_counter;
static void *locker(void *arg) { (void) arg; for (int i = 0; i < 50000; i++) { pthread_mutex_lock(mtx); (*shared_counter)++; pthread_mutex_unlock(mtx); } return NULL; }

int main(void) {
    // 1. one mapping larger than 4 GB
    size_t big = 6 * GB;
    unsigned char *p = mmap(NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    CHECK("mmap 6 GB", p != MAP_FAILED);
    if (p == MAP_FAILED) { printf("errno %d\n", errno); return 1; }
    CHECK("6 GB mapping ends above 4 GB", (uintptr_t) p + big > 4 * GB);
    p[0] = 1; p[big / 2] = 2; p[big - 1] = 3;
    uint64_t *q = (uint64_t *) (p + 5 * GB + 4093); // unaligned, crosses a page
    *q = 0x1122334455667788ull;
    CHECK("read back at 0, 3 GB, 6 GB", p[0] == 1 && p[big / 2] == 2 && p[big - 1] == 3 && *q == 0x1122334455667788ull && p[4 * GB] == 0);
    unsigned char *high = p + 5 * GB;

    // 2. hint and fixed addresses
    void *hint = (void *) 0xc000000000ul;
    void *h = mmap(hint, 1 << 26, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK("hint 0xc000000000 honored", h == hint);
    CHECK("mprotect high region", mprotect(h, 1 << 20, PROT_READ | PROT_WRITE) == 0 && (((char *) h)[4096] = 5) == 5);
    void *f = mmap((void *) 0x70000000000ul, 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    CHECK("MAP_FIXED at 7 TB", f == (void *) 0x70000000000ul && (*(int *) f = 7) == 7);
    void *span = mmap((void *) 0x100000000ul - 8192, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (span != MAP_FAILED) {
        uint64_t *b = (uint64_t *) (0x100000000ul - 4);
        *b = 0xa1a2a3a4b1b2b3b4ull;
        memmove((char *) 0x100000000ul - 100, (char *) 0x100000000ul - 4, 8);
        CHECK("value across the 4 GB line", *b == 0xa1a2a3a4b1b2b3b4ull && *(uint64_t *) (0x100000000ul - 100) == *b);
    } else CHECK("map across the 4 GB line", 0);
    CHECK("munmap part of big mapping", munmap(p + GB, GB) == 0);

    // 3. file mapping high, syscalls with high buffers
    int fd = open("/tmp/as64.dat", O_RDWR | O_CREAT | O_TRUNC, 0600);
    const char msg[] = "file data through a high buffer";
    memcpy(high, msg, sizeof msg);
    CHECK("write from high buffer", write(fd, high, sizeof msg) == sizeof msg);
    char *fm = mmap((void *) 0x20000000000ul, 4096, PROT_READ, MAP_SHARED | MAP_FIXED, fd, 0);
    CHECK("file mapped at 2 TB", fm == (void *) 0x20000000000ul && memcmp(fm, msg, sizeof msg) == 0);
    CHECK("pread into high buffer", pread(fd, high + 100, sizeof msg, 0) == sizeof msg && memcmp(high + 100, msg, sizeof msg) == 0);
    struct iovec iov[2] = {{high + 200, 5}, {high + 300, 26}};
    CHECK("preadv into high buffers", preadv(fd, iov, 2, 0) == 31 && memcmp(high + 300, msg + 5, 26) == 0);
    int pipefd[2]; pipe(pipefd);
    write(pipefd[1], high, 10);
    int *navail = (int *) (high + 400);
    CHECK("ioctl FIONREAD, high pointer", ioctl(pipefd[0], FIONREAD, navail) == 0 && *navail == 10);
    int sv[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    socklen_t *len = (socklen_t *) (high + 500); int *val = (int *) (high + 600); *len = sizeof(int);
    CHECK("getsockopt, high pointers", getsockopt(sv[0], SOL_SOCKET, SO_TYPE, val, len) == 0 && *val == SOCK_STREAM);
    struct flock *fl = (struct flock *) (high + 700);
    memset(fl, 0, sizeof *fl); fl->l_type = F_WRLCK; fl->l_whence = SEEK_SET; fl->l_start = 0; fl->l_len = 10;
    CHECK("fcntl F_SETLK, high pointer", fcntl(fd, F_SETLK, fl) == 0);
    char *cwd = (char *) high + 1000;
    CHECK("getcwd into high buffer", getcwd(cwd, 512) == cwd && cwd[0] == '/');

    // 4. fork: copy-on-write of high memory
    high[0] = 'P';
    pid_t pid = fork();
    if (pid == 0) { high[0] = 'C'; _exit(high[0] == 'C' && p[big - 1] == 3 ? 42 : 1); }
    int status; waitpid(pid, &status, 0);
    CHECK("fork: child saw and changed copy", WIFEXITED(status) && WEXITSTATUS(status) == 42);
    CHECK("fork: parent copy untouched", high[0] == 'P');

    // 5. threads contending on a mutex that lives above 4 GB (futex on a high address)
    mtx = (pthread_mutex_t *) (high + 8192); shared_counter = (unsigned long *) (high + 12288);
    pthread_mutex_init(mtx, NULL);
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, locker, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    CHECK("mutex above 4 GB, 4 threads", *shared_counter == 200000);

    // 6. faults report the full address
    struct sigaction sa = {0}; sa.sa_sigaction = on_segv; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    volatile char *bad = (volatile char *) 0x123456789000ul;
    if (sigsetjmp(jb, 1) == 0) { *bad = 1; CHECK("fault on unmapped high address", 0); }
    else CHECK("SIGSEGV si_addr is the full address", fault_addr == 0x123456789000ul);
    bad = (volatile char *) (p + GB + 4096); // the part unmapped above
    if (sigsetjmp(jb, 1) == 0) { *bad = 1; CHECK("fault in unmapped hole", 0); }
    else CHECK("fault inside the munmapped hole", fault_addr == (uintptr_t) (p + GB + 4096));
    void *toohigh = mmap((void *) 0x700000000000ul, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    printf("%-34s %s\n", "MAP_FIXED at 112 TB", toohigh == MAP_FAILED ? "refused (ish limit is 8 TB)" : "mapped");

    // 7. /proc/self/maps shows a high mapping
    FILE *maps = fopen("/proc/self/maps", "r"); char line[512]; int found = 0;
    while (maps && fgets(line, sizeof line, maps)) if (strncmp(line, "c000000000-", 11) == 0) found = 1;
    CHECK("/proc/self/maps lists c000000000", found);
    return 0;
}
