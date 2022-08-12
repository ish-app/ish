#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sqlite3.h>

#include "debug.h"
#include "kernel/errno.h"
#include "kernel/task.h"
#include "fs/fd.h"
#include "fs/dev.h"
#include "fs/inode.h"
#include "fs/real.h"
#define ISH_INTERNAL
#include "fs/fake.h"

// TODO document database

// this exists only to override readdir to fix the returned inode numbers
static struct fd_ops fakefs_fdops;

static struct fd *fakefs_open(struct mount *mount, const char *path, int flags, int mode) {
    struct fakefs_db *fs = &mount->fakefs;
    struct fd *fd = realfs.open(mount, path, flags, 0666);
    if (IS_ERR(fd))
        return fd;
    db_begin(fs);
    fd->fake_inode = path_get_inode(fs, path);
    if (flags & O_CREAT_) {
        struct ish_stat ishstat;
        ishstat.mode = mode | S_IFREG;
        ishstat.uid = current->euid;
        ishstat.gid = current->egid;
        ishstat.rdev = 0;
        if (fd->fake_inode == 0) {
            path_create(fs, path, &ishstat);
            fd->fake_inode = path_get_inode(fs, path);
        }
    }
    db_commit(fs);
    if (fd->fake_inode == 0) {
        // metadata for this file is missing
        // TODO unlink the real file
        fd_close(fd);
        return ERR_PTR(_ENOENT);
    }
    fd->ops = &fakefs_fdops;
    return fd;
}

// WARNING: giant hack, just for file providerws
struct fd *fakefs_open_inode(struct mount *mount, ino_t inode) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    sqlite3_stmt *stmt = fs->stmt.path_from_inode;
    sqlite3_bind_int64(stmt, 1, inode);
step:
    if (!db_exec(fs, stmt)) {
        db_reset(fs, stmt);
        db_rollback(fs);
        return ERR_PTR(_ENOENT);
    }
    const char *path = (const char *) sqlite3_column_text(stmt, 0);
    struct fd *fd = realfs.open(mount, path, O_RDWR_, 0);
    if (PTR_ERR(fd) == _EISDIR)
        fd = realfs.open(mount, path, O_RDONLY_, 0);
    if (PTR_ERR(fd) == _ENOENT)
        goto step;
    db_reset(fs, stmt);
    db_commit(fs);
    fd->fake_inode = inode;
    fd->ops = &fakefs_fdops;
    return fd;
}

static int fakefs_link(struct mount *mount, const char *src, const char *dst) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    int err = realfs.link(mount, src, dst);
    if (err < 0) {
        db_rollback(fs);
        return err;
    }
    path_link(fs, src, dst);
    db_commit(fs);
    return 0;
}

static int fakefs_unlink(struct mount *mount, const char *path) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    int err = realfs.unlink(mount, path);
    if (err < 0) {
        db_rollback(fs);
        return err;
    }
    ino_t ino = path_unlink(fs, path);
    db_commit(fs);
    inode_check_orphaned(mount, ino);
    return 0;
}

static int fakefs_rmdir(struct mount *mount, const char *path) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    int err = realfs.rmdir(mount, path);
    if (err < 0) {
        db_rollback(fs);
        return err;
    }
    ino_t ino = path_unlink(fs, path);
    db_commit(fs);
    inode_check_orphaned(mount, ino);
    return 0;
}

static int fakefs_rename(struct mount *mount, const char *src, const char *dst) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    path_rename(fs, src, dst);
    int err = realfs.rename(mount, src, dst);
    if (err < 0) {
        db_rollback(fs);
        return err;
    }
    db_commit(fs);
    return 0;
}

static int fakefs_symlink(struct mount *mount, const char *target, const char *link) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    // create a file containing the target
    int fd = openat(mount->root_fd, fix_path(link), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        db_rollback(fs);
        return errno_map();
    }
    ssize_t res = write(fd, target, strlen(target));
    close(fd);
    if (res < 0) {
        int saved_errno = errno;
        unlinkat(mount->root_fd, fix_path(link), 0);
        db_rollback(fs);
        errno = saved_errno;
        return errno_map();
    }

    // customize the stat info so it looks like a link
    struct ish_stat ishstat;
    ishstat.mode = S_IFLNK | 0777; // symlinks always have full permissions
    ishstat.uid = current->euid;
    ishstat.gid = current->egid;
    ishstat.rdev = 0;
    path_create(fs, link, &ishstat);
    db_commit(fs);
    return 0;
}

static int fakefs_mknod(struct mount *mount, const char *path, mode_t_ mode, dev_t_ dev) {
    struct fakefs_db *fs = &mount->fakefs;
    mode_t_ real_mode = 0666;
    if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISSOCK(mode))
        real_mode |= S_IFREG;
    else
        real_mode |= mode & S_IFMT;
    db_begin(fs);
    int err = realfs.mknod(mount, path, real_mode, 0);
    if (err < 0) {
        db_rollback(fs);
        return err;
    }
    struct ish_stat stat;
    stat.mode = mode;
    stat.uid = current->euid;
    stat.gid = current->egid;
    stat.rdev = 0;
    if (S_ISBLK(mode) || S_ISCHR(mode))
        stat.rdev = dev;
    path_create(fs, path, &stat);
    db_commit(fs);
    return err;
}

static int fakefs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    struct ish_stat ishstat;
    ino_t inode;
    if (!path_read_stat(fs, path, &ishstat, &inode)) {
        db_rollback(fs);
        return _ENOENT;
    }
    int err = realfs.stat(mount, path, fake_stat);
    db_commit(fs);
    if (err < 0)
        return err;
    fake_stat->inode = inode;
    fake_stat->mode = ishstat.mode;
    fake_stat->uid = ishstat.uid;
    fake_stat->gid = ishstat.gid;
    fake_stat->rdev = ishstat.rdev;
    return 0;
}

static int fakefs_fstat(struct fd *fd, struct statbuf *fake_stat) {
    struct fakefs_db *fs = &fd->mount->fakefs;
    int err = realfs.fstat(fd, fake_stat);
    if (err < 0)
        return err;
    db_begin(fs);
    struct ish_stat ishstat;
    inode_read_stat(fs, fd->fake_inode, &ishstat);
    db_commit(fs);
    fake_stat->inode = fd->fake_inode;
    fake_stat->mode = ishstat.mode;
    fake_stat->uid = ishstat.uid;
    fake_stat->gid = ishstat.gid;
    fake_stat->rdev = ishstat.rdev;
    return 0;
}

static void fake_stat_setattr(struct ish_stat *ishstat, struct attr attr) {
    switch (attr.type) {
        case attr_uid:
            ishstat->uid = attr.uid;
            break;
        case attr_gid:
            ishstat->gid = attr.gid;
            break;
        case attr_mode:
            ishstat->mode = (ishstat->mode & S_IFMT) | (attr.mode & ~S_IFMT);
            break;
    }
}

static int fakefs_setattr(struct mount *mount, const char *path, struct attr attr) {
    struct fakefs_db *fs = &mount->fakefs;
    if (attr.type == attr_size)
        return realfs.setattr(mount, path, attr);
    db_begin(fs);
    struct ish_stat ishstat;
    ino_t inode;
    if (!path_read_stat(fs, path, &ishstat, &inode)) {
        db_rollback(fs);
        return _ENOENT;
    }
    fake_stat_setattr(&ishstat, attr);
    inode_write_stat(fs, inode, &ishstat);
    db_commit(fs);
    return 0;
}

static int fakefs_fsetattr(struct fd *fd, struct attr attr) {
    struct fakefs_db *fs = &fd->mount->fakefs;
    if (attr.type == attr_size)
        return realfs.fsetattr(fd, attr);
    db_begin(fs);
    struct ish_stat ishstat;
    inode_read_stat(fs, fd->fake_inode, &ishstat);
    fake_stat_setattr(&ishstat, attr);
    inode_write_stat(fs, fd->fake_inode, &ishstat);
    db_commit(fs);
    return 0;
}

static int fakefs_mkdir(struct mount *mount, const char *path, mode_t_ mode) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    int err = realfs.mkdir(mount, path, 0777);
    if (err < 0) {
        db_rollback(fs);
        return err;
    }
    struct ish_stat ishstat;
    ishstat.mode = mode | S_IFDIR;
    ishstat.uid = current->euid;
    ishstat.gid = current->egid;
    ishstat.rdev = 0;
    path_create(fs, path, &ishstat);
    db_commit(fs);
    return 0;
}

static ssize_t file_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    // broken symlinks can't be included in an iOS app or else Xcode craps out
    int fd = openat(mount->root_fd, fix_path(path), O_RDONLY);
    if (fd < 0)
        return errno_map();
    int err = read(fd, buf, bufsize);
    close(fd);
    if (err < 0)
        return errno_map();
    return err;
}

static ssize_t fakefs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    struct ish_stat ishstat;
    if (!path_read_stat(fs, path, &ishstat, NULL)) {
        db_rollback(fs);
        return _ENOENT;
    }
    if (!S_ISLNK(ishstat.mode)) {
        db_rollback(fs);
        return _EINVAL;
    }

    ssize_t err = realfs.readlink(mount, path, buf, bufsize);
    if (err == _EINVAL)
        err = file_readlink(mount, path, buf, bufsize);
    db_commit(fs);
    return err;
}

static int fakefs_readdir(struct fd *fd, struct dir_entry *entry) {
    assert(fd->ops == &fakefs_fdops);
    int res;
retry:
    res = realfs_fdops.readdir(fd, entry);
    if (res <= 0)
        return res;

    // this is annoying
    char entry_path[MAX_PATH + 1];
    realfs_getpath(fd, entry_path);
    if (strcmp(entry->name, "..") == 0) {
        if (strcmp(entry_path, "") != 0) {
            *strrchr(entry_path, '/') = '\0';
        }
    } else if (strcmp(entry->name, ".") != 0) {
        // god I don't know what to do if this would overflow
        strcat(entry_path, "/");
        strcat(entry_path, entry->name);
    }

    struct fakefs_db *fs = &fd->mount->fakefs;
    db_begin(fs);
    entry->inode = path_get_inode(fs, entry_path);
    db_commit(fs);
    // it's quite possible that due to some mishap there's no metadata for this file
    // so just skip this entry, instead of crashing the program, so there's hope for recovery
    if (entry->inode == 0)
        goto retry;
    return res;
}

static struct fd_ops fakefs_fdops;
static void __attribute__((constructor)) init_fake_fdops() {
    fakefs_fdops = realfs_fdops;
    fakefs_fdops.readdir = fakefs_readdir;
}

static int fakefs_mount(struct mount *mount) {
    char db_path[PATH_MAX];
    strcpy(db_path, mount->source);
    char *basename = strrchr(db_path, '/') + 1;
    assert(strcmp(basename, "data") == 0);
    strcpy(basename, "meta.db");

    // do this now so rebuilding can use root_fd
    int err = realfs.mount(mount);
    if (err < 0)
        return err;

    err = fake_db_init(&mount->fakefs, db_path, mount->root_fd);
    if (err < 0)
        return err;

    return 0;
}

static int fakefs_umount(struct mount *mount) {
    int err = fake_db_deinit(&mount->fakefs);
    if (err != SQLITE_OK) {
        printk("sqlite failed to close: %d\n", err);
    }
    /* return realfs.umount(mount); */
    return 0;
}

static void fakefs_inode_orphaned(struct mount *mount, ino_t inode) {
    struct fakefs_db *fs = &mount->fakefs;
    db_begin(fs);
    sqlite3_bind_int64(fs->stmt.try_cleanup_inode, 1, inode);
    db_exec_reset(fs, fs->stmt.try_cleanup_inode);
    db_commit(fs);
}

const struct fs_ops fakefs = {
    .name = "fake", .magic = 0x66616b65,
    .mount = fakefs_mount,
    .umount = fakefs_umount,
    .statfs = realfs_statfs,
    .open = fakefs_open,
    .readlink = fakefs_readlink,
    .link = fakefs_link,
    .unlink = fakefs_unlink,
    .rename = fakefs_rename,
    .symlink = fakefs_symlink,
    .mknod = fakefs_mknod,

    .close = realfs_close,
    .stat = fakefs_stat,
    .fstat = fakefs_fstat,
    .flock = realfs_flock,
    .setattr = fakefs_setattr,
    .fsetattr = fakefs_fsetattr,
    .getpath = realfs_getpath,
    .utime = realfs_utime,

    .mkdir = fakefs_mkdir,
    .rmdir = fakefs_rmdir,

    .inode_orphaned = fakefs_inode_orphaned,
};
