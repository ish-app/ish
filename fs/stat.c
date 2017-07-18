#include <sys/stat.h>
#include <limits.h>

#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/fs.h"

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

int generic_stat(const char *pathname, struct statbuf *stat, bool follow_links) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount(path);
    return mount->fs->stat(path_in_mount(path, mount), stat, follow_links);
}

static dword_t sys_stat_path(addr_t pathname_addr, addr_t statbuf_addr, bool follow_links) {
    int err;
    char pathname[MAX_PATH];
    user_get_string(pathname_addr, pathname, sizeof(pathname));
    struct statbuf stat;
    if ((err = generic_stat(pathname, &stat, follow_links)) < 0)
        return err;
    struct newstat64 newstat = stat_convert_newstat64(stat);
    user_put_count(statbuf_addr, &newstat, sizeof(newstat));
    return 0;
}

dword_t sys_stat64(addr_t pathname_addr, addr_t statbuf_addr) {
    return sys_stat_path(pathname_addr, statbuf_addr, false);
}

dword_t sys_lstat64(addr_t pathname_addr, addr_t statbuf_addr) {
    return sys_stat_path(pathname_addr, statbuf_addr, true);
}

dword_t sys_fstat64(fd_t fd_no, addr_t statbuf_addr) {
    struct fd *fd = current->files[fd_no];
    if (fd == NULL)
        return _EBADF;
    struct statbuf stat;
    int err = fd->ops->stat(fd, &stat);
    if (err < 0)
        return err;
    struct newstat64 newstat = stat_convert_newstat64(stat);
    user_put_count(statbuf_addr, &newstat, sizeof(newstat));
    return 0;
}
