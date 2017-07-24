#include <stdlib.h>
#include <string.h>

#include "sys/fs.h"
#include "sys/process.h"

static struct mount *find_mount(char *pathname) {
    struct mount *mount;
    for (mount = mounts; mount != NULL; mount = mount->next) {
        if (strncmp(pathname, mount->point, strlen(mount->point)) == 0) {
            break;
        }
    }
    assert(mount != NULL); // this would mean there's no root FS mounted
    return mount;
}

struct mount *find_mount_and_trim_path(char *path) {
    struct mount *mount = find_mount(path);
    char *dst = path;
    const char *src = path + strlen(mount->point);
    while (*src != '\0')
        *dst++ = *src++;
    *dst = '\0';
    return mount;
}

int generic_open(const char *pathname, struct fd *fd, int flags, int mode) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->open(mount, path, fd, flags, mode);
}

// TODO I bet this can be shorter
int generic_access(const char *pathname, int mode) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->access(mount, path, mode);
}

// TODO I bet this can be shorter
int generic_unlink(const char *pathname) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->unlink(mount, path);
}

ssize_t generic_readlink(const char *pathname, char *buf, size_t bufsize) {
    char path[MAX_PATH];
    int err = path_normalize(pathname, path);
    if (err < 0)
        return err;
    struct mount *mount = find_mount_and_trim_path(path);
    return mount->fs->readlink(mount, path, buf, bufsize);
}
