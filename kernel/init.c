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
    mounts = malloc(sizeof(struct mount));
    mounts->point = "";
    mounts->source = strdup(source_realpath);
    mounts->fs = fs;
    mounts->next = NULL;
    mounts->data = NULL;
    if (fs->mount) {
        int err = fs->mount(mounts);
        if (err < 0)
            return err;
    }
    return 0;
}

static void nop_handler() {}

static struct tgroup *init_tgroup() {
    struct tgroup *group = malloc(sizeof(struct tgroup));
    if (group == NULL)
        return NULL;
    *group = (struct tgroup) {};
    list_init(&group->threads);
    lock_init(&group->lock);
    pthread_cond_init(&group->child_exit, NULL);
    return group;
}

void create_first_process() {
    signal(SIGUSR1, nop_handler);
    signal(SIGPIPE, SIG_IGN);

    current = task_create(NULL);
    current->cpu.mem = mem_new();
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
        current->group->limits[i].cur = current->group->limits[i].max = RLIM_INFINITY_;
    current->thread = pthread_self();
    sys_setsid();
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

    fd->refcount = 3;
    current->files->files[0] = fd;
    current->files->files[1] = fd;
    current->files->files[2] = fd;
    return 0;
}

