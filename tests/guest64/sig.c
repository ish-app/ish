// A signal handler that uses x87 and SSE must not disturb the interrupted
// computation: the same sum is computed with and without a 2 ms timer signal.
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <math.h>
static volatile int hits;
static void handler(int sig) {
    (void) sig;
    volatile long double x = 1.5L;
    for (int i = 0; i < 50; i++) x = x * 1.01L + sqrtl(x);
    volatile double d = 3.0; d = d * d / 7;
    hits++;
}
__attribute__((noinline)) static void work(long n, long double *acc, double *dacc) {
    long double a = 0; double d = 0;
    for (long i = 1; i <= n; i++) { a += sqrtl((long double) i) / (long double) (i + 3); d += sqrt((double) i) * 1e-9; }
    *acc = a; *dacc = d;
}
int main(void) {
    long n = 3000000;
    long double a1, a2; double d1, d2;
    work(n, &a1, &d1);
    signal(SIGALRM, handler);
    struct itimerval it = {{0, 2000}, {0, 2000}};
    setitimer(ITIMER_REAL, &it, NULL);
    work(n, &a2, &d2);
    memset(&it, 0, sizeof it); setitimer(ITIMER_REAL, &it, NULL);
    printf("signals delivered: %s, long double equal: %d, double equal: %d\n", hits > 5 ? "yes" : "NO", memcmp(&a1, &a2, 10) == 0, d1 == d2);
    return 0;
}
