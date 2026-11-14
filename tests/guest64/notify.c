// inotify, pidfd and /proc/self/statm. The output must be the same natively and in ish.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/socket.h>

static int ifd;
static const char *label[16];
static uint32_t last_cookie;

static void drain(const char *what) {
    printf("-- %s\n", what);
    char buf[8192];
    for (;;) {
        ssize_t n = read(ifd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN) printf("   read error %s\n", strerror(errno));
            break;
        }
        for (char *p = buf; p < buf + n;) {
            struct inotify_event *e = (struct inotify_event *) p;
            printf("   %-5s", e->wd >= 0 && e->wd < 16 && label[e->wd] ? label[e->wd] : "?");
            static const struct { uint32_t bit; const char *name; } names[] = {
                {IN_ACCESS, "ACCESS"}, {IN_MODIFY, "MODIFY"}, {IN_ATTRIB, "ATTRIB"}, {IN_CLOSE_WRITE, "CLOSE_WRITE"},
                {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"}, {IN_OPEN, "OPEN"}, {IN_MOVED_FROM, "MOVED_FROM"}, {IN_MOVED_TO, "MOVED_TO"},
                {IN_CREATE, "CREATE"}, {IN_DELETE, "DELETE"}, {IN_DELETE_SELF, "DELETE_SELF"}, {IN_MOVE_SELF, "MOVE_SELF"},
                {IN_Q_OVERFLOW, "Q_OVERFLOW"}, {IN_IGNORED, "IGNORED"}, {IN_ISDIR, "ISDIR"},
            };
            for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
                if (e->mask & names[i].bit) printf(" %s", names[i].name);
            if (e->len) printf(" name=%s", e->name);
            if (e->cookie) {
                printf(" cookie=%s", e->cookie == last_cookie ? "same" : "new");
                last_cookie = e->cookie;
            }
            printf("\n");
            p += sizeof(*e) + e->len;
        }
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char dir[] = "/tmp/notifyXXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    if (chdir(dir)) return 1;
    mkdir("sub", 0755);

    ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    printf("inotify_init1 %s\n", ifd >= 0 ? "ok" : strerror(errno));
    char one;
    printf("read with nothing queued: %s\n", read(ifd, &one, 1) < 0 && errno == EAGAIN ? "EAGAIN" : "unexpected");
    struct pollfd pfd = {ifd, POLLIN, 0};
    printf("poll with nothing queued: %d\n", poll(&pfd, 1, 0));

    int wdir = inotify_add_watch(ifd, ".", IN_ALL_EVENTS & ~IN_ACCESS);
    label[wdir] = "dir";
    int wsub = inotify_add_watch(ifd, "sub", IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF);
    label[wsub] = "sub";
    printf("same path gives the same watch: %d\n", inotify_add_watch(ifd, dir, IN_ALL_EVENTS & ~IN_ACCESS) == wdir);
    printf("missing path: %s\n", inotify_add_watch(ifd, "nope", IN_MODIFY) < 0 ? strerror(errno) : "unexpected");

    int fd = open("a.txt", O_CREAT | O_WRONLY, 0644);
    drain("create and open a.txt");
    printf("poll after drain: %d\n", poll(&pfd, 1, 0));
    if (write(fd, "hello", 5) != 5) return 1;
    printf("poll after a write: %d\n", poll(&pfd, 1, 1000));
    drain("write");
    close(fd);
    drain("close after writing");

    printf("ONLYDIR on a file: %s\n", inotify_add_watch(ifd, "a.txt", IN_MODIFY | IN_ONLYDIR) < 0 ? strerror(errno) : "unexpected");
    int wfile = inotify_add_watch(ifd, "a.txt", IN_ALL_EVENTS & ~IN_ACCESS);
    label[wfile] = "file";

    fd = open("a.txt", O_RDONLY);
    close(fd);
    drain("open read-only and close");
    chmod("a.txt", 0600);
    drain("chmod");
    if (truncate("a.txt", 2)) return 1;
    drain("truncate");
    fd = open("a.txt", O_WRONLY | O_TRUNC);
    close(fd);
    drain("open with O_TRUNC and close");

    rename("a.txt", "b.txt");
    drain("rename inside the directory");
    fd = open("b.txt", O_WRONLY | O_APPEND);
    if (write(fd, "x", 1) != 1) return 1;
    close(fd);
    drain("the file watch followed the rename");
    rename("b.txt", "sub/c.txt");
    drain("rename into sub");
    if (symlink("sub/c.txt", "link")) return 1;
    drain("symlink");
    if (link("sub/c.txt", "hard")) return 1;
    drain("hard link");
    unlink("hard"); unlink("link");
    drain("unlink both links");
    mkdir("d2", 0755);
    drain("mkdir");
    rename("d2", "d3");
    drain("rename a directory");
    rmdir("d3");
    drain("rmdir");

    printf("rm_watch: %d\n", inotify_rm_watch(ifd, wfile));
    drain("after rm_watch");
    printf("rm_watch again: %s\n", inotify_rm_watch(ifd, wfile) < 0 ? strerror(errno) : "unexpected");

    int wone = inotify_add_watch(ifd, "sub/c.txt", IN_MODIFY | IN_ONESHOT);
    label[wone] = "once";
    fd = open("sub/c.txt", O_WRONLY | O_APPEND);
    if (write(fd, "1", 1) != 1) return 1;
    drain("oneshot: first write");
    if (write(fd, "2", 1) != 1) return 1;
    close(fd);
    drain("oneshot: second write");

    int wlast = inotify_add_watch(ifd, "sub/c.txt", IN_ALL_EVENTS & ~IN_ACCESS);
    label[wlast] = "last";
    unlink("sub/c.txt");
    drain("unlink of a watched file in sub");
    rmdir("sub");
    drain("rmdir of the watched sub");

    // a blocking read is woken by an event from another process
    fcntl(ifd, F_SETFL, fcntl(ifd, F_GETFL) & ~O_NONBLOCK);
    pid_t kid = fork();
    if (kid == 0) { usleep(100000); close(open("late", O_CREAT | O_WRONLY, 0644)); _exit(0); }
    char buf[4096];
    ssize_t n = read(ifd, buf, sizeof(buf));
    printf("blocking read returned an event: %d\n", n >= (ssize_t) sizeof(struct inotify_event));
    waitpid(kid, NULL, 0);
    fcntl(ifd, F_SETFL, fcntl(ifd, F_GETFL) | O_NONBLOCK);
    while (read(ifd, buf, sizeof(buf)) > 0);
    unlink("late");
    while (read(ifd, buf, sizeof(buf)) > 0);
    close(ifd);

    // pidfd
    kid = fork();
    if (kid == 0) { usleep(200000); _exit(7); }
    int pfdn = (int) syscall(SYS_pidfd_open, kid, 0);
    printf("pidfd_open %s, close-on-exec %d\n", pfdn >= 0 ? "ok" : strerror(errno), (fcntl(pfdn, F_GETFD) & FD_CLOEXEC) != 0);
    struct pollfd pp = {pfdn, POLLIN, 0};
    printf("pidfd poll while running: %d\n", poll(&pp, 1, 0));
    int ep = epoll_create1(0);
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = pfdn};
    epoll_ctl(ep, EPOLL_CTL_ADD, pfdn, &ev);
    struct epoll_event got;
    printf("pidfd epoll after exit: %d\n", epoll_wait(ep, &got, 1, 5000));
    printf("pidfd poll after exit: %d\n", poll(&pp, 1, 0));
    siginfo_t si = {0};
    int wr = waitid(P_PIDFD, (id_t) pfdn, &si, WEXITED);
    printf("waitid on the pidfd: %d, exit status %d\n", wr, si.si_status);
    close(pfdn);
    kid = fork();
    if (kid == 0) { pause(); _exit(0); }
    pfdn = (int) syscall(SYS_pidfd_open, kid, 0);
    printf("pidfd_send_signal: %ld\n", syscall(SYS_pidfd_send_signal, pfdn, SIGTERM, NULL, 0));
    int st = 0;
    waitpid(kid, &st, 0);
    printf("killed by the signal: %d\n", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    pp.fd = pfdn;
    printf("pidfd of a reaped process is readable: %d\n", poll(&pp, 1, 0));
    printf("pidfd_send_signal to a dead process: %s\n", syscall(SYS_pidfd_send_signal, pfdn, 0, NULL, 0) < 0 ? strerror(errno) : "unexpected");
    printf("pidfd_open of a missing process: %s\n", syscall(SYS_pidfd_open, 999999, 0) < 0 ? strerror(errno) : "unexpected");
    printf("pidfd_open of pid 0: %s\n", syscall(SYS_pidfd_open, 0, 0) < 0 ? strerror(errno) : "unexpected");
    close(pfdn);

    // a one-shot epoll entry is disarmed by its event, not removed
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) return 1;
    int ep2 = epoll_create1(0);
    struct epoll_event oe = {.events = EPOLLIN | EPOLLONESHOT, .data.fd = sv[0]};
    epoll_ctl(ep2, EPOLL_CTL_ADD, sv[0], &oe);
    if (write(sv[1], "x", 1) != 1) return 1;
    printf("oneshot: first wait %d\n", epoll_wait(ep2, &got, 1, 1000));
    printf("oneshot: second wait %d\n", epoll_wait(ep2, &got, 1, 0));
    printf("oneshot: add again %s\n", epoll_ctl(ep2, EPOLL_CTL_ADD, sv[0], &oe) < 0 ? strerror(errno) : "unexpected");
    printf("oneshot: re-arm %d\n", epoll_ctl(ep2, EPOLL_CTL_MOD, sv[0], &oe));
    printf("oneshot: wait after re-arm %d\n", epoll_wait(ep2, &got, 1, 1000));
    printf("oneshot: delete %d\n", epoll_ctl(ep2, EPOLL_CTL_DEL, sv[0], NULL));

    // close_range with CLOSE_RANGE_CLOEXEC marks instead of closing
    int cr1 = dup(sv[0]), cr2 = dup(sv[0]);
    int lo = cr1 < cr2 ? cr1 : cr2, hi = cr1 < cr2 ? cr2 : cr1;
    long crr = syscall(436, lo, hi, 4);
    printf("close_range CLOEXEC: %ld, still open %d, marked %d %d\n", crr,
            fcntl(cr1, F_GETFL) != -1, (fcntl(cr1, F_GETFD) & FD_CLOEXEC) != 0, (fcntl(cr2, F_GETFD) & FD_CLOEXEC) != 0);
    close(cr1); close(cr2); close(sv[0]); close(sv[1]); close(ep2);

    // statm: resident memory grows when pages are touched and shrinks when they are discarded
    long size0, res0, size1, res1, res2, res3;
    FILE *f = fopen("/proc/self/statm", "r");
    if (fscanf(f, "%ld %ld", &size0, &res0) != 2) return 1;
    fclose(f);
    size_t len = 64 << 20;
    char *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    f = fopen("/proc/self/statm", "r");
    if (fscanf(f, "%ld %ld", &size1, &res1) != 2) return 1;
    fclose(f);
    memset(m, 1, len);
    f = fopen("/proc/self/statm", "r");
    if (fscanf(f, "%*d %ld", &res2) != 1) return 1;
    fclose(f);
    madvise(m, len, MADV_DONTNEED);
    f = fopen("/proc/self/statm", "r");
    if (fscanf(f, "%*d %ld", &res3) != 1) return 1;
    fclose(f);
    long pages = (long) (len / 4096);
    printf("statm is not zero: %d\n", size0 > 0 && res0 > 0 && res0 <= size0);
    printf("statm size grows with a mapping: %d\n", size1 - size0 >= pages && size1 - size0 < pages + 64);
    printf("statm resident unchanged by an untouched mapping: %d\n", res1 - res0 < 64);
    printf("statm resident grows when touched: %d\n", res2 - res1 >= pages - 64 && res2 - res1 < pages + 64);
    printf("statm resident shrinks after MADV_DONTNEED: %d\n", res2 - res3 >= pages - 64);

    if (chdir("/")) return 1;
    rmdir(dir);
    return 0;
}
