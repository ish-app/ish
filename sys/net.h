#ifndef SYS_SOCKET_H
#define SYS_SOCKET_H

#include <sys/socket.h>
#include "sys/errno.h"
#include "misc.h"

dword_t sys_socketcall(dword_t call_num, addr_t args_addr);

#define PF_LOCAL_ 1
#define PF_INET_ 2
#define AF_LOCAL_ PF_LOCAL_
#define AF_INET_ PF_INET_

static inline int family_to_real(int fake) {
    switch (fake) {
        case PF_LOCAL_: return PF_LOCAL;
        case PF_INET_: return PF_INET;
        default: return -1;
    }
}

struct sockaddr_ {
    word_t family;
    byte_t data[14];
};

#define SOCK_STREAM_ 1
#define SOCK_DGRAM_ 2

#endif
