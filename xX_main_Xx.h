#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "sys/calls.h"

static void mount_root(const char *source) {
    mounts = malloc(sizeof(struct mount));
    mounts->point = "";
    mounts->source = strdup(source);
    mounts->fs = &realfs;
    mounts->next = NULL;
}

// this function parses command line arguments and initializes global
// data structures. thanks programming discussions discord server for the name.
// https://discord.gg/9zT7NHP
static inline int xX_main_Xx(int argc, char *const argv[]) {
    // parse cli options
    int opt;
    const char *root = "";
    while ((opt = getopt(argc, argv, "+r:")) != -1) {
        switch (opt) {
            case 'r':
                root = optarg;
                break;
        }
    }

    char root_realpath[PATH_MAX + 1] = "";
    if (*root != '\0' && realpath(root, root_realpath) == NULL) {
        perror(root); exit(1);
    }
    mount_root(root_realpath);

    // make a process
    current = process_create();
    mem_init(&curmem);
    current->ppid = 1;
    current->uid = current->gid = 0;
    current->root = "";
    current->pwd = getcwd(NULL, 0);

    // I can't wait for when the init system works and I don't need to do this
    current->files[0] = malloc(sizeof(struct fd));
    current->files[0]->ops = &realfs_fdops;
    current->files[0]->real_fd = 0;
    current->files[0]->refcnt = 1;
    current->files[1] = malloc(sizeof(struct fd));
    current->files[1]->ops = &realfs_fdops;
    current->files[1]->real_fd = 1;
    current->files[1]->refcnt = 1;
    current->files[2] = malloc(sizeof(struct fd));
    current->files[2]->ops = &realfs_fdops;
    current->files[2]->real_fd = 2;
    current->files[2]->refcnt = 1;

    // go.
    char *envp[] = {NULL};
    int err = sys_execve(argv[optind], argv + optind, envp);
    if (err < 0)
        return -err;
    return 0;
}
