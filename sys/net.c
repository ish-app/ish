#include <sys/socket.h>
#include "sys/calls.h"
#include "sys/net.h"
#include "debug.h"

static struct fd_ops socket_fdops;

dword_t sys_socket(dword_t domain, dword_t type, dword_t protocol) {
    int real_domain = family_to_real(domain);
    if (real_domain < 0)
        return _EINVAL;
    int real_type;
    switch (type) {
        case SOCK_STREAM_: real_type = SOCK_STREAM; break;
        case SOCK_DGRAM_: real_type = SOCK_DGRAM; break;
        default: return _EINVAL;
    }
    if (protocol != 0)
        return _EINVAL;

    fd_t fd_no = fd_next();
    if (fd_no == -1)
        return _EMFILE;
    int sock = socket(real_domain, real_type, protocol);
    if (sock < 0)
        return err_map(errno);
    struct fd *fd = fd_create();
    fd->real_fd = sock;
    fd->ops = &socket_fdops;
    current->files[fd_no] = fd;
    return fd_no;
}

static struct fd *sock_getfd(fd_t sock_fd) {
    struct fd *sock = current->files[sock_fd];
    if (sock->ops != &socket_fdops)
        return NULL;
    return sock;
}

dword_t sys_bind(fd_t sock_fd, addr_t sockaddr_addr, dword_t sockaddr_len) {
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    char sockaddr_buf[sockaddr_len];
    if (user_read(sockaddr_addr, sockaddr_buf, sockaddr_len))
        return _EFAULT;
    struct sockaddr_ *sockaddr = (void *) sockaddr_buf;
    sockaddr->family = family_to_real(sockaddr->family);
    return bind(sock->real_fd, (void *) sockaddr, sockaddr_len);
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
    {NULL, 0}, // connect
    {NULL, 0}, // listen
    {NULL, 0}, // accept
    {NULL, 0}, // getsockname
    {NULL, 0}, // getpeername
    {NULL, 0}, // socketpair
    {NULL, 0}, // send
    {NULL, 0}, // sendto
    {NULL, 0}, // recv
    {NULL, 0}, // recvfrom
    {NULL, 0}, // shutdown
    {NULL, 0}, // setsockopt
    {NULL, 0}, // getsockopt
    {NULL, 0}, // sendmsg
    {NULL, 0}, // recvmsg
    {NULL, 0}, // accept4
    {NULL, 0}, // recvmmsg
    {NULL, 0}, // sendmmsg
};

dword_t sys_socketcall(dword_t call_num, addr_t args_addr) {
    if (call_num < 1 || call_num > sizeof(socket_calls)/sizeof(socket_calls[0]))
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
