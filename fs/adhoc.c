#include <sys/stat.h>
#include "sys/fs.h"

static struct mount adhoc_mount;

struct fd *adhoc_fd_create() {
    struct fd *fd = fd_create();
    if (fd == NULL)
        return NULL;
    fd->mount = &adhoc_mount;
    fd->stat = malloc(sizeof(struct statbuf));
    *fd->stat = (struct statbuf) {};
    return fd;
}

static int adhoc_fstat(struct fd *fd, struct statbuf *stat) {
    *stat = *fd->stat;
    return 0;
}

static int adhoc_fchmod(struct fd *fd, mode_t_ mode) {
    fd->stat->mode = (fd->stat->mode & S_IFMT) | mode;
    return 0;
}

static int adhoc_fchown(struct fd *fd, uid_t_ owner, uid_t_ group) {
    fd->stat->uid = owner;
    fd->stat->gid = group;
    return 0;
}

static const struct fs_ops adhoc_fs = {
    .fstat = adhoc_fstat,
    .fchmod = adhoc_fchmod,
    .fchown = adhoc_fchown,
};

static struct mount adhoc_mount = {
    .fs = &adhoc_fs,
};
