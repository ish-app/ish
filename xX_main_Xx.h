#include <unistd.h>
#include "kernel/init.h"
#include "kernel/fs.h"

static void exit_handler(int code) {
    exit(code >> 8);
}

// this function parses command line arguments and initializes global
// data structures. thanks programming discussions discord server for the name.
// https://discord.gg/9zT7NHP
static inline int xX_main_Xx(int argc, char *const argv[]) {
    // parse cli options
    int opt;
    const char *root = "";
    bool has_root = false;
    while ((opt = getopt(argc, argv, "+r:")) != -1) {
        switch (opt) {
            case 'r':
                root = optarg;
                has_root = true;
                break;
        }
    }

    char root_realpath[MAX_PATH + 1] = "/";
    if (has_root && realpath(root, root_realpath) == NULL) {
        perror(root);
        exit(1);
    }
    mount_root(&fakefs, root_realpath);

    char *envp[] = {NULL};
    int err = create_init_process(argv[optind], argv + optind, envp);
    if (err < 0)
        return err;
    err = create_stdio(real_tty_driver);
    if (err < 0)
        return err;
    exit_hook = exit_handler;
    return 0;
}
