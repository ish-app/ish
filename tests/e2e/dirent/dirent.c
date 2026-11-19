#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

// Lists the entries of a directory with the d_type from getdents64 (through
// readdir) and from getdents (the type is the last byte of each record).
// With more arguments, only the entries with those names are listed.

static const char *dt(int t) {
    switch (t) {
    case DT_UNKNOWN: return "unknown";
    case DT_FIFO: return "fifo";
    case DT_CHR: return "chr";
    case DT_DIR: return "dir";
    case DT_BLK: return "blk";
    case DT_REG: return "reg";
    case DT_LNK: return "lnk";
    case DT_SOCK: return "sock";
    default: return "other";
    }
}

static int wanted(const char *name, int argc, char **argv) {
    if (argc <= 2)
        return 1;
    for (int i = 2; i < argc; i++)
        if (strcmp(name, argv[i]) == 0)
            return 1;
    return 0;
}

static int compare(const void *a, const void *b) {
    return strcmp(*(char **) a, *(char **) b);
}

int main(int argc, char **argv) {
    char *lines[1024];
    int n = 0;

    DIR *dir = opendir(argv[1]);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (!wanted(e->d_name, argc, argv))
            continue;
        if (asprintf(&lines[n++], "%s getdents64=%s", e->d_name, dt(e->d_type)) < 0)
            return 1;
    }
    closedir(dir);

    int fd = open(argv[1], O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    char buf[4096];
    long len;
    while ((len = syscall(SYS_getdents, fd, buf, sizeof(buf))) > 0) {
        for (long off = 0; off < len;) {
            unsigned short reclen = *(unsigned short *) (buf + off + 8);
            const char *name = buf + off + 10;
            int type = buf[off + reclen - 1];
            if (wanted(name, argc, argv)) {
                if (asprintf(&lines[n++], "%s getdents=%s", name, dt(type)) < 0)
                    return 1;
            }
            off += reclen;
        }
    }
    if (len < 0) {
        perror("getdents");
        return 1;
    }
    close(fd);

    qsort(lines, n, sizeof(lines[0]), compare);
    for (int i = 0; i < n; i++)
        printf("%s\n", lines[i]);
    return 0;
}
