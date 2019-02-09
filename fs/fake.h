#ifndef ISH_INTERNAL
#error "for internal use only"
#endif

#ifndef FS_FAKE_H
#define FS_FAKE_H

#include "kernel/fs.h"
#include "misc.h"

struct ish_stat {
    dword_t mode;
    dword_t uid;
    dword_t gid;
    dword_t rdev;
};

void db_begin(struct mount *mount);
void db_commit(struct mount *mount);
void db_rollback(struct mount *mount);

ino_t path_get_inode(struct mount *mount, const char *path);
bool path_read_stat(struct mount *mount, const char *path, struct ish_stat *stat, ino_t *inode);
void path_create(struct mount *mount, const char *path, struct ish_stat *stat);

struct fd *fakefs_open_inode(struct mount *mount, ino_t inode);

#endif
