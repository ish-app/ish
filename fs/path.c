#include <string.h>
#include "kernel/calls.h"
#include "fs/path.h"

#define __NO_AT (struct fd *) 1

int path_normalize(struct fd *at, const char *path, char *out, bool follow_links) {
    assert(at != NULL);
    const char *p = path;
    char *o = out;
    *o = '\0';
    int n = MAX_PATH - 1;

    if (strcmp(path, "") == 0)
        return _ENOENT;

    if (at != __NO_AT) {
        // start with root or cwd, depending on whether it starts with a slash
        lock(&current->fs->lock);
        if (*p == '/')
            at = current->fs->root;
        else if (at == AT_PWD)
            at = current->fs->pwd;
        unlock(&current->fs->lock);
        if (at != NULL) {
            char at_path[MAX_PATH];
            int err = generic_getpath(at, at_path);
            if (err < 0)
                return err;
            assert(path_is_normalized(at_path));
            if (strcmp(at_path, "/") != 0) {
                strcpy(o, at_path);
                n -= strlen(at_path);
                o += strlen(at_path);
            }
        }
    }

    while (*p == '/')
        p++;

    while (*p != '\0') {
        if (p[0] == '.') {
            if (p[1] == '\0' || p[1] == '/') {
                // single dot path component, ignore
                p++;
                while (*p == '/')
                    p++;
                continue;
            } else if (p[1] == '.' && (p[2] == '\0' || p[2] == '/')) {
                // double dot path component, delete the last component
                if (o != out) {
                    do {
                        o--;
                        n++;
                    } while (*o != '/');
                }
                p += 2;
                while (*p == '/')
                    p++;
                continue;
            }
        }

        // output a slash
        *o++ = '/'; n--;
        char *c = o;
        // copy up to a slash or null
        while (*p != '/' && *p != '\0' && --n > 0)
            *o++ = *p++;
        // eat any slashes
        while (*p == '/')
            p++;

        if (n == 0)
            return _ENAMETOOLONG;

        if (follow_links || *p != '\0') {
            // this buffer is used to store the path that we're readlinking, then
            // if it turns out to point to a symlink it's reused as the buffer
            // passed to the next path_normalize call
            char possible_symlink[MAX_PATH];
            *o = '\0';
            strcpy(possible_symlink, out);
            struct mount *mount = find_mount_and_trim_path(possible_symlink);
            assert(path_is_normalized(possible_symlink));
            int res = _EINVAL;
            if (mount->fs->readlink)
                res = mount->fs->readlink(mount, possible_symlink, c, MAX_PATH - (c - out));
            mount_release(mount);
            if (res >= 0) {
                // readlink does not null terminate
                c[res] = '\0';
                // if we should restart from the root, copy down
                if (*c == '/')
                    memmove(out, c, strlen(c) + 1);
                char *expanded_path = possible_symlink;
                strcpy(expanded_path, out);
                // if (*p) {
                    strcat(expanded_path, "/");
                    strcat(expanded_path, p);
                // }
                return path_normalize(__NO_AT, expanded_path, out, follow_links);
            }
        }
    }

    *o = '\0';
    assert(path_is_normalized(out));
    return 0;
}

bool path_is_normalized(const char *path) {
    while (*path != '\0') {
        if (*path != '/')
            return false;
        path++;
        if (*path == '/')
            return false;
        while (*path != '/' && *path != '\0')
            path++;
    }
    return true;
}
