// A process with busy and sleeping threads calls execve: the new program must run and exit 0.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/wait.h>
static volatile unsigned long spin;
static int ep;
static void *busy(void *a) { char *p = malloc(1 << 20); for (;;) { memset(p, (int) spin++, 1 << 20); } return a; }
static void *sleeper(void *a) { struct epoll_event e; for (;;) epoll_wait(ep, &e, 1, -1); return a; }
static void *napper(void *a) { for (;;) usleep(1000); return a; }
int main(int argc, char **argv) {
    if (argc > 1) { printf("after exec: %s, threads %d\n", argv[1], 1); return 0; }
    int ok = 0;
    for (int round = 0; round < 20; round++) {
        pid_t kid = fork();
        if (kid == 0) {
            ep = epoll_create1(EPOLL_CLOEXEC);
            pthread_t t;
            for (int i = 0; i < 3; i++) { pthread_create(&t, NULL, busy, NULL); pthread_create(&t, NULL, sleeper, NULL); pthread_create(&t, NULL, napper, NULL); }
            usleep(20000);
            execl("/proc/self/exe", "execthr", "x", NULL);
            _exit(99);
        }
        int st = -1;
        waitpid(kid, &st, 0);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) ok++;
        else printf("round %d: status %#x\n", round, st);
    }
    printf("exec with 9 other threads: %d of 20 ok\n", ok);
    return 0;
}
