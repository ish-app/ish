#ifndef SYS_SOCKET_H
#define SYS_SOCKET_H

#include <sys/socket.h>
#include "sys/errno.h"
#include "misc.h"
#include "debug.h"

dword_t sys_socketcall(dword_t call_num, addr_t args_addr);

struct sockaddr_ {
    word_t family;
    byte_t data[14];
};

#define PF_LOCAL_ 1
#define PF_INET_ 2
#define AF_LOCAL_ PF_LOCAL_
#define AF_INET_ PF_INET_

static inline int sock_family_to_real(int fake) {
    switch (fake) {
        case PF_LOCAL_: return PF_LOCAL;
        case PF_INET_: return PF_INET;
        default: return -1;
    }
}

#define SOCK_STREAM_ 1
#define SOCK_DGRAM_ 2

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

#endif
