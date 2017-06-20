#include <stdlib.h>
#include <string.h>

#include "sys/fs.h"
#include "emu/process.h"

path_t find_mount(char *pathname, const struct fs_ops **fs) {
    struct mount *mount;
    for (mount = mounts; mount != NULL; mount = mount->next) {
        if (strncmp(pathname, mount->mount_point, strlen(mount->mount_point)) == 0) {
            break;
        }
    }
    assert(mount != NULL); // this would mean there's no root FS mounted

    *fs = mount->fs;
    return pathname + strlen(mount->mount_point);
}

int generic_open(const char *pathname, struct fd *fd, int flags, int mode) {
    char *full_path = pathname_expand(pathname);
    const struct fs_ops *fs;
    path_t path = find_mount(full_path, &fs);
    int err = fs->open(path, fd, flags, mode);
    free(full_path);
    return err;
}

// TODO I bet this can be shorter
int generic_access(const char *pathname, int mode) {
    char *full_path = pathname_expand(pathname);
    const struct fs_ops *fs;
    path_t path = find_mount(full_path, &fs);
    int err = fs->access(path, mode);
    free(full_path);
    return err;
}

// TODO I bet this can be shorter
int generic_unlink(const char *pathname) {
    char *full_path = pathname_expand(pathname);
    const struct fs_ops *fs;
    path_t path = find_mount(full_path, &fs);
    int err = fs->unlink(path);
    free(full_path);
    return err;
}

ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize) {
    char full_path[strlen(pathname) + 2];
    strcpy(full_path, pathname);
    path_parse(full_path);
    const struct fs_ops *fs;
    path_t path = find_mount(full_path, &fs);
    return fs->readlink(path, buf, bufsize);
}

void mount_root() {
    mounts = malloc(sizeof(struct mount));
    mounts->mount_point = strndup("\0", 2);
    mounts->fs = &realfs;
    mounts->next = NULL;
}
