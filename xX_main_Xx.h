#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "kernel/init.h"
#include "kernel/fs.h"

static void exit_handler(int code) {
    if (code & 0xff)
        raise(code & 0xff);
    else
        exit(code >> 8);
}

// this function parses command line arguments and initializes global
// data structures. thanks programming discussions discord server for the name.
// https://discord.gg/9zT7NHP
static inline int xX_main_Xx(int argc, char *const argv[], const char *envp) {
    // parse cli options
    int opt;
    const char *root = "";
    bool has_root = false;
    const struct fs_ops *fs = &realfs;
    while ((opt = getopt(argc, argv, "+r:f:")) != -1) {
        switch (opt) {
            case 'r':
            case 'f':
                root = optarg;
                has_root = true;
                if (opt == 'f')
                    fs = &fakefs;
                break;
        }
    }

    char root_realpath[MAX_PATH + 1] = "/";
    if (has_root && realpath(root, root_realpath) == NULL) {
        perror(root);
        exit(1);
    }
    if (fs == &fakefs)
        strcat(root_realpath, "/data");
    int err = mount_root(fs, root_realpath);
    if (err < 0)
        return err;

    create_first_process();
    if (!has_root) {
        char cwd[MAX_PATH + 1];
        getcwd(cwd, sizeof(cwd));
        struct fd *pwd = generic_open(cwd, O_RDONLY_, 0);
        fs_chdir(current->fs, pwd);
    }

    char argv_copy[4096];
    int i = optind;
    size_t p = 0;
    while (i < argc) {
        strcpy(&argv_copy[p], argv[i]);
        p += strlen(argv[i]) + 1;
        i++;
    }
    argv_copy[p] = '\0';
    err = sys_execve(argv[optind], argv_copy, envp == NULL ? "\0" : envp);
    if (err < 0)
        return err;
    err = create_stdio(&real_tty_driver);
    if (err < 0)
        return err;
    exit_hook = exit_handler;
    return 0;
}
