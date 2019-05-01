#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "fs/fd.h"
#include "fs/inode.h"
#include "fs/path.h"
#include "fs/sock.h"
#include "debug.h"

#define SOCKET_TYPE_MASK 0xf

const struct fd_ops socket_fdops;

static fd_t sock_fd_create(int sock_fd, int domain, int type, int protocol) {
    struct fd *fd = adhoc_fd_create(&socket_fdops);
    if (fd == NULL)
        return _ENOMEM;
    fd->stat.mode = S_IFSOCK | 0666;
    fd->real_fd = sock_fd;
    fd->socket.domain = domain;
    fd->socket.type = type & SOCKET_TYPE_MASK;
    fd->socket.protocol = protocol;
    return f_install(fd, type & ~SOCKET_TYPE_MASK);
}

int_t sys_socket(dword_t domain, dword_t type, dword_t protocol) {
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

#ifdef __APPLE__
    if (domain == AF_INET_ && type == SOCK_DGRAM_) {
        // in some cases, such as ICMP, datagram sockets on mac can default to
        // including the IP header like raw sockets
        int one = 1;
        setsockopt(sock, IPPROTO_IP, IP_STRIPHDR, &one, sizeof(one));
    }
#endif

    fd_t f = sock_fd_create(sock, domain, type, protocol);
    if (f < 0)
        close(sock);
    return f;
}

static void inode_release_if_exist(struct inode_data *inode) {
    if (inode != NULL)
        inode_release(inode);
}

static struct fd *sock_getfd(fd_t sock_fd) {
    struct fd *sock = f_get(sock_fd);
    if (sock == NULL || sock->ops != &socket_fdops)
        return NULL;
    return sock;
}

static struct inode_data *unix_socket_get(const char *path_raw, int flag) {
    char path[MAX_PATH];
    int err = path_normalize(AT_PWD, path_raw, path, true);
    if (err < 0)
        return ERR_PTR(err);
    struct mount *mount = find_mount_and_trim_path(path);
    struct statbuf stat;
    err = mount->fs->stat(mount, path, &stat, true);

    // If bind was called, there are some funny semantics.
    if (flag & O_CREAT_) {
        // If the file exists, fail.
        if (err == 0) {
            err = _EADDRINUSE;
            goto out;
        }
        // If the file can't be found, try to create it as a socket.
        if (err < 0) {
            mode_t_ mode = 0777;
            struct fs_info *fs = current->fs;
            lock(&fs->lock);
            mode &= ~fs->umask;
            unlock(&fs->lock);
            err = mount->fs->mknod(mount, path, S_IFSOCK | mode, 0);
            if (err < 0)
                goto out;
            err = mount->fs->stat(mount, path, &stat, true);
            if (err < 0)
                goto out;
        }
    }

    // If something other than bind was called, just do the obvious thing and
    // fail if stat failed.
    if (!(flag & O_CREAT_) && err < 0)
        goto out;

    if (!S_ISSOCK(stat.mode)) {
        err = _ENOTSOCK;
        goto out;
    }

    // Look up the socket ID for the inode number.
    struct inode_data *inode = inode_get(mount, stat.inode);
    lock(&inode->lock);
    if (inode->socket_id == 0) {
        static uint32_t next_socket_id = 0;
        static lock_t next_socket_id_lock = LOCK_INITIALIZER;
        lock(&next_socket_id_lock);
        inode->socket_id = ++next_socket_id;
        unlock(&next_socket_id_lock);
    }
    unlock(&inode->lock);

    mount_release(mount);
    return inode;

out:
    mount_release(mount);
    return ERR_PTR(err);
}

static int sockaddr_read_get_inode(addr_t sockaddr_addr, void *sockaddr, uint_t *sockaddr_len, struct inode_data **inode_out, int flag) {
    // Make sure we can read things without overflowing buffers
    if (*sockaddr_len < sizeof(socklen_t))
        return _EINVAL;
    if (*sockaddr_len > sizeof(struct sockaddr_max_))
        return _EINVAL;

    if (user_read(sockaddr_addr, sockaddr, *sockaddr_len))
        return _EFAULT;
    struct sockaddr *real_addr = sockaddr;
    struct sockaddr_ *fake_addr = sockaddr;
    real_addr->sa_family = sock_family_to_real(fake_addr->family);

    switch (real_addr->sa_family) {
        case PF_INET:
            if (*sockaddr_len < sizeof(struct sockaddr_in))
                return _EINVAL;
            break;
        case PF_INET6:
            if (*sockaddr_len < sizeof(struct sockaddr_in6))
                return _EINVAL;
            break;

        case PF_LOCAL: {
            // First pull out the path, being careful to not overflow anything.
            char path[sizeof(struct sockaddr_max_) - offsetof(struct sockaddr_max_, data) + 1]; // big enough
            size_t addr_path_size = *sockaddr_len - offsetof(struct sockaddr_, data);
            memcpy(path, fake_addr->data, addr_path_size);
            path[addr_path_size] = '\0';

            struct inode_data *inode = unix_socket_get(path, flag);
            if (IS_ERR(inode))
                return PTR_ERR(inode);
            *inode_out = inode;

            struct sockaddr_un *real_addr_un = sockaddr;
            size_t path_len = sprintf(real_addr_un->sun_path, "%s%d", "/tmp/ishsock", inode->socket_id);
            // The call to real bind will fail if the backing socket already
            // exists from a previous run or something. We already checked that
            // the fake file doesn't exist in unix_socket_get, so try a simple
            // solution.
            if (flag & O_CREAT_)
                unlink(real_addr_un->sun_path);
            *sockaddr_len = offsetof(struct sockaddr_un, sun_path) + path_len;
            break;
        }
        default:
            return _EINVAL;
    }
    return 0;
}

static int sockaddr_read(addr_t sockaddr_addr, void *sockaddr, uint_t *sockaddr_len) {
    struct inode_data *inode = NULL;
    int err = sockaddr_read_get_inode(sockaddr_addr, sockaddr, sockaddr_len, &inode, 0);
    inode_release_if_exist(inode);
    return err;
}

static int sockaddr_write(addr_t sockaddr_addr, void *sockaddr, uint_t *sockaddr_len) {
    struct sockaddr *real_addr = sockaddr;
    struct sockaddr_ *fake_addr = sockaddr;
    fake_addr->family = sock_family_from_real(real_addr->sa_family);
    size_t len = sizeof(struct sockaddr_);
    switch (fake_addr->family) {
        case PF_INET_:
            len = sizeof(struct sockaddr_in);
            break;
        case PF_INET6_:
            len = sizeof(struct sockaddr_in6);
            break;
        case PF_LOCAL_: {
            // Most callers of sockaddr_write use it to return a peer name, and
            // since we don't know the peer name in this case, just return the
            // default peer name, which is the null address.
            static struct sockaddr_ unix_domain_null = {.family = PF_LOCAL_};
            sockaddr = &unix_domain_null;
            break;
        }
        default:
            return _EINVAL;
    }

    if (*sockaddr_len > len)
        *sockaddr_len = len;
    // The address is supposed to be truncated if the specified length is too
    // short, instead of returning an error.
    if (user_write(sockaddr_addr, sockaddr, *sockaddr_len))
        return _EFAULT;
    return 0;
}

int_t sys_bind(fd_t sock_fd, addr_t sockaddr_addr, uint_t sockaddr_len) {
    STRACE("bind(%d, 0x%x, %d)", sock_fd, sockaddr_addr, sockaddr_len);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    struct sockaddr_max_ sockaddr;
    struct inode_data *inode = NULL;
    int err = sockaddr_read_get_inode(sockaddr_addr, &sockaddr, &sockaddr_len, &inode, O_CREAT_);
    if (err < 0)
        return err;

    err = bind(sock->real_fd, (void *) &sockaddr, sockaddr_len);
    if (err < 0) {
        inode_release_if_exist(inode);
        return errno_map();
    }
    sock->socket.unix_name_inode = inode;
    return 0;
}

int_t sys_connect(fd_t sock_fd, addr_t sockaddr_addr, uint_t sockaddr_len) {
    STRACE("connect(%d, 0x%x, %d)", sock_fd, sockaddr_addr, sockaddr_len);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    struct sockaddr_max_ sockaddr;
    struct inode_data *inode = NULL;
    int err = sockaddr_read_get_inode(sockaddr_addr, &sockaddr, &sockaddr_len, &inode, 0);
    if (err < 0)
        return err;

    err = connect(sock->real_fd, (void *) &sockaddr, sockaddr_len);
    if (err < 0) {
        inode_release_if_exist(inode);
        return errno_map();
    }
    sock->socket.unix_peer_inode = inode;
    return err;
}

int_t sys_listen(fd_t sock_fd, int_t backlog) {
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

int_t sys_accept(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
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

    int err = sockaddr_write(sockaddr_addr, sockaddr, &sockaddr_len);
    if (err < 0)
        return client;
    if (user_put(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;

    fd_t client_f = sock_fd_create(client,
            sock->socket.domain, sock->socket.type, sock->socket.protocol);
    if (client_f < 0)
        close(client);
    return client_f;
}

int_t sys_getsockname(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
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

    int err = sockaddr_write(sockaddr_addr, sockaddr, &sockaddr_len);
    if (err < 0)
        return err;
    if (user_put(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;
    return res;
}

int_t sys_getpeername(fd_t sock_fd, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
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

    int err = sockaddr_write(sockaddr_addr, sockaddr, &sockaddr_len);
    if (err < 0)
        return err;
    if (user_put(sockaddr_len_addr, sockaddr_len))
        return _EFAULT;
    return res;
}

int_t sys_socketpair(dword_t domain, dword_t type, dword_t protocol, addr_t sockets_addr) {
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
    err = fake_sockets[0] = sock_fd_create(sockets[0], domain, type, protocol);
    if (fake_sockets[0] < 0)
        goto close_sockets;
    err = fake_sockets[1] = sock_fd_create(sockets[1], domain, type, protocol);
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

int_t sys_sendto(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, dword_t sockaddr_len) {
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
    struct sockaddr_max_ sockaddr;
    if (sockaddr_addr) {
        int err = sockaddr_read(sockaddr_addr, &sockaddr, &sockaddr_len);
        if (err < 0)
            return err;
    }

    ssize_t res = sendto(sock->real_fd, buffer, len, real_flags,
            sockaddr_addr ? (void *) &sockaddr : NULL, sockaddr_len);
    if (res < 0)
        return errno_map();
    return res;
}

int_t sys_recvfrom(fd_t sock_fd, addr_t buffer_addr, dword_t len, dword_t flags, addr_t sockaddr_addr, addr_t sockaddr_len_addr) {
    STRACE("recvfrom(%d, 0x%x, %d, %d, 0x%x, 0x%x)", sock_fd, buffer_addr, len, flags, sockaddr_addr, sockaddr_len_addr);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    int real_flags = sock_flags_to_real(flags);
    if (real_flags < 0)
        return _EINVAL;
    uint_t sockaddr_len = 0;
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
        int err = sockaddr_write(sockaddr_addr, sockaddr, &sockaddr_len);
        if (err < 0)
            return err;
    }
    if (sockaddr_len_addr != 0)
        if (user_put(sockaddr_len_addr, sockaddr_len))
            return _EFAULT;
    return res;
}

int_t sys_send(fd_t sock_fd, addr_t buf, dword_t len, int_t flags) {
    return sys_sendto(sock_fd, buf, len, flags, 0, 0);
}

int_t sys_recv(fd_t sock_fd, addr_t buf, dword_t len, int_t flags) {
    return sys_recvfrom(sock_fd, buf, len, flags, 0, 0);
}

int_t sys_shutdown(fd_t sock_fd, dword_t how) {
    STRACE("shutdown(%d, %d)", sock_fd, how);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;
    int err = shutdown(sock->real_fd, how);
    if (err < 0)
        return errno_map();
    return 0;
}

int_t sys_setsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, dword_t value_len) {
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

int_t sys_getsockopt(fd_t sock_fd, dword_t level, dword_t option, addr_t value_addr, dword_t len_addr) {
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

    if (level == SOL_SOCKET_ && (option == SO_DOMAIN_ || option == SO_TYPE_ || option == SO_PROTOCOL_)) {
        dword_t *value_p = (dword_t *) value;
        if (value_len != sizeof(*value_p))
            return _EINVAL;
        if (option == SO_DOMAIN_)
            *value_p = sock->socket.domain;
        else if (option == SO_TYPE_)
            *value_p = sock->socket.type;
        else if (option == SO_PROTOCOL_)
            *value_p = sock->socket.protocol;
    } else {
        int real_opt = sock_opt_to_real(option, level);
        if (real_opt < 0)
            return _EINVAL;
        int real_level = sock_level_to_real(level);
        if (real_level < 0)
            return _EINVAL;

        int err = getsockopt(sock->real_fd, real_level, real_opt, value, &value_len);
        if (err < 0)
            return errno_map();
    }

    if (user_put(len_addr, value_len))
        return _EFAULT;
    if (user_put(value_addr, value))
        return _EFAULT;
    return 0;
}

int_t sys_sendmsg(fd_t sock_fd, addr_t msghdr_addr, int_t flags) {
    int err;
    STRACE("sendmsg(%d, %#x, %d)", sock_fd, msghdr_addr, flags);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;

    struct msghdr msg;
    struct msghdr_ msg_fake;
    if (user_get(msghdr_addr, msg_fake))
        return _EFAULT;

    // msg_name
    struct sockaddr_max_ msg_name;
    if (msg_fake.msg_name != 0) {
        int err = sockaddr_read(msg_fake.msg_name, &msg_name, &msg_fake.msg_namelen);
        if (err < 0)
            return err;
        msg.msg_name = &msg_name;
        msg.msg_namelen = msg_fake.msg_namelen;
    }

    // msg_iovec
    struct iovec_ msg_iov_fake[msg_fake.msg_iovlen];
    if (user_get(msg_fake.msg_iov, msg_iov_fake))
        return _EFAULT;
    struct iovec msg_iov[msg_fake.msg_iovlen];
    memset(msg_iov, 0, sizeof(msg_iov));
    msg.msg_iov = msg_iov;
    msg.msg_iovlen = sizeof(msg_iov) / sizeof(msg_iov[0]);
    for (int i = 0; i < msg.msg_iovlen; i++) {
        msg_iov[i].iov_len = msg_iov_fake[i].len;
        msg_iov[i].iov_base = malloc(msg_iov_fake[i].len);
        err = _EFAULT;
        if (user_read(msg_iov_fake[i].base, msg_iov[i].iov_base, msg_iov_fake[i].len))
            goto out_free_iov;
    }

    // msg_control
    char *msg_control = NULL;
    if (msg_fake.msg_control != 0) {
        msg_control = malloc(msg_fake.msg_controllen);
        err = _EFAULT;
        if (user_read(msg_fake.msg_control, msg_control, msg_fake.msg_controllen))
            goto out_free_control;
    }
    msg.msg_control = msg_control;
    msg.msg_controllen = msg_fake.msg_controllen;

    msg.msg_flags = sock_flags_to_real(msg_fake.msg_flags);
    err = _EINVAL;
    if (msg.msg_flags < 0)
        goto out_free_control;
    int real_flags = sock_flags_to_real(flags);
    if (real_flags < 0)
        goto out_free_control;

    err = sendmsg(sock->real_fd, &msg, real_flags);
    if (err < 0)
        err = errno_map();

out_free_control:
    free(msg_control);
out_free_iov:
    for (int i = 0; i < msg.msg_iovlen; i++)
        free(msg_iov[i].iov_base);
    return err;
}

int_t sys_recvmsg(fd_t sock_fd, addr_t msghdr_addr, int_t flags) {
    STRACE("recvmsg(%d, %#x, %d)", sock_fd, msghdr_addr, flags);
    struct fd *sock = sock_getfd(sock_fd);
    if (sock == NULL)
        return _EBADF;

    struct msghdr msg;
    struct msghdr_ msg_fake;
    if (user_get(msghdr_addr, msg_fake))
        return _EFAULT;

    // msg_name
    char msg_name[msg_fake.msg_namelen];
    if (msg_fake.msg_name != 0) {
        msg.msg_name = msg_name;
        msg.msg_namelen = sizeof(msg_name);
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }

    // msg_control (no initial content)
    char msg_control[msg_fake.msg_controllen];
    msg.msg_control = msg_control;
    msg.msg_controllen = sizeof(msg_control);

    int real_flags = sock_flags_to_real(flags);
    if (real_flags < 0)
        return _EINVAL;

    // msg_iovec (no initial content)
    struct iovec_ msg_iov_fake[msg_fake.msg_iovlen];
    if (user_get(msg_fake.msg_iov, msg_iov_fake))
        return _EFAULT;
    struct iovec msg_iov[msg_fake.msg_iovlen];
    msg.msg_iov = msg_iov;
    msg.msg_iovlen = sizeof(msg_iov) / sizeof(msg_iov[0]);
    for (int i = 0; i < msg.msg_iovlen; i++) {
        msg_iov[i].iov_len = msg_iov_fake[i].len;
        msg_iov[i].iov_base = malloc(msg_iov_fake[i].len);
    }

    ssize_t res = recvmsg(sock->real_fd, &msg, real_flags);
    // don't return error quite yet, there are outstanding mallocs

    // msg_iovec (changed)
    // copy as many bytes as were received, according to the return value
    size_t n = res;
    if (res < 0)
        n = 0;
    for (int i = 0; i < msg.msg_iovlen; i++) {
        size_t chunk_size = msg_iov[i].iov_len;
        if (chunk_size > n)
            chunk_size = n;
        if (chunk_size > 0)
            if (user_write(msg_iov_fake[i].base, msg_iov[i].iov_base, chunk_size))
                return _EFAULT;
        n -= chunk_size;
        free(msg_iov[i].iov_base);
    }

    // by now the iovecs have been freed so we can return
    if (res < 0)
        return errno_map();

    // msg_name (changed)
    if (msg.msg_name != 0) {
        int err = sockaddr_write(msg_fake.msg_name, msg.msg_name, &msg_fake.msg_namelen);
        if (err < 0)
            return err;
    }
    msg_fake.msg_namelen = msg.msg_namelen;

    // msg_control (changed)
    if (msg_fake.msg_controllen != 0)
        if (user_write(msg_fake.msg_control, msg.msg_control, msg.msg_controllen))
            return _EFAULT;
    msg_fake.msg_controllen = msg.msg_controllen;

    // msg_flags (changed)
    msg_fake.msg_flags = sock_flags_from_real(msg.msg_flags);

    if (user_put(msghdr_addr, msg_fake))
        return _EFAULT;

    if (res < 0)
        return errno_map();
    return res;
}

struct mmsghdr_ {
    struct msghdr_ hdr;
    uint_t len;
};

int_t sys_sendmmsg(fd_t sock_fd, addr_t msg_vec, uint_t vec_len, int_t flags) {
    int num_sent = 0;
    for (unsigned i = 0; i < vec_len; i++) {
        addr_t msghdr = msg_vec + i * sizeof(struct mmsghdr_);
        int_t res = sys_sendmsg(sock_fd, msghdr, flags);
        if (res >= 0) {
            addr_t msg_len_addr = msghdr + offsetof(struct mmsghdr_, len);
            if (user_put(msg_len_addr, res))
                res = _EFAULT;
        }
        if (res < 0) {
            // From the man page:
            // If an error occurs after at least one message has been sent, the
            // call succeeds, and returns the number of messages sent.  The
            // error code is lost.
            if (num_sent > 0)
                break;
            return res;
        }
        num_sent++;
        if (res == 0) {
            // This means the socket is non-blocking and can't be written to anymore.
            break;
        }
    }
    return num_sent;
}

static void sock_translate_err(struct fd *fd, int *err) {
    // on ios, when the device goes to sleep, all connected sockets are killed.
    // reads/writes return ENOTCONN, which I'm pretty sure is a violation of
    // posix. so instead, detect this and return ECONNRESET.
    if (*err == _ENOTCONN) {
        struct sockaddr addr;
        socklen_t len = sizeof(addr);
        if (getpeername(fd->real_fd, &addr, &len) < 0 && errno == EINVAL) {
            *err = _ECONNRESET;
        }
    }
}

static ssize_t sock_read(struct fd *fd, void *buf, size_t size) {
    int err = realfs_read(fd, buf, size);
    sock_translate_err(fd, &err);
    return err;
}

static ssize_t sock_write(struct fd *fd, const void *buf, size_t size) {
    int err = realfs_write(fd, buf, size);
    sock_translate_err(fd, &err);
    return err;
}

static int sock_close(struct fd *fd) {
    sockrestart_end_listen(fd);
    inode_release_if_exist(fd->socket.unix_name_inode);
    inode_release_if_exist(fd->socket.unix_peer_inode);
    return realfs_close(fd);
}

const struct fd_ops socket_fdops = {
    .read = sock_read,
    .write = sock_write,
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
    {(syscall_t) sys_send, 4}, // send
    {(syscall_t) sys_recv, 4}, // recv
    {(syscall_t) sys_sendto, 6},
    {(syscall_t) sys_recvfrom, 6},
    {(syscall_t) sys_shutdown, 2},
    {(syscall_t) sys_setsockopt, 5},
    {(syscall_t) sys_getsockopt, 5},
    {(syscall_t) sys_sendmsg, 3},
    {(syscall_t) sys_recvmsg, 3},
    {NULL}, // accept4
    {NULL}, // recvmmsg
    {(syscall_t) sys_sendmmsg, 4},
};

int_t sys_socketcall(dword_t call_num, addr_t args_addr) {
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
