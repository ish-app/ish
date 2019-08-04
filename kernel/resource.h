#ifndef RESOURCE_H
#define RESOURCE_H
#include "kernel/time.h"

typedef qword_t rlim_t_;
typedef dword_t rlim32_t_;
#define RLIM_INFINITY_ ((rlim_t_) -1)

struct rlimit_ {
    rlim_t_ cur;
    rlim_t_ max;
};

struct rlimit32_ {
    rlim32_t_ cur;
    rlim32_t_ max;
};

#define RLIMIT_CPU_ 0
#define RLIMIT_FSIZE_ 1
#define RLIMIT_DATA_ 2
#define RLIMIT_STACK_ 3
#define RLIMIT_CORE_ 4
#define RLIMIT_RSS_ 5
#define RLIMIT_NPROC_ 6
#define RLIMIT_NOFILE_ 7
#define RLIMIT_MEMLOCK_ 8
#define RLIMIT_AS_ 9
#define RLIMIT_LOCKS_ 10
#define RLIMIT_SIGPENDING_ 11
#define RLIMIT_MSGQUEUE_ 12
#define RLIMIT_NICE_ 13
#define RLIMIT_RTPRIO_ 14
#define RLIMIT_RTTIME_ 15
#define RLIMIT_NLIMITS_ 16

dword_t sys_getrlimit32(dword_t resource, addr_t rlim_addr);
dword_t sys_setrlimit32(dword_t resource, addr_t rlim_addr);
dword_t sys_prlimit64(pid_t_ pid, dword_t resource, addr_t new_limit_addr, addr_t old_limit_addr);
dword_t sys_old_getrlimit32(dword_t resource, addr_t rlim_addr);

rlim_t_ rlimit(int resource);

struct rusage_ {
    struct timeval_ utime;
    struct timeval_ stime;
    dword_t maxrss;
    dword_t ixrss;
    dword_t idrss;
    dword_t isrss;
    dword_t minflt;
    dword_t majflt;
    dword_t nswap;
    dword_t inblock;
    dword_t oublock;
    dword_t msgsnd;
    dword_t msgrcv;
    dword_t nsignals;
    dword_t nvcsw;
    dword_t nivcsw;
};

struct rusage_ rusage_get_current(void);
void rusage_add(struct rusage_ *dst, struct rusage_ *src);
#define RUSAGE_SELF_ 0
#define RUSAGE_CHILDREN_ -1
dword_t sys_getrusage(dword_t who, addr_t rusage_addr);

int_t sys_sched_getaffinity(pid_t_ pid, dword_t cpusetsize, addr_t cpuset_addr);
int_t sys_sched_setaffinity(pid_t_ pid, dword_t cpusetsize, addr_t cpuset_addr);
int_t sys_getpriority(int_t which, pid_t_ who);
int_t sys_setpriority(int_t which, pid_t_ who, int_t prio);

#endif
