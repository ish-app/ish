#include "debug.h"
#include <string.h>
#include <sys/stat.h>
#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/process.h"
#include "sys/fs.h"

static fd_t fd_next() {
    for (fd_t fd = 0; fd < MAX_FD; fd++)
        if (current->files[fd] == NULL)
            return fd;
    return -1;
}

// TODO unshittify names
struct fd *fd_create() {
    struct fd *fd = malloc(sizeof(struct fd));
    if (fd == NULL)
        return NULL;
    fd->refcnt = 1;
    fd->flags = 0;
    fd->mount = NULL;
    list_init(&fd->poll_fds);
    return fd;
}

// TODO ENAMETOOLONG

dword_t sys_access(addr_t pathname_addr, dword_t mode) {
    char pathname[MAX_PATH];
    if (user_read_string(pathname_addr, pathname, sizeof(pathname)))
        return _EFAULT;
    return generic_access(pathname, mode);
}

fd_t sys_openat(fd_t dirfd, addr_t pathname_addr, dword_t flags, dword_t mode) {
    char pathname[MAX_PATH];
    if (user_read_string(pathname_addr, pathname, sizeof(pathname)))
        return _EFAULT;

    STRACE("openat(%d, \"%s\", 0x%x, 0x%x)", dirfd, pathname, flags, mode);

    if (flags & O_CREAT_)
        mode &= current->umask;

    struct fd *at;
    if (dirfd == AT_FDCWD_)
        at = current->pwd;
    else
        at = current->files[dirfd];

    fd_t fd = fd_next();
    if (fd == -1)
        return _EMFILE;
    current->files[fd] = generic_openat(at, pathname, flags, mode);
    if (IS_ERR(current->files[fd]))
        return PTR_ERR(current->files[fd]);
    return fd;
}

fd_t sys_open(addr_t pathname_addr, dword_t flags, dword_t mode) {
    return sys_openat(AT_FDCWD_, pathname_addr, flags, mode);
}

dword_t sys_readlink(addr_t pathname_addr, addr_t buf_addr, dword_t bufsize) {
    char pathname[MAX_PATH];
    if (user_read_string(pathname_addr, pathname, sizeof(pathname)))
        return _EFAULT;
    char buf[bufsize];
    int err = generic_readlink(pathname, buf, bufsize);
    if (err >= 0)
        if (user_write_string(buf_addr, buf))
            return _EFAULT;
    return err;
}

dword_t sys_unlink(addr_t pathname_addr) {
    char pathname[MAX_PATH];
    if (user_read_string(pathname_addr, pathname, sizeof(pathname)))
        return _EFAULT;
    return generic_unlink(pathname);
}

dword_t sys_close(fd_t f) {
    STRACE("close(%d)", f);
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    if (--fd->refcnt == 0) {
        int err = fd->ops->close(fd);
        if (err < 0)
            return err;
        free(fd);
    }
    current->files[f] = NULL;
    return 0;
}

dword_t sys_read(fd_t fd_no, addr_t buf_addr, dword_t size) {
    char buf[size+1];
    struct fd *fd = current->files[fd_no];
    if (fd == NULL)
        return _EBADF;
    int res = fd->ops->read(fd, buf, size);
    buf[size] = 0; // null termination for nice debug output
    STRACE("read(%d, \"%s\", %d)", fd_no, buf, size);
    if (res >= 0)
        if (user_write(buf_addr, buf, res))
            return _EFAULT;
    return res;
}

dword_t sys_readv(fd_t fd_no, addr_t iovec_addr, dword_t iovec_count) {
    struct io_vec iovecs[iovec_count];
    if (user_get(iovec_addr, iovecs))
        return _EFAULT;
    int res;
    dword_t count = 0;
    for (unsigned i = 0; i < iovec_count; i++) {
        res = sys_read(fd_no, iovecs[i].base, iovecs[i].len);
        if (iovecs[i].len != 0 && res == 0)
            break;
        if (res < 0)
            return res;
        count += res;
    }
    return count;
}

dword_t sys_write(fd_t fd_no, addr_t buf_addr, dword_t size) {
    char buf[size];
    if (user_read(buf_addr, buf, size))
        return _EFAULT;
    struct fd *fd = current->files[fd_no];
    if (fd == NULL)
        return _EBADF;
    return fd->ops->write(fd, buf, size);
}

dword_t sys_writev(fd_t fd_no, addr_t iovec_addr, dword_t iovec_count) {
    struct io_vec iovecs[iovec_count];
    if (user_get(iovec_addr, iovecs))
        return _EFAULT;
    int res;
    dword_t count = 0;
    for (unsigned i = 0; i < iovec_count; i++) {
        res = sys_write(fd_no, iovecs[i].base, iovecs[i].len);
        if (res < 0)
            return res;
        count += res;
    }
    return count;
}

// TODO doesn't work very well if off_t isn't 64 bits
dword_t sys__llseek(fd_t f, dword_t off_high, dword_t off_low, addr_t res_addr, dword_t whence) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    off_t off = ((off_t) off_high << 32) | off_low;
    off_t res = fd->ops->lseek(fd, off, whence);
    if (res < 0)
        return res;
    if (user_put(res_addr, res))
        return _EFAULT;
    return 0;
}

dword_t sys_lseek(fd_t f, dword_t off, dword_t whence) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    off_t res = fd->ops->lseek(fd, off, whence);
    if ((dword_t) res != res)
        return _EOVERFLOW;
    return res;
}

dword_t sys_ioctl(fd_t f, dword_t cmd, dword_t arg) {
    STRACE("ioctl(%d, 0x%x, 0x%x)", f, cmd, arg);
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    ssize_t size = fd->ops->ioctl_size(fd, cmd);
    if (size < 0) {
        println("unknown ioctl %x", cmd);
        return _EINVAL;
    }
    if (size == 0)
        return fd->ops->ioctl(fd, cmd, (void *) (long) arg);

    // praying that this won't break
    char buf[size];
    if (user_read(arg, buf, size))
        return _EFAULT;
    int res = fd->ops->ioctl(fd, cmd, buf);
    if (res < 0)
        return res;
    if (user_write(arg, buf, size))
        return _EFAULT;
    return res;
}

#define F_DUPFD_ 0
#define F_GETFD_ 1
#define F_SETFD_ 2
#define F_GETFL_ 3
#define F_SETFL_ 4

dword_t sys_fcntl64(fd_t f, dword_t cmd, dword_t arg) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    switch (cmd) {
        case F_DUPFD_: {
            STRACE("fcntl(%d, F_DUPFD, %d)", f, arg);
            fd_t new_fd;
            for (new_fd = arg; new_fd < MAX_FD; new_fd++)
                if (current->files[new_fd] == NULL)
                    break;
            if (new_fd == MAX_FD)
                return _EMFILE;
            current->files[new_fd] = current->files[f];
            current->files[new_fd]->refcnt++;
            return new_fd;
        }

        case F_GETFD_:
            STRACE("fcntl(%d, F_GETFD)", f);
            return fd->flags;
        case F_SETFD_:
            STRACE("fcntl(%d, F_SETFD, 0x%x)", f, arg);
            fd->flags = arg;
            return 0;

        default:
            return _EINVAL;
    }
}

dword_t sys_dup(fd_t fd) {
    if (current->files[fd] == NULL)
        return _EBADF;
    fd_t new_fd = fd_next();
    if (new_fd < 0)
        return _EMFILE;
    current->files[new_fd] = current->files[fd];
    current->files[new_fd]->refcnt++;
    return new_fd;
}

dword_t sys_dup2(fd_t fd, fd_t new_fd) {
    int res;
    if (fd == new_fd)
        return fd;
    if (current->files[fd] == NULL)
        return _EBADF;
    if (new_fd >= MAX_FD)
        return _EBADF;
    if (current->files[new_fd] != NULL) {
        res = sys_close(new_fd);
        if (res < 0)
            return res;
    }

    current->files[new_fd] = current->files[fd];
    current->files[new_fd]->refcnt++;
    return new_fd;
}

dword_t sys_getcwd(addr_t buf_addr, dword_t size) {
    char buf[MAX_PATH];
    int err = current->pwd->ops->getpath(current->pwd, buf);
    if (err < 0)
        return err;
    if (user_write(buf_addr, buf, size))
        return _EFAULT;
    return strlen(buf);
}

dword_t sys_chdir(addr_t path_addr) {
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;

    struct fd *fd = generic_open(path, O_DIRECTORY_, 0);
    if (IS_ERR(fd))
        return PTR_ERR(fd);
    generic_close(current->pwd);
    current->pwd = fd;
    return 0;
}

dword_t sys_umask(dword_t mask) {
    mode_t_ old_umask = current->umask;
    current->umask = ((mode_t_) mask) & 0777;
    return old_umask;
}

dword_t sys_fstatfs(fd_t f, addr_t stat_addr) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    struct statfs_ stat;
    int err = fd->fs->statfs(&stat);
    if (err < 0)
        return err;
    if (user_put(stat_addr, stat))
        return _EFAULT;
    return 0;
}

dword_t sys_flock(fd_t f, dword_t operation) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    return fd->fs->flock(fd, operation);
}

// a few stubs
dword_t sys_sendfile(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count) {
    return _EINVAL;
}
dword_t sys_sendfile64(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count) {
    return _EINVAL;
}
dword_t sys_mount(addr_t source_addr, addr_t target_addr, addr_t type_addr, dword_t flags, addr_t data_addr) {
    return _EINVAL; // I'm sorry, we do not support this action at this time.
}
