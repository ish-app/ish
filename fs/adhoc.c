#include <string.h>
#include <sys/stat.h>
#include "debug.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "kernel/errno.h"

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

static struct fd *adhoc_open(struct mount *UNUSED(mount), const char *path, int UNUSED(flags), int UNUSED(mode))  {
    if (*path != '/') {
        return ERR_PTR(_EBADF);
    }
    char fdstr[MAX_NAME];
    strcpy(fdstr, path+1);
    fd_t nfd = atol(fdstr);
    if (nfd < 0) {
        return ERR_PTR(_EBADF);
    }
    
    return fd_retain(f_get(nfd));
}

static int adhoc_stat(struct mount *UNUSED(mount), const char *path, struct statbuf *stat) {
    if (*path != '/') {
        return _EBADF;
    }
    char fdstr[MAX_NAME];
    strcpy(fdstr, path+1);
    fd_t nfd = atol(fdstr);
    if (nfd < 0) {
        return _EBADF;
    }
    struct fd* fd = f_get(nfd);
    return fd->mount->fs->fstat(fd, stat);
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
    .open = adhoc_open,
    .stat = adhoc_stat,
};

static struct mount adhoc_mount = {
    .fs = &adhoc_fs,
    .point = "",
};
