#if !__i386__ || !__linux__
#error "VDSO must be built in Linux in 32-bit mode"
#endif

#define _GNU_SOURCE
#include <sys/time.h>
#include <time.h>
#include <asm/unistd.h>

time_t __vdso_time(time_t *t) {
    time_t result;
    __asm__("int $0x80" : "=a" (result) :
            "0" (__NR_time), "b" (t));
    return result;
}

int __vdso_gettimeofday(struct timeval *tv, void *tz) {
    int result;
    __asm__("int $0x80" : "=a" (result) :
            "0" (__NR_gettimeofday), "b" (tv), "c" (tz));
    return result;
}

int __vdso_clock_gettime(clockid_t clock, struct timespec *ts) {
    int result;
    __asm__("int $0x80" : "=a" (result) :
            "0" (__NR_clock_gettime), "b" (clock), "c" (ts));
    return result;
}

