#ifndef FS_FIX_PATH_H
#define FS_FIX_PATH_H

static inline const char *fix_path(const char *path) {
    if (path[0] == '\0')
        return ".";
    if (path[0] == '/')
        path++;
    return path;
}

#endif
