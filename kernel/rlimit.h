#ifndef RLIMIT_H
#define RLIMIT_H

typedef qword_t rlim_t_;
#define RLIM_INFINITY_ ((rlim_t_) -1)

struct rlimit_ {
    rlim_t_ cur;
    rlim_t_ max;
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

#endif
