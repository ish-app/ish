#include <stdlib.h>
#include <string.h>

#include "fs/fs.h"

void find_mount(const char *pathname, char *path, const struct fs_ops **fs) {
    strcpy(path, pathname);
    path_parse(path);

    struct mount *mount;
    for (mount = mounts; mount != NULL; mount = mount->next) {
        if (path_has_prefix(path, mount->mount_point)) {
            break;
        }
    }
    assert(mount != NULL); // this would mean there's no root FS mounted

    *fs = mount->fs;
}

int generic_open(const char *pathname, struct fd *fd, int flags) {
    char path[strlen(pathname) + 2];
    const struct fs_ops *fs;
    find_mount(pathname, path, &fs);
    int err = fs->open(path, fd, flags);
    if (err >= 0)
        fd->fs = fs;
    return err;
}

int generic_close(struct fd *fd) {
    return fd->fs->close(fd);
}

ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize) {
    char path[strlen(pathname) + 2];
    const struct fs_ops *fs;
    find_mount(pathname, path, &fs);
    return fs->readlink(path, buf, bufsize);
}

void mount_root() {
    mounts = malloc(sizeof(struct mount));
    mounts->mount_point = strndup("\0", 2);
    mounts->fs = &realfs;
    mounts->next = NULL;
}
