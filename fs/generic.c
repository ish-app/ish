#include <stdlib.h>
#include <string.h>

#include "sys/fs.h"
#include "sys/process.h"

struct mount *find_mount(char *pathname) {
    struct mount *mount;
    for (mount = mounts; mount != NULL; mount = mount->next) {
        if (strncmp(pathname, mount->mount_point, strlen(mount->mount_point)) == 0) {
            break;
        }
    }
    assert(mount != NULL); // this would mean there's no root FS mounted
    return mount;
}

char *path_in_mount(char *path, struct mount *mount) {
    return path + strlen(mount->mount_point);
}

int generic_open(const char *pathname, struct fd *fd, int flags, int mode) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount(path);
    return mount->fs->open(path_in_mount(path, mount), fd, flags, mode);
}

// TODO I bet this can be shorter
int generic_access(const char *pathname, int mode) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount(path);
    return mount->fs->access(path_in_mount(path, mount), mode);
}

// TODO I bet this can be shorter
int generic_unlink(const char *pathname) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount(path);
    return mount->fs->unlink(path_in_mount(path, mount));
}

ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount(path);
    return mount->fs->readlink(path_in_mount(path, mount), buf, bufsize);
}

void mount_root() {
    mounts = malloc(sizeof(struct mount));
    mounts->mount_point = strdup("");
    mounts->fs = &realfs;
    mounts->next = NULL;
}
