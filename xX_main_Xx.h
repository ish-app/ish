#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "sys/calls.h"
#include "fs/tty.h"

static void mount_root(const char *source) {
    mounts = malloc(sizeof(struct mount));
    mounts->point = "";
    mounts->source = strdup(source);
    mounts->fs = &realfs;
    mounts->next = NULL;
}

static void nop_handler() {}

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

    char root_realpath[PATH_MAX + 1] = "/";
    if (has_root && realpath(root, root_realpath) == NULL) {
        perror(root); exit(1);
    }
    mount_root(root_realpath);

    signal(SIGUSR1, nop_handler);

    // make a process
    current = process_create();
    mem_init(&curmem);
    current->ppid = 1;
    current->uid = current->gid = 0;
    current->root = strdup("");
    if (has_root)
        current->pwd = strdup("");
    else
        current->pwd = getcwd(NULL, 0);
    current->thread = pthread_self();
    sys_setsid();

    // I can't wait for when init and udev works and I don't need to do this
    tty_drivers[TTY_VIRTUAL] = real_tty_driver;
    fd_t stdin_fd = create_fd();
    assert(stdin_fd == 0);

    // FIXME use generic_open (or something) to avoid this mess
    current->files[stdin_fd]->mount = mounts;
    current->files[stdin_fd]->real_fd = STDIN_FILENO;

    int err = dev_open(4, 0, DEV_CHAR, current->files[stdin_fd]);
    if (err < 0)
        return err;
    fd_t stdout_fd = sys_dup(0);
    assert(stdout_fd == 1);
    fd_t stderr_fd = sys_dup(0);
    assert(stderr_fd == 2);

    // go.
    char *envp[] = {NULL};
    err = sys_execve(argv[optind], argv + optind, envp);
    if (err < 0)
        return err;
    return 0;
}
