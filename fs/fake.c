#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <gdbm.h>

#include "debug.h"
#include "kernel/errno.h"
#include "kernel/task.h"
#include "fs/fd.h"

// TODO document database

struct ish_stat {
    dword_t mode;
    dword_t uid;
    dword_t gid;
    dword_t rdev;
};

static void gdbm_fatal(const char *thingy) {
    println("fatal gdbm error: %s", thingy);
}

static noreturn void gdbm_err(GDBM_FILE db) {
    println("gdbm error: %s", gdbm_db_strerror(db));
    abort();
}

static datum make_datum(char *data, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsprintf(data, format, args);
    va_end(args);
    return (datum) {.dptr = data, .dsize = n};
}

static datum read_meta(struct mount *mount, datum key) {
    datum value = gdbm_fetch(mount->db, key);
    if (value.dptr == NULL && gdbm_last_errno(mount->db) != GDBM_ITEM_NOT_FOUND)
        gdbm_err(mount->db);
    return value;
}

static void write_meta(struct mount *mount, datum key, datum data) {
    if (gdbm_store(mount->db, key, data, GDBM_REPLACE) == -1)
        gdbm_err(mount->db);
}

static void delete_meta(struct mount *mount, datum key) {
    if (gdbm_delete(mount->db, key) == -1 && gdbm_last_errno(mount->db) != GDBM_ITEM_NOT_FOUND)
        gdbm_err(mount->db);
}

static ino_t inode_for_path(struct mount *mount, const char *path) {
    struct stat stat;
    if (fstatat(mount->root_fd, fix_path(path), &stat, AT_SYMLINK_NOFOLLOW) < 0)
        // interestingly, both linux and darwin reserve inode number 0. linux
        // uses it for an error return and darwin uses it to mark deleted
        // directory entries (and maybe also for error returns, I don't know).
        return 0;
    return stat.st_ino;
}

static ino_t write_path(struct mount *mount, const char *path) {
    ino_t inode = inode_for_path(mount, path);
    if (inode != 0) {
        char keydata[MAX_PATH+strlen("inode")+1];
        char valuedata[30];
        write_meta(mount, 
                make_datum(keydata, "inode %s", path), 
                make_datum(valuedata, "%lu", inode));
    }
    return inode;
}

static void delete_path(struct mount *mount, const char *path) {
    char keydata[MAX_PATH+strlen("inode")+1];
    delete_meta(mount, make_datum(keydata, "inode %s", path));
}

static datum stat_key(char *data, struct mount *mount, const char *path) {
    // record the path-inode correspondence, in case there was a crash before
    // this could be recorded when the file was created
    ino_t inode = write_path(mount, path);
    if (inode == 0)
        return (datum) {};
    return make_datum(data, "stat %lu", inode);
}

static bool read_stat(struct mount *mount, const char *path, struct ish_stat *stat) {
    char keydata[30];
    datum d = read_meta(mount, stat_key(keydata, mount, path));
    if (d.dptr == NULL)
        return false;
    assert(d.dsize == sizeof(struct ish_stat));
    *stat = *(struct ish_stat *) d.dptr;
    free(d.dptr);
    return true;
}

static void write_stat(struct mount *mount, const char *path, struct ish_stat *stat) {
    char keydata[30];
    datum key = stat_key(keydata, mount, path);
    assert(key.dptr != NULL);
    datum data;
    data.dptr = (void *) stat;
    data.dsize = sizeof(struct ish_stat);
    write_meta(mount, key, data);
}

static struct fd *fakefs_open(struct mount *mount, const char *path, int flags, int mode) {
    struct fd *fd = realfs.open(mount, path, flags, 0666);
    if (IS_ERR(fd))
        return fd;
    if (flags & O_CREAT_) {
        struct ish_stat ishstat;
        ishstat.mode = mode | S_IFREG;
        ishstat.uid = current->uid;
        ishstat.gid = current->gid;
        ishstat.rdev = 0;
        write_stat(mount, path, &ishstat);
    }
    return fd;
}

static int fakefs_link(struct mount *mount, const char *src, const char *dst) {
    int err = realfs.link(mount, src, dst);
    if (err < 0)
        return err;
    write_path(mount, dst);
    return 0;
}

static int fakefs_unlink(struct mount *mount, const char *path) {
    // find out if this is the last link
    bool gone = false;
    int fd = openat(mount->root_fd, fix_path(path), O_RDONLY);
    struct stat stat;
    if (fd >= 0 && fstat(fd, &stat) >= 0 && stat.st_nlink == 1)
        gone = true;
    if (fd >= 0)
        close(fd);

    char keydata[30];
    datum key = stat_key(keydata, mount, path);
    int err = realfs.unlink(mount, path);
    if (err < 0)
        return err;
    delete_path(mount, path);
    if (gone)
        delete_meta(mount, key);
    return 0;
}

static int fakefs_rmdir(struct mount *mount, const char *path) {
    char keydata[30];
    datum key = stat_key(keydata, mount, path);
    int err = realfs.rmdir(mount, path);
    if (err < 0)
        return err;
    delete_path(mount, path);
    delete_meta(mount, key);
    return 0;
}

static int fakefs_rename(struct mount *mount, const char *src, const char *dst) {
    int err = realfs.rename(mount, src, dst);
    if (err < 0)
        return err;
    write_path(mount, dst);
    delete_path(mount, src);
    return 0;
}

static int fakefs_symlink(struct mount *mount, const char *target, const char *link) {
    // create a file containing the target
    int fd = openat(mount->root_fd, fix_path(link), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        return errno_map();
    ssize_t res = write(fd, target, strlen(target));
    close(fd);
    if (res < 0) {
        unlinkat(mount->root_fd, fix_path(link), 0);
        return errno_map();
    }

    // customize the stat info so it looks like a link
    struct ish_stat ishstat;
    ishstat.mode = S_IFLNK | 0777; // symlinks always have full permissions
    ishstat.uid = current->uid;
    ishstat.gid = current->gid;
    ishstat.rdev = 0;
    write_stat(mount, link, &ishstat);
    return 0;
}

static int fakefs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat, bool follow_links) {
    struct ish_stat ishstat;
    if (!read_stat(mount, path, &ishstat))
        return _ENOENT;
    int err = realfs.stat(mount, path, fake_stat, follow_links);
    if (err < 0)
        return err;
    fake_stat->mode = ishstat.mode;
    fake_stat->uid = ishstat.uid;
    fake_stat->gid = ishstat.gid;
    fake_stat->rdev = ishstat.rdev;
    return 0;
}

static int fakefs_fstat(struct fd *fd, struct statbuf *fake_stat) {
    // this is truly sad, but there is no alternative
    char path[MAX_PATH];
    int err = fd->ops->getpath(fd, path);
    if (err < 0)
        return err;
    return fakefs_stat(fd->mount, path, fake_stat, false);
}

static int fakefs_setattr(struct mount *mount, const char *path, struct attr attr) {
    struct ish_stat ishstat;
    if (!read_stat(mount, path, &ishstat))
        return _ENOENT;
    switch (attr.type) {
        case attr_uid:
            ishstat.uid = attr.uid;
            break;
        case attr_gid:
            ishstat.gid = attr.gid;
            break;
        case attr_mode:
            ishstat.mode = (ishstat.mode & S_IFMT) | (attr.mode & ~S_IFMT);
            break;
        case attr_size:
            return realfs_truncate(mount, path, attr.size);
    }
    write_stat(mount, path, &ishstat);
    return 0;
}

static int fakefs_fsetattr(struct fd *fd, struct attr attr) {
    char path[MAX_PATH];
    int err = fd->ops->getpath(fd, path);
    if (err < 0)
        return err;
    return fakefs_setattr(fd->mount, path, attr);
}

static int fakefs_mkdir(struct mount *mount, const char *path, mode_t_ mode) {
    int err = realfs.mkdir(mount, path, 0777);
    if (err < 0)
        return err;
    struct ish_stat ishstat;
    ishstat.mode = mode | S_IFDIR;
    ishstat.uid = current->uid;
    ishstat.gid = current->gid;
    ishstat.rdev = 0;
    write_stat(mount, path, &ishstat);
    return 0;
}

static ssize_t fakefs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    struct ish_stat ishstat;
    if (!read_stat(mount, path, &ishstat))
        return _ENOENT;
    if (!S_ISLNK(ishstat.mode))
        return _EINVAL;

    ssize_t err = realfs.readlink(mount, path, buf, bufsize);
    if (err == _EINVAL) {
        // broken symlinks can't be included in an iOS app or else Xcode craps out
        int fd = openat(mount->root_fd, fix_path(path), O_RDONLY);
        if (fd < 0)
            return errno_map();
        err = read(fd, buf, bufsize);
        if (err < 0)
            return errno_map();
        close(fd);
        return err;
    }
    return err;
}

int fakefs_rebuild(struct mount *mount, const char *db_path);

static int fakefs_mount(struct mount *mount) {
    char db_path[PATH_MAX];
    strcpy(db_path, mount->source);
    char *basename = strrchr(db_path, '/') + 1;
    assert(strcmp(basename, "data") == 0);
    strcpy(basename, "meta.db");
    mount->db = gdbm_open(db_path, 0, GDBM_WRITER, 0, gdbm_fatal);
    if (mount->db == NULL) {
        println("gdbm error: %s", gdbm_strerror(gdbm_errno));
        return _EINVAL;
    }

    // do this now so fakefs_rebuild can use mount->root_fd
    int err = realfs.mount(mount);
    if (err < 0)
        return err;

    // after the filesystem is compressed, transmitted, and uncompressed, the
    // inode numbers will be different. to detect this, the inode of the
    // database file is stored inside the database and compared with the actual
    // database file inode, and if they're different we rebuild the database.
    struct stat stat;
    if (fstat(gdbm_fdesc(mount->db), &stat) < 0) DIE("fstat database");
    datum key = {.dptr = "db inode", .dsize = strlen("db inode")};
    datum value = gdbm_fetch(mount->db, key);
    if (value.dptr != NULL) {
        if (atol(value.dptr) != stat.st_ino) {
            int err = fakefs_rebuild(mount, db_path);
            if (err < 0) {
                close(mount->root_fd);
                return err;
            }
        }
        free(value.dptr);
    }

    char keydata[30];
    value = make_datum(keydata, "%lu", (unsigned long) stat.st_ino);
    value.dsize++; // make sure to null terminate
    gdbm_store(mount->db, key, value, GDBM_REPLACE);

    return 0;
}

static int fakefs_umount(struct mount *mount) {
    if (mount->data)
        gdbm_close(mount->data);
    /* return realfs.umount(mount); */
    return 0;
}

const struct fs_ops fakefs = {
    .mount = fakefs_mount,
    .umount = fakefs_umount,
    .statfs = realfs_statfs,
    .open = fakefs_open,
    .readlink = fakefs_readlink,
    .link = fakefs_link,
    .unlink = fakefs_unlink,
    .rename = fakefs_rename,
    .symlink = fakefs_symlink,
    
    .stat = fakefs_stat,
    .fstat = fakefs_fstat,
    .flock = realfs_flock,
    .setattr = fakefs_setattr,
    .fsetattr = fakefs_fsetattr,
    .utime = realfs_utime,

    .mkdir = fakefs_mkdir,
    .rmdir = fakefs_rmdir,
};
