#include <fcntl.h>
#include <unistd.h>

#include "sys/errno.h"
#include "fs/fs.h"

const struct fs_ops realfs = {
    .open = realfs_open,
    .close = realfs_close,
    .readlink = realfs_readlink,
};

int realfs_open(char *path, struct fd *fd, int flags) {
    path_stringify(path);
    int fd_no = open(path, flags);
    if (fd_no < 0) return err_map(errno);
    return fd->real_fd = fd_no;
}

int realfs_close(struct fd *fd) {
    int err = close(fd->real_fd);
    if (err < 0) return err_map(errno);
    return 0;
}

ssize_t realfs_readlink(char *path, char *buf, size_t bufsize) {
    path_stringify(path);
    ssize_t size = readlink(path, buf, bufsize);
    if (size < 0) return err_map(errno);
    return size;
}
