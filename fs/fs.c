#include <stdlib.h>
#include <string.h>

#include "fs/fs.h"
#include "emu/process.h"

path_t find_mount(const char *pathname, const struct fs_ops **fs) {
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

// for now just collapses slashes, may eventually do something with . and ..
// TODO move to fs/pathname.c or something
void pathname_normalize(char *pathname) {
    char *s = pathname, *d = pathname;
    while (*s != '\0') {
        // copy up to a slash
        while (*s != '/' && *s != '\0')
            *d++ = *s++;
        // collapse slashes
        while (*s == '/')
            s++;
        // but make sure there's no slash at the end
        if (*s != '\0')
            *d++ = '/';
    }
    *d = '\0'; // finally
}

// turns the given path into an absolute path
// result is malloced and needs to be freed
char *pathname_expand(const char *pathname) {
    // this is not the most efficient way to do it, i'm aware, but it works and it's rare
    size_t full_path_len = strlen(pathname);
    if (pathname[0] != '/')
        full_path_len += strlen(current->pwd) + 1; // plus one for slash
    char *full_path = malloc(full_path_len); full_path[0] = '\0';
    if (pathname[0] != '/') {
        strcat(full_path, current->pwd);
        strcat(full_path, "/");
    }
    strcat(full_path, pathname);
    pathname_normalize(full_path);
    return full_path;
}

int generic_open(const char *pathname, struct fd *fd, int flags) {
    char *full_path = pathname_expand(pathname);
    const struct fs_ops *fs;
    path_t path = find_mount(full_path, &fs);
    int err = fs->open(path, fd, flags);
    free(full_path);
    if (err >= 0)
        fd->fs = fs;
    return err;
}

int generic_close(struct fd *fd) {
    return fd->fs->close(fd);
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
