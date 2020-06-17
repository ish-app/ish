#include <stdio.h>

#define ISH_INTERNAL
#include "fs/fake.h"

int main(int argc, const char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "wrong number of arguments\n");
        fprintf(stderr, "usage: %s <rootfs archive> <destination dir>\n", argv[0]);
        return 1;
    }
    const char *archive_path = argv[1];
    const char *fs = argv[2];
    fakefsify(archive_path, fs);
}
