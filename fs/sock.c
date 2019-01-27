#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "fs/fd.h"
#include "fs/sock.h"
#include "debug.h"

const struct fd_ops socket_fdops;

static fd_t sock_fd_create(int sock_fd, int flags) {
    struct fd *fd = adhoc_fd_create(&socket_fdops);
    if (fd == NULL)
        return _ENOMEM;
    fd->stat.mode = S_IFSOCK | 0666;
    fd->real_fd = sock_fd;
    return f_install(fd, flags);
}

dword_t sys_socket(dword_t domain, dword_t type, dword_t protocol) {
    STRACE("socket(%d, %d, %d)", domain, type, protocol);
    int real_domain = sock_family_to_real(domain);
    if (real_domain < 0)
        return _EINVAL;
    int real_type = sock_type_to_real(type, protocol);
    if (real_type < 0)
        return _EINVAL;

    // this hack makes mtr work
    if (type == SOCK_RAW_ && protocol == IPPROTO_RAW)
        protocol = IPPROTO_ICMP;

    int sock = socket(real_domain, real_type, protocol);
    if (sock < 0)
        return errno_map();
    fd_t f = sock_fd_create(sock, type);
    if (f < 0)
        close(sock);
    if (f >= 0)
        f_get(f)->sockrestart.proto = protocol;
    return f;
}

static struct fd *sock_getfd(fd_t sock_fd) {
    struct fd *sock = f_get(sock_fd);
    if (sock == NULL || sock->ops != &socket_fdops)
        return NULL;
    return sock;
}

static int sockaddr_read(addr_t sockaddr_addr, void *sockaddr, size_t sockaddr_len) {
    if (user_read(sockaddr_addr, sockaddr, sockaddr_len))
        return _EFAULT;
    struct sockaddr *real_addr = sockaddr;
    struct sockaddr_ *fake_addr = sockaddr;
    real_addr->sa_family = sock_family_to_real(fake_addr->family);
    switch (real_addr->sa_family) {
        case PF_INET:
        case PF_INET6:
            break;
        case PF_LOCAL:
            return _ENOENT; // lol
        default:
            return _EINVAL;
    }
    return 0;
}

static int sockaddr_write(addr_t sockaddr_addr, void *sockaddr, size_t sockaddr_len) {
    struct sockaddr *real_addr = sockaddr;
    struct sockaddr_ *fake_addr = sockaddr;
    fake_addr->family = sock_family_from_real(real_addr->sa_family);
    switch (fake_addr->family) {
        case PF_INET_:
        case PF_INET6_:
            break;
        case PF_LOCAL_:
            return _ENOENT; // lol
        default:
            return _EINVAL;
    }
    if (user_write(sockaddr_addr, sockaddr, sockaddr_len))
        return _EFAULT;
    return 0;
}

dword_t sys_bind(fd_t sock_fd, addr_t sockaddr_addr, dword_t sockaddr_len) {
    STRACE("bind(%d, 0x%x, %d)", sock_fd, sockaddr_addr, sockaddr_len);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    char sockaddr[sockaddr_len];
    int err = sockaddr_read(sockaddr_addr, sockaddr, sockaddr_len);
    if (err < 0)
        return err;

    err = bind(sock->real_fd, (void *) sockaddr, sockaddr_len);
    if (err < 0)
        return errno_map();
    return 0;
}

dword_t sys_connect(fd_t sock_fd, addr_t sockaddr_addr, dword_t sockaddr_len) {
    STRACE("connect(%d, 0x%x, %d)", sock_fd, sockaddr_addr, sockaddr_len);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    char sockaddr[sockaddr_len];
    int err = sockaddr_read(sockaddr_addr, sockaddr, sockaddr_len);
    if (err < 0)
        return err;

    err = connect(sock->real_fd, (void *) sockaddr, sockaddr_len);
    if (err < 0)
        return errno_map();
    return err;
}

dword_t sys_listen(fd_t sock_fd, int_t backlog) {
    STRACE("listen(%d, %d)", sock_fd, backlog);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    int err = listen(sock->real_fd, backlog);
    if (err < 0)
        return errno_map();
    sockrestart_begin_listen(sock);
    return err;
}

dword_t sys_accept(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
    STRACE("accept(%d, 0x%x, 0x%x)", sock_fd, sockaddr_addr, sockaddr_len_addr);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    dword_t sockaddr_len;
    if (user_get(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;

    char sockaddr[sockaddr_len];
    int client;
    do {
        sockrestart_begin_listen_wait(sock);
        errno = 0;
        client = accept(sock->real_fd, (void *) sockaddr, &sockaddr_len);
        sockrestart_end_listen_wait(sock);
    } while (sockrestart_should_restart_listen_wait() && errno == EINTR);
    if (client < 0)
        return errno_map();

    int err = sockaddr_write(sockaddr_addr, sockaddr, sockaddr_len);
    if (err < 0)
        return client;
    if (user_put(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;

    fd_t client_f = sock_fd_create(client, 0);
    if (client_f < 0)
        close(client);
    return client_f;
}

dword_t sys_getsockname(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
    STRACE("getsockname(%d, 0x%x, 0x%x)", sock_fd, sockaddr_addr, sockaddr_len_addr);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    dword_t sockaddr_len;
    if (user_get(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;

    char sockaddr[sockaddr_len];
    int res = getsockname(sock->real_fd, (void *) sockaddr, &sockaddr_len);
    if (res < 0)
        return errno_map();

    int err = sockaddr_write(sockaddr_addr, sockaddr, sockaddr_len);
    if (err < 0)
        return err;
    if (user_put(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;
    return res;
}

dword_t sys_getpeername(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
    STRACE("getpeername(%d, 0x%x, 0x%x)", sock_fd, sockaddr_addr, sockaddr_len_addr);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    dword_t sockaddr_len;
    if (user_get(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;

    char sockaddr[sockaddr_len];
    int res = getpeername(sock->real_fd, (void *) sockaddr, &sockaddr_len);
    if (res < 0)
        return errno_map();

    int err = sockaddr_write(sockaddr_addr, sockaddr, sockaddr_len);
    if (err < 0)
        return err;
    if (user_put(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;
    return res;
}

dword_t sys_socketpair(dword_t domain, dword_t type, dword_t protocol, addr_t sockets_addr) {
    STRACE("socketpair(%d, %d, %d, 0x%x)", domain, type, protocol, sockets_addr);
    int real_domain = sock_family_to_real(domain);
    if (real_domain < 0)
        return _EINVAL;
    int real_type = sock_type_to_real(type, protocol);
    if (real_type < 0)
        return _EINVAL;

    int sockets[2];
    int err = socketpair(domain, type, protocol, sockets);
    if (err < 0)
        return errno_map();

    int fake_sockets[2];
    err = fake_sockets[0] = sock_fd_create(sockets[0], type);
    if (fake_sockets[0] < 0)
        goto close_sockets;
    err = fake_sockets[1] = sock_fd_create(sockets[1], type);
    if (fake_sockets[1] < 0)
        goto close_fake_0;

    err = _EFAULT;
    if (user_put(sockets_addr, fake_sockets))
        goto close_fake_1;

    STRACE(" [%d, %d]", fake_sockets[0], fake_sockets[1]);
    return 0;

close_fake_1:
    sys_close(fake_sockets[1]);
close_fake_0:
    sys_close(fake_sockets[0]);
close_sockets:
    close(sockets[0]);
    close(sockets[1]);
    return err;
}

dword_t sys_sendto(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, dword_t sockaddr_len) {
    STRACE("sendto(%d, 0x%x, %d, %d, 0x%x, %d)", sock_fd, buffer_addr, len, flags, sockaddr_addr, sockaddr_len);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    char buffer[len];
    if (user_read(buffer_addr, buffer, len))
        return _EFAULT;
    int real_flags = sock_flags_to_real(flags);
    if (real_flags < 0)
        return _EINVAL;
    char sockaddr[sockaddr_len];
    if (sockaddr_addr) {
        int err = sockaddr_read(sockaddr_addr, sockaddr, sockaddr_len);
        if (err < 0)
            return err;
    }

    ssize_t res = sendto(sock->real_fd, buffer, len, real_flags,
            sockaddr_addr ? (void *) sockaddr : NULL, sockaddr_len);
    if (res < 0)
        return errno_map();
    return res;
}

dword_t sys_recvfrom(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
    STRACE("recvfrom(%d, 0x%x, %d, %d, 0x%x, 0x%x)", sock_fd, buffer_addr, len, flags, sockaddr_addr, sockaddr_len_addr);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    int real_flags = sock_flags_to_real(flags);
    if (real_flags < 0)
        return _EINVAL;
    dword_t sockaddr_len = 0;
    if (sockaddr_len_addr != 0)
        if (user_get(sockaddr_len_addr, sockaddr_len))
            return _EFAULT;

    char buffer[len];
    char sockaddr[sockaddr_len];
    ssize_t res = recvfrom(sock->real_fd, buffer, len, real_flags,
            sockaddr_addr != 0 ? (void *) sockaddr : NULL,
            sockaddr_len_addr != 0 ? &sockaddr_len : NULL);
    if (res < 0)
        return errno_map();

    if (user_write(buffer_addr, buffer, len))
        return _EFAULT;
    if (sockaddr_addr != 0) {
        int err = sockaddr_write(sockaddr_addr, sockaddr, sockaddr_len);
        if (err < 0)
            return err;
    }
    if (sockaddr_len_addr != 0)
        if (user_put(sockaddr_len_addr, sockaddr_len))
            return _EFAULT;
    return res;
}

dword_t sys_shutdown(fd_t sock_fd, dword_t how) {
    STRACE("shutdown(%d, %d)", sock_fd, how);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    int err = shutdown(sock->real_fd, how);
    if (err < 0)
        return errno_map();
    return 0;
}

dword_t sys_setsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, dword_t value_len) {
    STRACE("setsockopt(%d, %d, %d, 0x%x, %d)", sock_fd, level, option, value_addr, value_len);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    char value[value_len];
    if (user_read(value_addr, value, value_len))
        return _EFAULT;

    // ICMP6_FILTER can only be set on real SOCK_RAW
    if (level == IPPROTO_ICMPV6 && option == ICMP6_FILTER_)
        return 0;
    // IP_MTU_DISCOVER has no equivalent on Darwin
    if (level == IPPROTO_IP && option == IP_MTU_DISCOVER_)
        return 0;

    int real_opt = sock_opt_to_real(option, level);
    if (real_opt < 0)
        return _EINVAL;
    int real_level = sock_level_to_real(level);
    if (real_level < 0)
        return _EINVAL;

    int err = setsockopt(sock->real_fd, real_level, real_opt, value, value_len);
    if (err < 0)
        return errno_map();
    return 0;
}

dword_t sys_getsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, dword_t len_addr) {
    STRACE("getsockopt(%d, %d, %d, %#x, %#x)", sock_fd, level, option, value_addr, len_addr);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    dword_t value_len;
    if (user_get(len_addr, value_len))
        return _EFAULT;
    char value[value_len];
    if (user_read(value_addr, value, value_len))
        return _EFAULT;
    int real_opt = sock_opt_to_real(option, level);
    if (real_opt < 0)
        return _EINVAL;
    int real_level = sock_level_to_real(level);
    if (real_level < 0)
        return _EINVAL;

    int err = getsockopt(sock->real_fd, real_level, real_opt, value, &value_len);
    if (err < 0)
        return errno_map();

    if (level == SOL_SOCKET_ && option == SO_TYPE_) {
        // TODO Find a way to get the socket protocol so we can return SOCK_RAW_ for
        // our fake raw sockets (SO_PROTOCOL is not available).

        dword_t *type = (dword_t *) &value[0];
        switch (*type) {
            case SOCK_STREAM: *type = SOCK_STREAM_; break;
            case SOCK_DGRAM: *type = SOCK_DGRAM_; break;
        }
    }

    if (user_put(len_addr, value_len))
        return _EFAULT;
    if (user_put(value_addr, value))
        return _EFAULT;
    return 0;
}

dword_t sys_recvmsg(fd_t sock_fd, addr_t msghdr_addr, dword_t flags) {
    STRACE("recvmsg(%d, 0x%x, %d)", sock_fd, msghdr_addr, flags);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));

    struct msghdr_ msg_fake;
    if (user_get(msghdr_addr, msg_fake)) {
        return _EFAULT;
    }

    // msg_name
    struct sockaddr msg_name;
    memset(&msg_name, 0, sizeof(msg_name));

    struct sockaddr_ msg_name_fake;
    if (msg_fake.msg_name != 0 && msg_fake.msg_namelen != 0) {
        if (user_get(msg_fake.msg_name, msg_name_fake))
            return _EFAULT;
#ifdef __APPLE__
        msg_name.sa_len = sizeof(msg_name);
#endif
        msg_name.sa_family = msg_name_fake.family;
        memcpy(&msg_name.sa_data, msg_name_fake.data, sizeof(msg_name_fake.data));

        msg.msg_name = (void *)&msg_name;
        msg.msg_namelen = sizeof(msg_name);
    }

    // msg_control (no initial content)
    char msg_control[msg_fake.msg_controllen];
    memset(&msg_control, 0, sizeof(msg_control));

    msg.msg_control = (void *)&msg_control;
    msg.msg_controllen = sizeof(msg_control);

    // msg_iovec (no initial content)
    struct iovec msg_iov[msg_fake.msg_iovlen];
    memset(&msg_iov, 0, sizeof(msg_iov));

    struct iovec_ msg_iov_fake[msg_fake.msg_iovlen];
    if (user_get(msg_fake.msg_iov, msg_iov_fake))
        return _EFAULT;

    for (uint_t i = 0; i < msg_fake.msg_iovlen; i++) {
        char iov_base[msg_iov_fake[i].iov_len];
        memset(&iov_base, 0, sizeof(iov_base));

        msg_iov[i].iov_base = (void *)&iov_base;
        msg_iov[i].iov_len = sizeof(iov_base);
    }

    msg.msg_iov = (void *)&msg_iov;
    msg.msg_iovlen = msg_fake.msg_iovlen;

    // msg_flags (no initial content)
    msg.msg_flags = sock_flags_to_real(msg_fake.msg_flags);

    int real_flags = sock_flags_to_real(flags);
    if (real_flags < 0)
        return _EINVAL;

    ssize_t res = recvmsg(sock->real_fd, &msg, real_flags);

    // msg_name (changed)
    memset(&msg_name_fake, 0, sizeof(msg_name_fake));

    if (msg.msg_name != 0 && msg.msg_namelen != 0) {
        struct sockaddr *msg_name = msg.msg_name;

        msg_name_fake.family = msg_name->sa_family;
        memcpy(&msg_name_fake.data, msg_name->sa_data, sizeof(msg_name->sa_data));

        if (user_put(msg_fake.msg_name, msg_name_fake)) {
            return _EFAULT;
        }
        msg_fake.msg_namelen = sizeof(msg_name_fake);
    }

    // msg_control (changed)
    uint_t message_offset = 0;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        void *cmsg_data = CMSG_DATA(cmsg);
        uint_t cmsg_data_size = cmsg->cmsg_len - sizeof(cmsg);

        if (user_write(msg_fake.msg_control + message_offset, cmsg, sizeof(cmsg)))
            return _EFAULT;

        if (user_write(msg_fake.msg_control + message_offset + sizeof(cmsg), cmsg_data, cmsg_data_size))
            return _EFAULT;

        message_offset += cmsg->cmsg_len;
    }

    msg_fake.msg_controllen = message_offset;

    // msg_iovec (changed)
    for (int i = 0; i < msg.msg_iovlen; i++) {
        if (user_write(msg_iov_fake[i].iov_base, msg_iov[i].iov_base, msg_iov[i].iov_len))
            return _EFAULT;

        msg_iov_fake[i].iov_len = msg_iov[i].iov_len;
    }

    if (user_put(msg_fake.msg_iov, msg_iov_fake))
        return _EFAULT;

    // msg_flags (changed)
    msg_fake.msg_flags = sock_flags_from_real(msg.msg_flags);

    if (user_put(msghdr_addr, msg_fake)) {
        return _EFAULT;
    }

    if (res < 0)
        return errno_map();
    return res;
}

static int sock_close(struct fd *fd) {
    sockrestart_end_listen(fd);
    return realfs_close(fd);
}

const struct fd_ops socket_fdops = {
    .read = realfs_read,
    .write = realfs_write,
    .close = sock_close,
    .poll = realfs_poll,
    .getflags = realfs_getflags,
    .setflags = realfs_setflags,
};

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static struct socket_call {
    syscall_t func;
    int args;
} socket_calls[] = {
    {NULL},
    {(syscall_t) sys_socket, 3},
    {(syscall_t) sys_bind, 3},
    {(syscall_t) sys_connect, 3},
    {(syscall_t) sys_listen, 2},
    {(syscall_t) sys_accept, 3},
    {(syscall_t) sys_getsockname, 3},
    {(syscall_t) sys_getpeername, 3},
    {(syscall_t) sys_socketpair, 4},
    {NULL}, // send
    {NULL}, // recv
    {(syscall_t) sys_sendto, 6},
    {(syscall_t) sys_recvfrom, 6},
    {(syscall_t) sys_shutdown, 2},
    {(syscall_t) sys_setsockopt, 5},
    {(syscall_t) sys_getsockopt, 5},
    {NULL}, // sendmsg
    {(syscall_t) sys_recvmsg, 3},
    {NULL}, // accept4
    {NULL}, // recvmmsg
    {NULL}, // sendmmsg
};

dword_t sys_socketcall(dword_t call_num, addr_t args_addr) {
    STRACE("%d ", call_num);
    if (call_num < 1 || call_num >= sizeof(socket_calls)/sizeof(socket_calls[0]))
        return _EINVAL;
    struct socket_call call = socket_calls[call_num];
    if (call.func == NULL) {
        FIXME("socketcall %d", call_num);
        return _ENOSYS;
    }

    dword_t args[6];
    if (user_read(args_addr, args, sizeof(dword_t) * call.args))
        return _EFAULT;
    return call.func(args[0], args[1], args[2], args[3], args[4], args[5]);
}
