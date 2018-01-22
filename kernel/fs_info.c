#include "kernel/fs.h"
#include "fs/fd.h"

struct fs_info *fs_info_new() {
    struct fs_info *fs = malloc(sizeof(struct fs_info));
    if (fs == NULL)
        return NULL;
    fs->refcount = 1;
    fs->umask = 0;
    fs->pwd = fs->root = NULL;
    lock_init(&fs->lock);
    return fs;
}

struct fs_info *fs_info_copy(struct fs_info *fs) {
    struct fs_info *new_fs = fs_info_new();
    new_fs->umask = fs->umask;
    new_fs->pwd = fd_retain(fs->pwd);
    new_fs->root = fd_retain(fs->root);
    return new_fs;
}

void fs_info_release(struct fs_info *fs) {
    if (--fs->refcount == 0) {
        fd_close(fs->pwd);
        fd_close(fs->root);
        free(fs);
    }
}
