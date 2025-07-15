#include "netiso.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "fs/sock.h"
#include "misc.h"
#include "debug.h"

#define NETISO_STRACE_(msg, ...) do { \
    printf(msg, ##__VA_ARGS__); \
    printf("\r\n"); \
} while (0)
#define NETISO_STRACE(msg, ...) STRACE(msg, ##__VA_ARGS__)

static bool netiso_supported_sock_family(uint16_t family) {
    switch (family) {
        case PF_INET:
        case PF_LOCAL:
            return true;
        default:
            return false;
    }
}

static int_t netiso_sockaddr_(addr_t sockaddr_addr, uint_t sockaddr_len) {
    NETISO_STRACE("netiso_sockaddr(0x%x, %d)", sockaddr_addr, sockaddr_len);

    if (sockaddr_len < 2 || sockaddr_len > sizeof(struct sockaddr_max_)) {
        NETISO_STRACE("netiso_sockaddr: invalid sockaddr_len %d", sockaddr_len);
        return _EINVAL;
    }

    struct sockaddr_max_ sockaddr;
    if (user_read(sockaddr_addr, &sockaddr, sockaddr_len)) {
        NETISO_STRACE("netiso_sockaddr: user_read failed");
        return _EFAULT;
    }

    uint16_t real_family = sock_family_to_real(sockaddr.family);
    if (!netiso_supported_sock_family(real_family)) {
        NETISO_STRACE("netiso_sockaddr: unsupported family %d", real_family);
        return _EINVAL;
    }

    if (real_family == PF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&sockaddr;
        in_addr_t ip = ntohl(in->sin_addr.s_addr);

        if (((ip ^ 0x0a000000) & 0xff000000) == 0 ||        // 10.0.0.0/8
            ((ip ^ 0xac100000) & 0xfff00000) == 0 ||        // 172.16.0.0/12
            ((ip ^ 0xc0a80000) & 0xffff0000) == 0 ||        // 192.168.0.0/16
            ((ip ^ 0x7f000000) & 0xff000000) == 0) {        // 127.0.0.0/8
            return 0;
        }

        NETISO_STRACE("netiso_sockaddr: address 0x%08x is not private network", ip);
        return _EINVAL;
    }

    if (real_family == PF_LOCAL) {
        // For local sockets, we can allow all addresses
        NETISO_STRACE("netiso_sockaddr: local socket allowed");
        return 0;
    }

    NETISO_STRACE("netiso_sockaddr: unsupported family: %d", real_family);
    return _EINVAL; // Unsupported family or address
}

int_t netiso_sockaddr(addr_t sockaddr_addr, uint_t sockaddr_len) {
#if NETWORK_ISOLATION
    return netiso_sockaddr_(sockaddr_addr, sockaddr_len);
#else
    return 0;
#endif
}