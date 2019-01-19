#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include "kernel/init.h"
#include "kernel/calls.h"
#include "fs/fd.h"
#include "fs/tty.h"

int mount_root(const struct fs_ops *fs, const char *source) {
    char source_realpath[MAX_PATH + 1];
    if (realpath(source, source_realpath) == NULL)
        return errno_map();
    int err = do_mount(fs, source_realpath, "");
    if (err < 0)
        return err;
    return 0;
}

static struct tgroup *init_tgroup() {
    struct tgroup *group = malloc(sizeof(struct tgroup));
    if (group == NULL)
        return NULL;
    *group = (struct tgroup) {};
    list_init(&group->threads);
    lock_init(&group->lock);
    cond_init(&group->child_exit);
    cond_init(&group->stopped_cond);
    return group;
}

void create_first_process() {
    extern void sigusr1_handler(int sig);
    struct sigaction sigact;
    sigact.sa_handler = sigusr1_handler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGUSR1);
    sigaction(SIGUSR1, &sigact, NULL);
    signal(SIGPIPE, SIG_IGN);

    current = task_create_(NULL);
    current->mm = mm_new();
    current->mem = current->cpu.mem = &current->mm->mem;
    struct tgroup *group = init_tgroup();
    list_add(&group->threads, &current->group_links);
    group->leader = current;
    current->group = group;
    current->tgid = current->pid;

    struct fs_info *fs = fs_info_new();
    current->fs = fs;
    fs->pwd = fs->root = generic_open("/", O_RDONLY_, 0);
    fs->pwd->refcount = 2;
    fs->umask = 0022;
    current->files = fdtable_new(3);
    current->sighand = sighand_new();
    for (int i = 0; i < RLIMIT_NLIMITS_; i++)
        group->limits[i].cur = group->limits[i].max = RLIM_INFINITY_;
    // python subprocess uses this limit as a way to close every open file
    group->limits[RLIMIT_NOFILE_].cur = 1024;
    current->thread = pthread_self();
    sys_setsid();
}

int create_stdio(struct tty_driver *driver) {
    // I can't wait for when init and udev works and I don't need to do this
    tty_drivers[TTY_CONSOLE_MAJOR] = driver;

    // FIXME use generic_open (or something) to avoid this mess
    struct fd *fd = adhoc_fd_create(NULL);
    fd->stat.rdev = dev_make(4, 0);
    fd->stat.mode = S_IFCHR | S_IRUSR;
    fd->flags = O_RDWR_;
    int err = dev_open(4, 0, DEV_CHAR, fd);
    if (err < 0)
        return err;

    fd->refcount = 3;
    current->files->files[0] = fd;
    current->files->files[1] = fd;
    current->files->files[2] = fd;
    return 0;
}

