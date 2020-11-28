#include <string.h>
#include <sys/stat.h>
#include <limits.h>

#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/path.h"

struct newstat64 stat_convert_newstat64(struct statbuf stat) {
    struct newstat64 newstat;
    newstat.dev = stat.dev;
    newstat.fucked_ino = stat.inode;
    newstat.ino = stat.inode;
    newstat.mode = stat.mode;
    newstat.nlink = stat.nlink;
    newstat.uid = stat.uid;
    newstat.gid = stat.gid;
    newstat.rdev = stat.rdev;
    newstat.size = stat.size;
    newstat.blksize = stat.blksize;
    newstat.blocks = stat.blocks;
    newstat.atime = stat.atime;
    newstat.atime_nsec = stat.atime_nsec;
    newstat.mtime = stat.mtime;
    newstat.mtime_nsec = stat.mtime_nsec;
    newstat.ctime = stat.ctime;
    newstat.ctime_nsec = stat.ctime_nsec;
    return newstat;
}

int generic_statat(struct fd *at, const char *path_raw, struct statbuf *stat, bool follow_links) {
    char path[MAX_PATH];
    int err = path_normalize(at, path_raw, path, follow_links ? N_SYMLINK_FOLLOW : N_SYMLINK_NOFOLLOW);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    memset(stat, 0, sizeof(*stat));
    err = mount->fs->stat(mount, path, stat);
    mount_release(mount);
    return err;
}

// TODO get rid of this and maybe everything else in the file
static struct fd *at_fd(fd_t f) {
    if (f == AT_FDCWD_)
        return AT_PWD;
    return f_get(f);
}

static dword_t sys_stat_path(fd_t at_f, addr_t path_addr, addr_t statbuf_addr, bool follow_links) {
    int err;
    char path[MAX_PATH];
    if (user_read_string(path_addr, path, sizeof(path)))
        return _EFAULT;
    STRACE("stat(at=%d, path=\"%s\", statbuf=0x%x, follow_links=%d)", at_f, path, statbuf_addr, follow_links);
    struct fd *at = at_fd(at_f);
    if (at == NULL)
        return _EBADF;
    struct statbuf stat = {};
    if ((err = generic_statat(at, path, &stat, follow_links)) < 0)
        return err;
    struct newstat64 newstat = stat_convert_newstat64(stat);
    if (user_put(statbuf_addr, newstat))
        return _EFAULT;
    return 0;
}

dword_t sys_stat64(addr_t path_addr, addr_t statbuf_addr) {
    return sys_stat_path(AT_FDCWD_, path_addr, statbuf_addr, true);
}

dword_t sys_lstat64(addr_t path_addr, addr_t statbuf_addr) {
    return sys_stat_path(AT_FDCWD_, path_addr, statbuf_addr, false);
}

dword_t sys_fstatat64(fd_t at, addr_t path_addr, addr_t statbuf_addr, dword_t flags) {
    return sys_stat_path(at, path_addr, statbuf_addr, !(flags & AT_SYMLINK_NOFOLLOW_));
}

dword_t sys_fstat64(fd_t fd_no, addr_t statbuf_addr) {
    STRACE("fstat64(%d, 0x%x)", fd_no, statbuf_addr);
    struct fd *fd = f_get(fd_no);
    if (fd == NULL)
        return _EBADF;
    struct statbuf stat = {};
    int err = fd->mount->fs->fstat(fd, &stat);
    if (err < 0)
        return err;
    struct newstat64 newstat = stat_convert_newstat64(stat);
    if (user_put(statbuf_addr, newstat))
        return _EFAULT;
    return 0;
}
