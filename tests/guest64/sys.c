// System calls added for the 64-bit guest. Every line must end in OK, natively and in ish.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <utime.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128

#define CHECK(name, cond) printf("%-36s %s\n", name, (cond) ? "OK" : "FAIL")
static double now(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static volatile int alarms, rt_hits;
static void on_alarm(int s) { (void) s; alarms++; }
static void on_rt(int s, siginfo_t *i, void *c) { (void) s; (void) c; if (i->si_value.sival_int == 1234) rt_hits++; }

static int futex_word; static volatile int waiter_result;
static void *futex_waiter(void *arg) { (void) arg; waiter_result = syscall(SYS_futex, &futex_word, FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG, 0, NULL, NULL, 0xffffffff); return NULL; }
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv = PTHREAD_COND_INITIALIZER; static int ready;
static void *signaller(void *arg) { (void) arg; usleep(50000); pthread_mutex_lock(&m); ready = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&m); return NULL; }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[] = "/tmp/systest.dat";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);

    // vectored I/O at an offset
    struct iovec w[2] = {{"hello ", 6}, {"world!", 6}};
    CHECK("pwritev", pwritev(fd, w, 2, 100) == 12);
    char a[8] = {0}, b[8] = {0};
    struct iovec r[2] = {{a, 6}, {b, 6}};
    CHECK("preadv", preadv(fd, r, 2, 100) == 12 && memcmp(a, "hello ", 6) == 0 && memcmp(b, "world!", 6) == 0);
    CHECK("file position untouched", lseek(fd, 0, SEEK_CUR) == 0);

    // fallocate
    struct stat st;
    CHECK("fallocate grows the file", fallocate(fd, 0, 0, 5000) == 0 && fstat(fd, &st) == 0 && st.st_size == 5000);
    CHECK("posix_fallocate", posix_fallocate(fd, 4096, 8192) == 0 && fstat(fd, &st) == 0 && st.st_size == 12288);

    // record locks: 64-bit struct flock
    struct flock fl = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 1000, .l_len = 50};
    CHECK("F_SETLK", fcntl(fd, F_SETLK, &fl) == 0);
    pid_t pid = fork();
    if (pid == 0) {
        int fd2 = open(path, O_RDWR);
        struct flock q = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 1020, .l_len = 10};
        int ok = fcntl(fd2, F_GETLK, &q) == 0 && q.l_type == F_WRLCK && q.l_start == 1000 && q.l_len == 50 && q.l_pid == getppid();
        struct flock t = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 1020, .l_len = 10};
        ok = ok && fcntl(fd2, F_SETLK, &t) == -1 && (errno == EAGAIN || errno == EACCES);
        struct flock free_ = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 3000, .l_len = 10};
        ok = ok && fcntl(fd2, F_SETLK, &free_) == 0;
        // blocks until the parent unlocks
        double t0 = now(CLOCK_MONOTONIC);
        ok = ok && fcntl(fd2, F_SETLKW, &t) == 0 && now(CLOCK_MONOTONIC) - t0 > 0.15;
        _exit(ok ? 7 : 1);
    }
    usleep(300000);
    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    siginfo_t info; memset(&info, 0, sizeof info);
    CHECK("waitid", waitid(P_PID, pid, &info, WEXITED) == 0 && info.si_pid == pid && info.si_code == CLD_EXITED);
    CHECK("F_GETLK, conflict, F_SETLKW", info.si_status == 7);

    // interval timer
    signal(SIGALRM, on_alarm);
    struct itimerval it = {{0, 0}, {0, 300000}}, cur;
    setitimer(ITIMER_REAL, &it, NULL);
    CHECK("getitimer shows time left", getitimer(ITIMER_REAL, &cur) == 0 && cur.it_value.tv_sec == 0 && cur.it_value.tv_usec > 100000 && cur.it_value.tv_usec <= 300000);
    pause();
    CHECK("SIGALRM from setitimer", alarms == 1);
    CHECK("getitimer after expiry is zero", getitimer(ITIMER_REAL, &cur) == 0 && cur.it_value.tv_sec == 0 && cur.it_value.tv_usec == 0);

    // POSIX timers
    timer_t tid;
    CHECK("timer_create(NULL sigevent)", timer_create(CLOCK_MONOTONIC, NULL, &tid) == 0);
    struct itimerspec ts = {{0, 0}, {0, 100000000}}, left;
    timer_settime(tid, 0, &ts, NULL);
    CHECK("timer_gettime", timer_gettime(tid, &left) == 0 && left.it_value.tv_nsec > 0 && left.it_value.tv_nsec <= 100000000);
    pause();
    CHECK("SIGALRM from POSIX timer", alarms == 2);
    CHECK("timer_delete", timer_delete(tid) == 0);
    struct sigaction sa = {0}; sa.sa_sigaction = on_rt; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGRTMIN, &sa, NULL);
    struct sigevent sev = {0}; sev.sigev_notify = SIGEV_SIGNAL; sev.sigev_signo = SIGRTMIN; sev.sigev_value.sival_int = 1234;
    timer_create(CLOCK_REALTIME, &sev, &tid);
    ts.it_value.tv_nsec = 50000000;
    timer_settime(tid, 0, &ts, NULL);
    pause();
    CHECK("timer signal carries sigev_value", rt_hits == 1);

    // timerfd
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec tspec = {{0, 0}, {0, 80000000}};
    timerfd_settime(tfd, 0, &tspec, NULL);
    CHECK("timerfd_gettime", timerfd_gettime(tfd, &left) == 0 && left.it_value.tv_nsec > 0 && left.it_value.tv_nsec <= 80000000);
    uint64_t expirations = 0;
    CHECK("timerfd read", read(tfd, &expirations, 8) == 8 && expirations == 1);

    // clock_nanosleep
    double t0 = now(CLOCK_MONOTONIC);
    struct timespec rel = {0, 120000000};
    CHECK("clock_nanosleep relative", clock_nanosleep(CLOCK_MONOTONIC, 0, &rel, NULL) == 0 && now(CLOCK_MONOTONIC) - t0 >= 0.119);
    struct timespec abs; clock_gettime(CLOCK_REALTIME, &abs);
    abs.tv_nsec += 150000000; if (abs.tv_nsec >= 1000000000) { abs.tv_nsec -= 1000000000; abs.tv_sec++; }
    t0 = now(CLOCK_MONOTONIC);
    CHECK("clock_nanosleep absolute", clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &abs, NULL) == 0 && now(CLOCK_MONOTONIC) - t0 >= 0.14 && now(CLOCK_MONOTONIC) - t0 < 1.0);
    abs.tv_sec -= 10;
    CHECK("absolute time in the past returns", clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &abs, NULL) == 0);

    // accept4
    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sun = {.sun_family = AF_UNIX}; strcpy(sun.sun_path, "/tmp/systest.sock"); unlink(sun.sun_path);
    bind(ls, (struct sockaddr *) &sun, sizeof sun); listen(ls, 1);
    int cs = socket(AF_UNIX, SOCK_STREAM, 0);
    connect(cs, (struct sockaddr *) &sun, sizeof sun);
    int as = accept4(ls, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    CHECK("accept4", as >= 0);
    CHECK("accept4 SOCK_NONBLOCK", as >= 0 && (fcntl(as, F_GETFL) & O_NONBLOCK));
    CHECK("accept4 SOCK_CLOEXEC", as >= 0 && (fcntl(as, F_GETFD) & FD_CLOEXEC));
    char c;
    CHECK("nonblocking read gives EAGAIN", as >= 0 && read(as, &c, 1) == -1 && errno == EAGAIN);
    write(cs, "x", 1);
    CHECK("data arrives", as >= 0 && read(as, &c, 1) == 1 && c == 'x');

    // old-style getdents
    int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);
    char dbuf[4096]; int found = 0;
    long n = syscall(SYS_getdents, dfd, dbuf, sizeof dbuf);
    for (long off = 0; off < n;) {
        struct { unsigned long ino; unsigned long off; unsigned short reclen; char name[]; } *d = (void *) (dbuf + off);
        if (strcmp(d->name, "systest.dat") == 0) found = 1;
        off += d->reclen;
    }
    CHECK("getdents (long-sized entries)", n > 0 && found);

    // futex: wait with an absolute timeout, wake, and a condition variable
    struct timespec deadline; clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += 100000000; if (deadline.tv_nsec >= 1000000000) { deadline.tv_nsec -= 1000000000; deadline.tv_sec++; }
    t0 = now(CLOCK_MONOTONIC);
    long fr = syscall(SYS_futex, &futex_word, FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG, 0, &deadline, NULL, 0xffffffff);
    CHECK("FUTEX_WAIT_BITSET times out", fr == -1 && errno == ETIMEDOUT && now(CLOCK_MONOTONIC) - t0 >= 0.09 && now(CLOCK_MONOTONIC) - t0 < 1.0);
    pthread_t th; pthread_create(&th, NULL, futex_waiter, NULL);
    usleep(100000);
    CHECK("FUTEX_WAKE_BITSET wakes one", syscall(SYS_futex, &futex_word, FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG, 1, NULL, NULL, 0xffffffff) == 1);
    pthread_join(th, NULL);
    CHECK("waiter returned 0", waiter_result == 0);
    pthread_create(&th, NULL, signaller, NULL);
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 5;
    pthread_mutex_lock(&m); int rc = 0;
    while (!ready && rc == 0) rc = pthread_cond_timedwait(&cv, &m, &until);
    pthread_mutex_unlock(&m); pthread_join(th, NULL);
    CHECK("pthread_cond_timedwait signalled", rc == 0 && ready);
    clock_gettime(CLOCK_REALTIME, &until); until.tv_nsec += 50000000; if (until.tv_nsec >= 1000000000) { until.tv_nsec -= 1000000000; until.tv_sec++; }
    pthread_mutex_lock(&m); rc = pthread_cond_timedwait(&cv, &m, &until); pthread_mutex_unlock(&m);
    CHECK("pthread_cond_timedwait times out", rc == ETIMEDOUT);

    // small ones
    struct utimbuf ut = {1000000000, 1100000000};
    CHECK("utime", utime(path, &ut) == 0 && stat(path, &st) == 0 && st.st_mtime == 1100000000 && st.st_atime == 1000000000);
    char name[16] = {0};
    CHECK("prctl set/get name", prctl(PR_SET_NAME, "sysname") == 0 && prctl(PR_GET_NAME, name) == 0 && strcmp(name, "sysname") == 0);
    unsigned cpu = 99;
    CHECK("getcpu", syscall(SYS_getcpu, &cpu, NULL, NULL) == 0 && cpu < 1024);
    CHECK("sched_get_priority_min/max", sched_get_priority_max(SCHED_OTHER) >= sched_get_priority_min(SCHED_OTHER));
    CHECK("faccessat2", syscall(439, AT_FDCWD, path, R_OK, 0) == 0 && syscall(439, AT_FDCWD, "/nonexistent", R_OK, 0) == -1);
    int d1 = dup(fd), d2 = dup(fd), d3 = dup(fd);
    int lo = d1 < d2 ? d1 : d2, hi = d3 > d2 ? d3 : d2;
    long cr = syscall(436, lo, hi, 0);
    CHECK("close_range", cr == 0 && fcntl(d1, F_GETFD) == -1 && fcntl(d3, F_GETFD) == -1 && fcntl(fd, F_GETFD) != -1);
    CHECK("sync/fsync/fdatasync", (sync(), 1) && fsync(fd) == 0 && fdatasync(fd) == 0);
    CHECK("posix_fadvise", posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL) == 0);
    {
        // MADV_DONTNEED: private anonymous pages read as zeros afterwards, neighbors are kept
        size_t psz = 4096, n = 64;
        unsigned char *m = mmap(NULL, n * psz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(m, 0xa5, n * psz);
        int r = madvise(m + 3 * psz, 37 * psz, MADV_DONTNEED);
        int good = r == 0;
        for (size_t i = 0; i < n * psz; i++) {
            unsigned char want = (i >= 3 * psz && i < 40 * psz) ? 0 : 0xa5;
            if (m[i] != want) { good = 0; break; }
        }
        m[5 * psz] = 7;
        CHECK("madvise DONTNEED zeroes", good && m[5 * psz] == 7 && m[5 * psz + 1] == 0);

        // after a fork: the child discards, the parent keeps its contents
        memset(m, 0x5a, n * psz);
        pid_t kid = fork();
        if (kid == 0) {
            madvise(m, n * psz, MADV_DONTNEED);
            _exit(m[0] == 0 && m[n * psz - 1] == 0 ? 0 : 1);
        }
        int st = -1;
        waitpid(kid, &st, 0);
        CHECK("madvise DONTNEED after fork", st == 0 && m[0] == 0x5a && m[n * psz - 1] == 0x5a);

        // mprotect in the child must not undo copy-on-write
        kid = fork();
        if (kid == 0) {
            mprotect(m, psz, PROT_READ);
            mprotect(m, psz, PROT_READ | PROT_WRITE);
            m[0] = 1;
            _exit(0);
        }
        waitpid(kid, &st, 0);
        CHECK("mprotect keeps copy-on-write", st == 0 && m[0] == 0x5a);

        // mprotect must not turn a shared mapping private
        unsigned char *sh = mmap(NULL, psz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        mprotect(sh, psz, PROT_READ);
        mprotect(sh, psz, PROT_READ | PROT_WRITE);
        kid = fork();
        if (kid == 0) { sh[0] = 9; _exit(0); }
        waitpid(kid, &st, 0);
        CHECK("mprotect keeps a shared mapping", st == 0 && sh[0] == 9);
        munmap(m, n * psz); munmap(sh, psz);
    }
    unlink(path); unlink(sun.sun_path);
    return 0;
}
