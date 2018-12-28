#include <sys/stat.h>
#include "debug.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "kernel/errno.h"

static struct mount adhoc_mount;

struct fd *adhoc_fd_create() {
    struct fd *fd = fd_create();
    if (fd == NULL)
        return NULL;
    adhoc_mount.refcount++;
    fd->mount = &adhoc_mount;
    fd->stat = (struct statbuf) {};
    return fd;
}

static int adhoc_fstat(struct fd *fd, struct statbuf *stat) {
    *stat = fd->stat;
    return 0;
}

static int adhoc_fsetattr(struct fd *fd, struct attr attr) {
    switch (attr.type) {
        case attr_uid:
            fd->stat.uid = attr.uid;
            break;
        case attr_gid:
            fd->stat.gid = attr.gid;
            break;
        case attr_mode:
            fd->stat.mode = (fd->stat.mode & S_IFMT) | (attr.mode & ~S_IFMT);
            break;
        case attr_size:
            return _EINVAL;
    }
    return 0;
}

static const struct fs_ops adhoc_fs = {
    .magic = 0x09041934, // FIXME wrong for pipes and sockets
    .fstat = adhoc_fstat,
    .fsetattr = adhoc_fsetattr,
};

static struct mount adhoc_mount = {
    .fs = &adhoc_fs,
};
