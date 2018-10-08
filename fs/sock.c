#include <sys/socket.h>
#include "kernel/calls.h"
#include "fs/fd.h"
#include "fs/sock.h"
#include "debug.h"

static struct fd_ops socket_fdops;

static fd_t sock_fd_create(int sock_fd, int flags) {
    struct fd *fd = adhoc_fd_create();
    if (fd == NULL)
        return _ENOMEM;
    fd->real_fd = sock_fd;
    fd->ops = &socket_fdops;
    if (flags & SOCK_CLOEXEC_)
        fd->flags = FD_CLOEXEC_;
    return f_install(fd);
}

dword_t sys_socket(dword_t domain, dword_t type, dword_t protocol) {
    STRACE("socket(%d, %d, %d)", domain, type, protocol);
    int real_domain = sock_family_to_real(domain);
    if (real_domain < 0)
        return _EINVAL;
    int real_type = sock_type_to_real(type, protocol);
    if (real_type < 0)
        return _EINVAL;

    int sock = socket(real_domain, real_type, protocol);
    if (sock < 0)
        return errno_map();
    fd_t f = sock_fd_create(sock, type);
    if (f < 0)
        close(sock);
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
    switch (fake_addr->family) {
        case PF_INET_:
        case PF_INET6_:
            real_addr->sa_family = fake_addr->family;
            break;
        case PF_LOCAL_:
            return _ENOENT; // lol
        default:
            return _EINVAL;
    }
    return 0;
}

static int sockaddr_write(addr_t sockaddr_addr, void *sockaddr, size_t sockaddr_len) {
    struct sockaddr *real_addr = sockaddr;
    struct sockaddr_ *fake_addr = sockaddr;
    switch (real_addr->sa_family) {
        case PF_INET_:
        case PF_INET6_:
            fake_addr->family = real_addr->sa_family;
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
    int real_opt = sock_opt_to_real(option);
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
    STRACE("getsockopt(%d, %d, %d, 0x%x, %d)", sock_fd, level, option, value_addr, len_addr);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    dword_t value_len;
    if (user_get(len_addr, value_len))
        return _EFAULT;
    char value[value_len];
    if (user_read(value_addr, value, value_len))
        return _EFAULT;
    int real_opt = sock_opt_to_real(option);
    if (real_opt < 0)
        return _EINVAL;
    int real_level = sock_level_to_real(level);
    if (real_level < 0)
        return _EINVAL;

    int err = getsockopt(sock->real_fd, real_level, real_opt, value, &value_len);
    if (err < 0)
        return errno_map();
    if (user_put(len_addr, value_len))
        return _EFAULT;
    return 0;
}

static struct fd_ops socket_fdops = {
    .read = realfs_read,
    .write = realfs_write,
    .close = realfs_close,
};

static struct socket_call {
    syscall_t func;
    int args;
} socket_calls[] = {
    {NULL, 0},
    {(syscall_t) sys_socket, 3},
    {(syscall_t) sys_bind, 3},
    {(syscall_t) sys_connect, 3},
    {NULL, 0}, // listen
    {NULL, 0}, // accept
    {(syscall_t) sys_getsockname, 3}, // getsockname
    {(syscall_t) sys_getpeername, 3}, // getpeername
    {(syscall_t) sys_socketpair, 4}, // socketpair
    {NULL, 0}, // send
    {NULL, 0}, // recv
    {(syscall_t) sys_sendto, 6}, // sendto
    {(syscall_t) sys_recvfrom, 6}, // recvfrom
    {(syscall_t) sys_shutdown, 2}, // shutdown
    {(syscall_t) sys_setsockopt, 5}, // setsockopt
    {(syscall_t) sys_getsockopt, 5}, // getsockopt
    {NULL, 0}, // sendmsg
    {NULL, 0}, // recvmsg
    {NULL, 0}, // accept4
    {NULL, 0}, // recvmmsg
    {NULL, 0}, // sendmmsg
};

dword_t sys_socketcall(dword_t call_num, addr_t args_addr) {
    STRACE("%d ", call_num);
    if (call_num < 1 || call_num >= sizeof(socket_calls)/sizeof(socket_calls[0]))
        return _EINVAL;
    struct socket_call call = socket_calls[call_num];
    if (call.func == NULL) {
        TODO("socketcall %d", call_num);
        return _ENOSYS;
    }

    dword_t args[6];
    if (user_read(args_addr, args, sizeof(dword_t) * call.args))
        return _EFAULT;
    return call.func(args[0], args[1], args[2], args[3], args[4], args[5]);
}
