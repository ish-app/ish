#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/xattr.h>
#include <ftw.h>
#include "fs/ishstat.h"

int xattrify_file(const char *file, const struct stat *stat, int type) {
    struct xattr_stat xstat;
    xstat.mode = stat->st_mode;
    xstat.uid = stat->st_uid;
    xstat.gid = stat->st_gid;
    xstat.dev = stat->st_dev;
    xstat.rdev = stat->st_rdev;
    if (set_ishstat(file, &xstat) < 0)
        perror(file);
    return 0;
}

int main(int argc, const char *argv[]) {
    const char *path = ".";
    if (argc >= 2)
        path = argv[1];
    if (ftw(path, xattrify_file, 1000) < 0)
        perror("ftw");
    return 0;
}
