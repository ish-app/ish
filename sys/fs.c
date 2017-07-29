#include <string.h>
#include <sys/stat.h>
#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/process.h"
#include "sys/fs.h"

fd_t find_fd() {
    for (fd_t fd = 0; fd < MAX_FD; fd++)
        if (current->files[fd] == NULL) {
            return fd;
        }
    return -1;
}

fd_t create_fd() {
    fd_t fd = find_fd();
    current->files[fd] = malloc(sizeof(struct fd));
    current->files[fd]->refcnt = 1;
    return fd;
}

// TODO ENAMETOOLONG

dword_t sys_access(addr_t pathname_addr, dword_t mode) {
    char pathname[MAX_PATH];
    user_get_string(pathname_addr, pathname, sizeof(pathname));
    return generic_access(pathname, mode);
}

fd_t sys_open(addr_t pathname_addr, dword_t flags, dword_t mode) {
    int err;
    char pathname[MAX_PATH];
    user_get_string(pathname_addr, pathname, sizeof(pathname));

    fd_t fd = create_fd();
    if (fd == -1)
        return _EMFILE;
    if ((err = generic_open(pathname, current->files[fd], flags, mode)) < 0)
        return err;
    return fd;
}

dword_t sys_readlink(addr_t pathname_addr, addr_t buf_addr, dword_t bufsize) {
    char pathname[MAX_PATH];
    user_get_string(pathname_addr, pathname, sizeof(pathname));
    char buf[bufsize];
    int err = generic_readlink(pathname, buf, bufsize);
    if (err >= 0)
        user_put_string(buf_addr, buf);
    return err;
}

dword_t sys_unlink(addr_t pathname_addr) {
    char pathname[MAX_PATH];
    user_get_string(pathname_addr, pathname, sizeof(pathname));
    return generic_unlink(pathname);
}

dword_t sys_close(fd_t f) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    if (--fd->refcnt == 0) {
        int err = fd->ops->close(fd);
        if (err < 0)
            return err;
        free(fd);
        current->files[f] = NULL;
    }
    return 0;
}

dword_t sys_read(fd_t fd_no, addr_t buf_addr, dword_t size) {
    char buf[size];
    struct fd *fd = current->files[fd_no];
    if (fd == NULL)
        return _EBADF;
    int res = fd->ops->read(fd, buf, size);
    if (res >= 0)
        user_put_count(buf_addr, buf, res);
    return res;
}

dword_t sys_readv(fd_t fd_no, addr_t iovec_addr, dword_t iovec_count) {
    struct io_vec iovecs[iovec_count];
    user_get_count(iovec_addr, iovecs, sizeof(iovecs));
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
    user_get_count(buf_addr, buf, size);
    struct fd *fd = current->files[fd_no];
    if (fd == NULL)
        return _EBADF;
    return fd->ops->write(fd, buf, size);
}

dword_t sys_writev(fd_t fd_no, addr_t iovec_addr, dword_t iovec_count) {
    struct io_vec iovecs[iovec_count];
    user_get_count(iovec_addr, iovecs, sizeof(iovecs));
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
    user_put_count(res_addr, &res, sizeof(res));
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
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;
    ssize_t size = fd->ops->ioctl_size(fd, cmd);
    if (size < 0) {
        printf("unknown ioctl %x\n", cmd);
        return _EINVAL;
    }
    if (size == 0)
        return fd->ops->ioctl(fd, cmd, (void *) (long) arg);

    // praying that this won't break
    char buf[size];
    user_get_count(arg, buf, size);
    int res = fd->ops->ioctl(fd, cmd, buf);
    if (res < 0)
        return res;
    user_put_count(arg, buf, size);
    return res;
}

dword_t sys_dup(fd_t fd) {
    if (current->files[fd] == NULL)
        return _EBADF;
    fd_t new_fd = find_fd();
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
    char buf[size];
    if (getcwd(buf, size) == NULL)
        return err_map(errno);
    size_t len = strlen(buf) + 1;
    user_put_count(buf_addr, buf, len);
    return len;
}

dword_t sys_chdir(addr_t path_addr) {
    char path[MAX_PATH];
    user_get_string(path_addr, path, sizeof(path));

    struct statbuf stat;
    int err = generic_stat(path, &stat, true);
    if (err < 0)
        return err;
    if (!(stat.mode & S_IFDIR))
        return _ENOTDIR;
    path_normalize(path, current->pwd, true);
    return 0;
}

// a few stubs
dword_t sys_sendfile(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count) {
    return _EINVAL;
}
dword_t sys_sendfile64(fd_t out_fd, fd_t in_fd, addr_t offset_addr, dword_t count) {
    return _EINVAL;
}
