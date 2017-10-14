#ifndef FS_ISHSTAT_H
#define FS_ISHSTAT_H

#include "misc.h"

// The values in this structure are stored in an extended attribute on a file,
// because on iOS I can't change the uid or gid of a file.
// TODO the xattr api is a little different on darwin
struct xattr_stat {
    dword_t mode;
    dword_t uid;
    dword_t gid;
    dword_t dev;
    dword_t rdev;
};
#if defined(__linux__)
#define STAT_XATTR "user.ish.stat"
#elif defined(__APPLE__)
#define STAT_XATTR "com.tbodt.ish.stat"
#endif

#if __linux__
static inline int set_ishstat(const char *path, struct xattr_stat *stat) {
    return setxattr(path, "user.ish.stat", stat, sizeof(*stat), 0);
}
static inline int fget_ishstat(int fd, struct xattr_stat *stat) {
    return fgetxattr(fd, "user.ish.stat", stat, sizeof(*stat));
}
static inline int lget_ishstat(const char *path, struct xattr_stat *stat) {
    return lgetxattr(path, "user.ish.stat", stat, sizeof(*stat));
}
#elif __APPLE__
static inline int set_ishstat(const char *path, struct xattr_stat *stat) {
    return setxattr(path, "com.tbodt.ish.stat", stat, sizeof(*stat), 0, 0);
}
static inline int fget_ishstat(int fd, struct xattr_stat *stat) {
    return fgetxattr(fd, "com.tbodt.ish.stat", stat, sizeof(*stat), 0, 0);
}
static inline int lget_ishstat(const char *path, struct xattr_stat *stat) {
    return getxattr(path, "com.tbodt.ish.stat", stat, sizeof(*stat), 0, XATTR_NOFOLLOW);
}
#endif

#endif
