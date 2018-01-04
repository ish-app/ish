#ifndef SYS_SOCK_H
#define SYS_SOCK_H

#include <sys/socket.h>
#include <netinet/tcp.h>
#include "kernel/errno.h"
#include "misc.h"
#include "debug.h"

dword_t sys_socketcall(dword_t call_num, addr_t args_addr);

struct sockaddr_ {
    uint16_t family;
    char data[14];
};

size_t sockaddr_size(void *p);
// result comes from malloc
struct sockaddr *sockaddr_to_real(void *p);

#define PF_LOCAL_ 1
#define PF_INET_ 2
#define PF_INET6_ 10
#define AF_LOCAL_ PF_LOCAL_
#define AF_INET_ PF_INET_
#define AF_INET6_ PF_INET6_

static inline int sock_family_to_real(int fake) {
    switch (fake) {
        case PF_LOCAL_: return PF_LOCAL;
        case PF_INET_: return PF_INET;
        case PF_INET6_: return PF_INET6;
    }
    return -1;
}

#define SOCK_STREAM_ 1
#define SOCK_DGRAM_ 2
#define SOCK_NONBLOCK_ 0x800
#define SOCK_CLOEXEC_ 0x80000

static inline int sock_type_to_real(int type, int protocol) {
    switch (type & 0xff) {
        case SOCK_STREAM_:
            if (protocol != 0 && protocol != 6)
                return -1;
            return SOCK_STREAM;
        case SOCK_DGRAM_:
            if (protocol != 0 && protocol != 17)
                return -1;
            return SOCK_DGRAM;
    }
    return -1;
}

#define MSG_OOB_ 0x1
#define MSG_PEEK_ 0x2
#define MSG_WAITALL_ 0x100

static inline int sock_flags_to_real(int fake) {
    int real = 0;
    if (fake & MSG_OOB_) real |= MSG_OOB;
    if (fake & MSG_PEEK_) real |= MSG_PEEK;
    if (fake & MSG_WAITALL_) real |= MSG_WAITALL;
    if (fake & ~(MSG_OOB_|MSG_PEEK_|MSG_WAITALL_))
        TRACELN("unimplemented socket flags %d", fake);
    return real;
}

#define TCP_NODELAY_ 1
#define SO_KEEPALIVE_ 9

static inline int sock_opt_to_real(int fake) {
    switch (fake) {
        case TCP_NODELAY_: return TCP_NODELAY;
        case SO_KEEPALIVE_: return SO_KEEPALIVE;
    }
    return -1;
}

#endif
