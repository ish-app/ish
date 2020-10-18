#include <stdio.h>
#include <string.h>
#include <libgen.h>

#define ISH_INTERNAL
#include "fs/fake.h"
#include "tools/fakefs.h"

enum cmd {
    cmd_import,
    cmd_export,
};

int main(int argc, const char *argv[]) {
    enum cmd cmd = cmd_import;
    if (strcmp(basename((char *) argv[0]), "unfakefsify") == 0) {
        cmd = cmd_export;
    }

    if (argc != 3) {
        fprintf(stderr, "wrong number of arguments\n");
        switch (cmd) {
            case cmd_import:
                fprintf(stderr, "usage: %s <rootfs.tar.gz> <fakefs>\n", argv[0]);
                break;
            case cmd_export:
                fprintf(stderr, "usage: %s <fakefs> <rootfs.tar.gz>\n", argv[0]);
                break;
        }
        return 1;
    }

    struct fakefsify_error err;
    bool (*func)(const char *, const char *, struct fakefsify_error *, struct progress) = NULL;
    if (cmd == cmd_import)
        func = fakefs_import;
    else if (cmd == cmd_export)
        func = fakefs_export;
    if (!(*func)(argv[1], argv[2], &err, (struct progress) {})) {
        fprintf(stderr, "error!!1! %d %d %s\n", err.line, err.type, err.message);
        return 1;
    }
}
