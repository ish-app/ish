#include "debug.h"
#include <string.h>
#include <sys/stat.h>
#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/process.h"
#include "sys/fs.h"

fd_t fd_next() {
    for (fd_t fd = 0; fd < MAX_FD; fd++)
        if (current->files[fd] == NULL)
            return fd;
    return -1;
}

struct fd *at_fd(fd_t f) {
    if (f == AT_FDCWD_)
        return current->pwd;
    return current->files[f];
}

// TODO ENAMETOOLONG

dword_t sys_access(addr_t path_addr, dword_t mode) {
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;
    return generic_access(path, mode);
}

fd_t sys_openat(fd_t at_f, addr_t path_addr, dword_t flags, dword_t mode) {
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;
    STRACE("openat(%d, \"%s\", 0x%x, 0x%x)", at_f, path, flags, mode);

    if (flags & O_CREAT_)
        mode &= ~current->umask;

    fd_t fd_no = fd_next();
    if (fd_no == -1)
        return _EMFILE;
    struct fd *at = at_fd(at_f);
    if (at == NULL)
        return _EBADF;
    struct fd *fd = generic_openat(at, path, flags, mode);
    if (IS_ERR(fd))
        return PTR_ERR(fd);
    current->files[fd_no] = fd;
    return fd_no;
}

fd_t sys_open(addr_t path_addr, dword_t flags, dword_t mode) {
    return sys_openat(AT_FDCWD_, path_addr, flags, mode);
}

dword_t sys_readlink(addr_t path_addr, addr_t buf_addr, dword_t bufsize) {
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;
    char buf[bufsize];
    int err = generic_readlink(path, buf, bufsize);
    if (err >= 0)
        if (user_write_string(buf_addr, buf))
            return _EFAULT;
    return err;
}

dword_t sys_unlinkat(fd_t at_f, addr_t path_addr) {
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;
    struct fd *at = at_fd(at_f);
    if (at == NULL)
        return _EBADF;
    return generic_unlinkat(at, path);
}

dword_t sys_unlink(addr_t path_addr) {
    return sys_unlinkat(AT_FDCWD_, path_addr);
}

dword_t sys_close(fd_t f) {
    STRACE("close(%d)", f);
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    if (--fd->refcount == 0) {
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
    char buf[size+1];
    if (user_read(buf_addr, buf, size))
        return _EFAULT;
    buf[size] = 0; // null termination for nice debug output
    STRACE("write(%d, \"%s\", %d)", fd_no, buf, size);
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
            current->files[new_fd]->refcount++;
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
    current->files[new_fd]->refcount++;
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
    current->files[new_fd]->refcount++;
    return new_fd;
}

dword_t sys_getcwd(addr_t buf_addr, dword_t size) {
    char pwd[MAX_PATH];
    int err = current->pwd->ops->getpath(current->pwd, pwd);
    if (err < 0)
        return err;

    if (strlen(pwd) + 1 > size)
        return _ERANGE;
    char buf[size];
    strcpy(buf, pwd);
    if (user_write(buf_addr, buf, sizeof(buf)))
        return _EFAULT;
    return size;
}

dword_t sys_chdir(addr_t path_addr) {
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;

    // TODO only normalize the path once

    struct statbuf stat;
    int err = generic_statat(NULL, path, &stat, true);
    if (err < 0)
        return err;
    if (!(stat.mode & S_IFDIR))
        return _ENOTDIR;

    struct fd *dir = generic_open(path, O_RDONLY_, 0);
    if (IS_ERR(dir))
        return PTR_ERR(dir);
    current->pwd = dir;
    return 0;
}

dword_t sys_umask(dword_t mask) {
    mode_t_ old_umask = current->umask;
    current->umask = ((mode_t_) mask) & 0777;
    return old_umask;
}

static dword_t statfs_mount(struct mount *mount, addr_t buf_addr) {
    struct statfsbuf stat;
    int err = mount->fs->statfs(mount, &stat);
    if (err >= 0)
        if (user_put(buf_addr, stat))
            return _EFAULT;
    return err;
}

dword_t sys_statfs64(addr_t path_addr, addr_t buf_addr) {
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;
    return statfs_mount(find_mount(path), buf_addr);
}

dword_t sys_fstatfs64(fd_t f, addr_t buf_addr) {
    return statfs_mount(current->files[f]->mount, buf_addr);
}

dword_t sys_flock(fd_t f, dword_t operation) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    return fd->mount->fs->flock(fd, operation);
}

dword_t sys_utimensat(fd_t at_f, addr_t path_addr, addr_t times_addr, dword_t flags) {
    struct timespec_ times[2];
    if (user_get(times_addr, times))
        return _EFAULT;
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;
    return 0; // TODO implement
}

dword_t sys_fchmod(fd_t f, dword_t mode) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    mode &= ~S_IFMT;
    return fd->mount->fs->fchmod(fd, mode);
}

dword_t sys_fchown32(fd_t f, dword_t owner, dword_t group) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    return fd->mount->fs->fchown(fd, owner, group);
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
