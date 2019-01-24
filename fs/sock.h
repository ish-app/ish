#ifndef SYS_SOCK_H
#define SYS_SOCK_H

#include <sys/socket.h>
#include <netinet/in.h>
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

struct msghdr_ {
  addr_t msg_name;
  int_t msg_namelen;
  addr_t msg_iov;
  uint_t msg_iovlen;
  addr_t msg_control;
  uint_t msg_controllen;
  int_t msg_flags;
};

struct iovec_ {
  addr_t iov_base;
  uint_t iov_len;
};

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
static inline int sock_family_from_real(int fake) {
    switch (fake) {
        case PF_LOCAL: return PF_LOCAL_;
        case PF_INET: return PF_INET_;
        case PF_INET6: return PF_INET6_;
    }
    return -1;
}

#define SOCK_STREAM_ 1
#define SOCK_DGRAM_ 2
#define SOCK_RAW_ 3
#define SOCK_NONBLOCK_ 0x800
#define SOCK_CLOEXEC_ 0x80000

static inline int sock_type_to_real(int type, int protocol) {
    switch (type & 0xff) {
        case SOCK_STREAM_:
            if (protocol != 0 && protocol != IPPROTO_TCP)
                return -1;
            return SOCK_STREAM;
        case SOCK_DGRAM_:
            if (protocol != 0 && protocol != IPPROTO_UDP)
                return -1;
            return SOCK_DGRAM;
        case SOCK_RAW_:
            if (protocol != IPPROTO_UDP && protocol != IPPROTO_ICMP && protocol != IPPROTO_ICMPV6 && protocol != IPPROTO_RAW)
                return -1;
            return SOCK_DGRAM;
    }
    return -1;
}

#define MSG_OOB_ 0x1
#define MSG_PEEK_ 0x2
#define MSG_CTRUNC_  0x8
#define MSG_TRUNC_  0x20
#define MSG_DONTWAIT_ 0x40
#define MSG_EOR_    0x80
#define MSG_WAITALL_ 0x100

static inline int sock_flags_to_real(int fake) {
    int real = 0;
    if (fake & MSG_OOB_) real |= MSG_OOB;
    if (fake & MSG_PEEK_) real |= MSG_PEEK;
    if (fake & MSG_CTRUNC_) real |= MSG_CTRUNC;
    if (fake & MSG_TRUNC_) real |= MSG_TRUNC;
    if (fake & MSG_DONTWAIT_) real |= MSG_DONTWAIT;
    if (fake & MSG_EOR_) real |= MSG_EOR;
    if (fake & MSG_WAITALL_) real |= MSG_WAITALL;
    if (fake & ~(MSG_OOB_|MSG_PEEK_|MSG_CTRUNC_|MSG_TRUNC_|MSG_DONTWAIT_|MSG_EOR_|MSG_WAITALL_))
        TRACE("unimplemented socket flags %d\n", fake);
    return real;
}
static inline int sock_flags_from_real(int real) {
  int fake = 0;
  if (real & MSG_OOB) fake |= MSG_OOB_;
  if (real & MSG_PEEK) fake |= MSG_PEEK_;
  if (real & MSG_CTRUNC) fake |= MSG_CTRUNC_;
  if (real & MSG_TRUNC) fake |= MSG_TRUNC_;
  if (real & MSG_DONTWAIT) fake |= MSG_DONTWAIT_;
  if (real & MSG_EOR) fake |= MSG_EOR_;
  if (real & MSG_WAITALL) fake |= MSG_WAITALL_;
  if (real & ~(MSG_OOB|MSG_PEEK|MSG_CTRUNC|MSG_TRUNC|MSG_DONTWAIT|MSG_EOR|MSG_WAITALL))
      TRACE("unimplemented socket flags %d\n", real);
  return fake;
}

#define SOL_SOCKET_ 1

#define SO_REUSEADDR_ 2
#define SO_TYPE_ 3
#define SO_ERROR_ 4
#define SO_BROADCAST_ 6
#define SO_KEEPALIVE_ 9
#define SO_SNDBUF_ 7
#define IP_TOS_ 1
#define IP_TTL_ 2
#define IP_HDRINCL_ 3
#define IP_MTU_DISCOVER_ 10
#define TCP_NODELAY_ 1
#define IPV6_UNICAST_HOPS_ 16
#define IPV6_V6ONLY_ 26
#define IPV6_TCLASS_ 67
#define ICMP6_FILTER_ 1

static inline int sock_opt_to_real(int fake, int level) {
    switch (level) {
        case SOL_SOCKET_: switch (fake) {
            case SO_REUSEADDR_: return SO_REUSEADDR;
            case SO_TYPE_: return SO_TYPE;
            case SO_ERROR_: return SO_ERROR;
            case SO_BROADCAST_: return SO_BROADCAST;
            case SO_KEEPALIVE_: return SO_KEEPALIVE;
            case SO_SNDBUF_: return SO_SNDBUF;
        } break;
        case IPPROTO_TCP: switch (fake) {
            case TCP_NODELAY_: return TCP_NODELAY;
        } break;
        case IPPROTO_IP: switch (fake) {
            case IP_TOS_: return IP_TOS;
            case IP_TTL_: return IP_TTL;
            case IP_HDRINCL_: return IP_HDRINCL;
        } break;
        case IPPROTO_IPV6: switch (fake) {
            case IPV6_UNICAST_HOPS_: return IPV6_UNICAST_HOPS;
            case IPV6_TCLASS_: return IPV6_TCLASS;
            case IPV6_V6ONLY_: return IPV6_V6ONLY;
        } break;
    }
    return -1;
}

static inline int sock_level_to_real(int fake) {
    if (fake == SOL_SOCKET_)
        return SOL_SOCKET;
    return fake;
}

#endif
