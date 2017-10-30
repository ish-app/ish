#include <limits.h>
#ifndef GDBM_NDBM
#include <ndbm.h>
#else
#include <gdbm-ndbm.h>
#endif
#include <string.h>
#include <fcntl.h>
#include "kernel/fs.h"

struct ish_stat {
    dword_t mode;
    dword_t uid;
    dword_t gid;
    dword_t rdev;
};

static datum read_meta(struct mount *mount, const char *path, const char *type) {
    DBM *db = mount->data;
    if (db == NULL) {
        char db_path[PATH_MAX];
        strcpy(db_path, mount->source);
        char *basename = strrchr(db_path, '/') + 1;
        assert(strcmp(basename, "data") == 0);
        strncpy(basename, "meta", 4);
        db = dbm_open(db_path, O_RDWR, 0666);
        assert(db != NULL);
    }
    char keydata[MAX_PATH+strlen(type)+1];
    strcpy(keydata, type);
    strcat(keydata, ":");
    strcat(keydata, path);
    datum key = {.dptr = keydata, .dsize = strlen(keydata)};
    return dbm_fetch(db, key);
}

static int fakefs_stat(struct mount *mount, char *path, struct statbuf *fake_stat, bool follow_links) {
    int err = realfs_stat(mount, path, fake_stat, follow_links);
    if (err < 0)
        return err;
    datum d = read_meta(mount, path, "meta");
    if (d.dptr != NULL) {
        assert(d.dsize == sizeof(struct ish_stat));
        struct ish_stat *ishstat = (void *) d.dptr;
        fake_stat->mode = ishstat->mode;
        fake_stat->uid = ishstat->uid;
        fake_stat->gid = ishstat->gid;
        fake_stat->rdev = ishstat->rdev;
    }
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

const struct fs_ops fakefs = {
    .open = realfs_open,
    .unlink = realfs_unlink,
    .stat = fakefs_stat,
    .access = realfs_access,
    .readlink = realfs_readlink,
    .fstat = fakefs_fstat,
    .statfs = realfs_statfs,
    .flock = realfs_flock,
};
