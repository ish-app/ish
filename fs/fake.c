#include <limits.h>
#ifndef GDBM_NDBM
#include <ndbm.h>
#else
#include <gdbm-ndbm.h>
#endif
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "kernel/errno.h"
#include "kernel/process.h"
#include "kernel/fs.h"

struct ish_stat {
    dword_t mode;
    dword_t uid;
    dword_t gid;
    dword_t rdev;
};

static DBM *get_db(struct mount *mount) {
    DBM *db = mount->data;
    if (db == NULL) {
        char db_path[PATH_MAX];
        strcpy(db_path, mount->source);
        char *basename = strrchr(db_path, '/') + 1;
        assert(strcmp(basename, "data") == 0);
        strncpy(basename, "meta", 4);
        db = dbm_open(db_path, O_RDWR, 0666);
        assert(db != NULL);
        mount->data = db;
    }
    return db;
}

static datum read_meta(struct mount *mount, const char *path, const char *type) {
    DBM *db = get_db(mount);
    char keydata[MAX_PATH+strlen(type)+1];
    strcpy(keydata, type);
    strcat(keydata, ":");
    strcat(keydata, path);
    datum key = {.dptr = keydata, .dsize = strlen(keydata)};
    return dbm_fetch(db, key);
}

static int read_ish_stat(struct mount *mount, const char *path, struct ish_stat *stat) {
    datum d = read_meta(mount, path, "meta");
    if (d.dptr == NULL)
        return 0;
    assert(d.dsize == sizeof(struct ish_stat));
    *stat = *(struct ish_stat *) d.dptr;
    return 1;
}

static int write_meta(struct mount *mount, const char *path, const char *type, datum data) {
    DBM *db = get_db(mount);
    char keydata[MAX_PATH+strlen(type)+1];
    strcpy(keydata, type);
    strcat(keydata, ":");
    strcat(keydata, path);
    datum key = {.dptr = keydata, .dsize = strlen(keydata)};
    return dbm_store(db, key, data, DBM_REPLACE);
}

static int write_stat(struct mount *mount, const char *path, struct ish_stat *ishstat) {
    datum data;
    data.dptr = (void *) ishstat;
    data.dsize = sizeof(struct ish_stat);
    return write_meta(mount, path, "meta", data);
}

static struct fd *fakefs_open(struct mount *mount, char *path, int flags, int mode) {
    struct fd *fd = realfs_open(mount, path, flags, mode);
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

static int fakefs_stat(struct mount *mount, char *path, struct statbuf *fake_stat, bool follow_links) {
    struct ish_stat ishstat;
    if (!read_ish_stat(mount, path, &ishstat))
        return _ENOENT;
    int err = realfs_stat(mount, path, fake_stat, follow_links);
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

ssize_t fakefs_readlink(struct mount *mount, char *path, char *buf, size_t bufsize) {
    struct ish_stat ishstat;
    if (!read_ish_stat(mount, path, &ishstat))
        return _ENOENT;
    if (!S_ISLNK(ishstat.mode))
        return _EINVAL;

    ssize_t err = realfs_readlink(mount, path, buf, bufsize);
    if (err == _EINVAL) {
        // broken symlinks can't be included in an iOS app or else Xcode craps out
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            return err_map(errno);
        int err = read(fd, buf, bufsize);
        if (err < 0)
            return err_map(errno);
        close(fd);
        return err;
    }
    return err;
}

const struct fs_ops fakefs = {
    .open = fakefs_open,
    .unlink = realfs_unlink,
    .stat = fakefs_stat,
    .access = realfs_access,
    .readlink = fakefs_readlink,
    .fstat = fakefs_fstat,
    .statfs = realfs_statfs,
    .flock = realfs_flock,
};
