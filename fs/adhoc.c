#include <string.h>
#include <sys/stat.h>
#include "debug.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "kernel/errno.h"
#include "fs/real.h"
#include <unistd.h>

static struct mount adhoc_mount;

struct fd *adhoc_fd_create(const struct fd_ops *ops) {
    struct fd *fd = fd_create(ops);
    if (fd == NULL)
        return NULL;
    mount_retain(&adhoc_mount);
    fd->mount = &adhoc_mount;
    fd->stat = (struct statbuf) {};
    return fd;
}

static int adhoc_fstat(struct fd *fd, struct statbuf *stat) {
    *stat = fd->stat;
    if (fd->ops == &realfs_fdops && S_ISREG(fd->stat.mode)) {
        // an anonymous regular file (memfd): its size lives in the host file
        struct stat real_stat;
        if (fstat(fd->real_fd, &real_stat) == 0) {
            stat->size = real_stat.st_size;
            stat->blocks = real_stat.st_blocks;
        }
    }
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
            if (fd->ops == &realfs_fdops && S_ISREG(fd->stat.mode)) {
                if (ftruncate(fd->real_fd, attr.size) < 0)
                    return errno_map();
                break;
            }
            return _EINVAL;
    }
    return 0;
}

static int adhoc_getpath(struct fd *fd, char *buf) {
    const char *type = "unknown"; // TODO allow this to be customized
    if (fd->stat.inode == 0)
        sprintf(buf, "anon_inode:[%s]", type);
    else
        sprintf(buf, "%s:[%lu]", type, (unsigned long) fd->stat.inode);
    return 0;
}

bool is_adhoc_fd(struct fd *fd) {
    return fd->mount == &adhoc_mount;
}

static const struct fs_ops adhoc_fs = {
    .magic = 0x09041934, // FIXME wrong for pipes and sockets
    .fstat = adhoc_fstat,
    .fsetattr = adhoc_fsetattr,
    .getpath = adhoc_getpath,
};

static struct mount adhoc_mount = {
    .fs = &adhoc_fs,
    .point = "",
};
