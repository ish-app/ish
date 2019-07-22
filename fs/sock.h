#ifndef SYS_SOCK_H
#define SYS_SOCK_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "kernel/errno.h"
#include "fs/fd.h"
#include "misc.h"
#include "debug.h"

int_t sys_socketcall(dword_t call_num, addr_t args_addr);

int_t sys_socket(dword_t domain, dword_t type, dword_t protocol);
int_t sys_bind(fd_t sock_fd, addr_t sockaddr_addr, uint_t sockaddr_len);
int_t sys_connect(fd_t sock_fd, addr_t sockaddr_addr, uint_t sockaddr_len);
int_t sys_listen(fd_t sock_fd, int_t backlog);
int_t sys_accept(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int_t sys_getsockname(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int_t sys_getpeername(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int_t sys_socketpair(dword_t domain, dword_t type, dword_t protocol, addr_t sockets_addr);
int_t sys_sendto(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, dword_t sockaddr_len);
int_t sys_recvfrom(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, addr_t sockaddr_len_addr);
int_t sys_shutdown(fd_t sock_fd, dword_t how);
int_t sys_setsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, dword_t value_len);
int_t sys_getsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, dword_t len_addr);
int_t sys_sendmsg(fd_t sock_fd, addr_t msghdr_addr, int_t flags);
int_t sys_recvmsg(fd_t sock_fd, addr_t msghdr_addr, int_t flags);
int_t sys_sendmmsg(fd_t sock_fd, addr_t msgvec_addr, uint_t msgvec_len, int_t flags);

struct sockaddr_ {
    uint16_t family;
    char data[14];
};
struct sockaddr_max_ {
    uint16_t family;
    char data[108];
};

size_t sockaddr_size(void *p);
// result comes from malloc
struct sockaddr *sockaddr_to_real(void *p);

struct msghdr_ {
    addr_t msg_name;
    uint_t msg_namelen;
    addr_t msg_iov;
    uint_t msg_iovlen;
    addr_t msg_control;
    uint_t msg_controllen;
    int_t msg_flags;
};

struct cmsghdr_ {
    dword_t len;
    int_t level;
    int_t type;
    uint8_t data[];
};
#define SCM_RIGHTS_ 1
// copied and ported from musl
#define CMSG_LEN_(cmsg) (((cmsg)->len + sizeof(dword_t) - 1) & ~(dword_t)(sizeof(dword_t) - 1))
#define CMSG_NEXT_(cmsg) ((uint8_t *)(cmsg) + CMSG_LEN_(cmsg))
#define CMSG_NXTHDR_(cmsg, mhdr_end) ((cmsg)->len < sizeof (struct cmsghdr_) || \
        CMSG_LEN_(cmsg) + sizeof(struct cmsghdr_) >= (size_t) (mhdr_end - (uint8_t *)(cmsg)) \
        ? NULL : (struct cmsghdr_ *)CMSG_NEXT_(cmsg))

struct scm {
    struct list queue;
    unsigned num_fds;
    struct fd *fds[];
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
            switch (protocol) {
                default:
                    return -1;
                case 0:
                case IPPROTO_UDP:
                case IPPROTO_ICMP:
                case IPPROTO_ICMPV6:
                    break;
            }
            return SOCK_DGRAM;
        case SOCK_RAW_:
            switch (protocol) {
                default:
                    return -1;
                case IPPROTO_RAW:
                case IPPROTO_UDP:
                case IPPROTO_ICMP:
                case IPPROTO_ICMPV6:
                    break;
            }
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
#define SO_SNDBUF_ 7
#define SO_KEEPALIVE_ 9
#define SO_TIMESTAMP_ 29
#define SO_PROTOCOL_ 38
#define SO_DOMAIN_ 39
#define IP_TOS_ 1
#define IP_TTL_ 2
#define IP_HDRINCL_ 3
#define IP_RETOPTS_ 7
#define IP_MTU_DISCOVER_ 10
#define IP_RECVTTL_ 12
#define IP_RECVTOS_ 13
#define TCP_NODELAY_ 1
#define TCP_DEFER_ACCEPT_ 9
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
            case SO_TIMESTAMP_: return SO_TIMESTAMP;
        } break;
        case IPPROTO_TCP: switch (fake) {
            case TCP_NODELAY_: return TCP_NODELAY;
            case TCP_DEFER_ACCEPT_: return 0; // unimplemented
        } break;
        case IPPROTO_IP: switch (fake) {
            case IP_TOS_: return IP_TOS;
            case IP_TTL_: return IP_TTL;
            case IP_HDRINCL_: return IP_HDRINCL;
            case IP_RETOPTS_: return IP_RETOPTS;
            case IP_RECVTTL_: return IP_RECVTTL;
            case IP_RECVTOS_: return IP_RECVTOS;
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

extern const char *sock_tmp_prefix;

#endif
