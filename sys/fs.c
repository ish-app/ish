#include "sys/calls.h"
#include "sys/errno.h"
#include "emu/process.h"
#include "fs/fs.h"

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

fd_t sys_open(addr_t pathname_addr, dword_t flags) {
    int err;
    char pathname[MAX_PATH];
    user_get_string(pathname_addr, pathname, sizeof(pathname));

    fd_t fd = create_fd();
    if (fd == -1)
        return _EMFILE;
    if ((err = generic_open(pathname, current->files[fd], flags)) < 0)
        return err;
    return fd;
}

dword_t sys_close(fd_t f) {
    if (current->files[f] == NULL)
        return _EBADF;
    struct fd *fd = current->files[f];
    if (--fd->refcnt == 0) {
        int err = fd->fs->close(fd);
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
    int res = fd->fs->read(fd, buf, size);
    if (res >= 0)
        user_put_count(buf_addr, buf, res);
    return res;
}

dword_t sys_write(fd_t fd_no, addr_t buf_addr, dword_t size) {
    char buf[size];
    user_get_count(buf_addr, buf, size);
    struct fd *fd = current->files[fd_no];
    return fd->fs->write(fd, buf, size);
}

dword_t sys_readlink(addr_t pathname_addr, addr_t buf_addr, dword_t bufsize) {
    return _ENOENT;
}

dword_t sys_dup(fd_t fd) {
    if (current->files[fd] == NULL)
        return _EBADF;
    fd_t new_fd = find_fd();
    if (new_fd < 0)
        return _EMFILE;
    current->files[new_fd] = current->files[fd];
    return 0;
}
