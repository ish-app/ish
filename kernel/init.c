#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include "kernel/init.h"
#include "kernel/calls.h"
#include "fs/tty.h"

int mount_root(const char *source) {
    char source_realpath[MAX_PATH + 1];
    if (realpath(source, source_realpath) == NULL)
        return err_map(errno);
    mounts = malloc(sizeof(struct mount));
    mounts->point = "";
    mounts->source = strdup(source_realpath);
    mounts->fs = &realfs;
    mounts->next = NULL;
    return 0;
}

static void nop_handler() {}

// this function parses command line arguments and initializes global
// data structures. thanks programming discussions discord server for the name.
// https://discord.gg/9zT7NHP
int create_init_process(const char *program, char *const argv[], char *const envp[]) {
    signal(SIGUSR1, nop_handler);

    current = process_create();
    current->cpu.mem = mem_new();
    current->ppid = 1;
    current->uid = current->gid = 0;
    current->root = generic_open("/", O_RDONLY_, 0);
    current->pwd = generic_dup(current->root);
    current->umask = 0022;
    current->thread = pthread_self();
    sys_setsid();

    return sys_execve(program, argv, envp);
}

int create_stdio(struct tty_driver driver) {
    // I can't wait for when init and udev works and I don't need to do this
    tty_drivers[TTY_VIRTUAL] = driver;

    // FIXME use generic_open (or something) to avoid this mess
    struct fd *fd = adhoc_fd_create();
    fd->stat->rdev = dev_make(4, 0);
    fd->stat->mode = S_IFCHR | S_IRUSR;
    int err = dev_open(4, 0, DEV_CHAR, fd);
    if (err < 0)
        return err;
    current->files[0] = fd;
    current->files[1] = generic_dup(fd);
    current->files[2] = generic_dup(fd);
    return 0;
}

