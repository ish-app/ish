#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include "kernel/init.h"
#include "kernel/calls.h"
#include "fs/fd.h"
#include "fs/tty.h"
#include "fs/devices.h"

int mount_root(const struct fs_ops *fs, const char *source) {
    char source_realpath[MAX_PATH + 1];
    if (realpath(source, source_realpath) == NULL)
        return errno_map();
    int err = do_mount(fs, source_realpath, "", 0);
    if (err < 0)
        return err;
    return 0;
}

static void establish_signal_handlers() {
    extern void sigusr1_handler(int sig);
    struct sigaction sigact;
    sigact.sa_handler = sigusr1_handler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGUSR1);
    sigaction(SIGUSR1, &sigact, NULL);
    signal(SIGPIPE, SIG_IGN);
}

// copied from include/asm-generic/resource.h in the kernel
static struct rlimit_ init_rlimits[16] = {
    [RLIMIT_CPU_]        = {RLIM_INFINITY_, RLIM_INFINITY_},
    [RLIMIT_FSIZE_]      = {RLIM_INFINITY_, RLIM_INFINITY_},
    [RLIMIT_DATA_]       = {RLIM_INFINITY_, RLIM_INFINITY_},
    [RLIMIT_STACK_]      = {8*1024*1024, RLIM_INFINITY_},
    [RLIMIT_CORE_]       = {0, RLIM_INFINITY_},
    [RLIMIT_RSS_]        = {RLIM_INFINITY_, RLIM_INFINITY_},
    [RLIMIT_NPROC_]      = {1024, 1024},
    [RLIMIT_NOFILE_]     = {1024, 4096},
    [RLIMIT_MEMLOCK_]    = {64*1024, 64*1024},
    [RLIMIT_AS_]         = {RLIM_INFINITY_, RLIM_INFINITY_},
    [RLIMIT_LOCKS_]      = {RLIM_INFINITY_, RLIM_INFINITY_},
    [RLIMIT_SIGPENDING_] = {1024, 1024},
    [RLIMIT_MSGQUEUE_]   = {819200, 819200},
    [RLIMIT_NICE_]       = {0, 0},
    [RLIMIT_RTPRIO_]     = {0, 0},
    [RLIMIT_RTTIME_]     = {RLIM_INFINITY_, RLIM_INFINITY_},
};

// TODO error propagation
static struct task *construct_task(struct task *parent) {
    struct task *task = task_create_(parent);

    struct tgroup *group = malloc(sizeof(struct tgroup));
    *group = (struct tgroup) {};
    list_init(&group->threads);
    lock_init(&group->lock);
    cond_init(&group->child_exit);
    cond_init(&group->stopped_cond);
    memcpy(group->limits, init_rlimits, sizeof(init_rlimits));
    group->leader = task;
    list_add(&group->threads, &task->group_links);
    task->group = group;
    task->tgid = task->pid;
    task_setsid(task);

    task_set_mm(task, mm_new());
    task->sighand = sighand_new();
    task->files = fdtable_new(3); // why is there a 3 here

    task->fs = fs_info_new();
    task->fs->umask = 0022;
    // we'll need to have current set to do the open call
    struct task *old_current = current;
    current = task;
    task->fs->root = generic_open("/", O_RDONLY_, 0);
    task->fs->pwd = fd_retain(task->fs->root);
    current = old_current;

    return task;
}

int become_first_process() {
    // now seems like a nice time
    establish_signal_handlers();

    struct task *task = construct_task(NULL);
    if (IS_ERR(task))
        return PTR_ERR(task);

    current = task;
    return 0;
}

int become_new_init_child() {
    // locking? who needs locking?!
    struct task *init = pid_get_task(1);
    assert(init != NULL);

    struct task *task = construct_task(init);
    if (IS_ERR(task))
        return PTR_ERR(task);

    // these are things we definitely don't want to inherit
    task->clear_tid = 0;
    task->vfork = NULL;
    // TODO: think about whether it would be a good idea to inherit fs_info

    current = task;
    return 0;
}

extern int console_major;
extern int console_minor;
void set_console_device(int major, int minor) {
    console_major = major;
    console_minor = minor;
}

int create_stdio(const char *file) {
    struct fd *fd = generic_open(file, O_RDWR_, 0);
    if (IS_ERR(fd)) {
        // fallback to adhoc files for stdio
        fd = adhoc_fd_create(NULL);
        // /dev/tty1
        fd->stat.rdev = dev_make(TTY_CONSOLE_MAJOR, 1);
        fd->stat.mode = S_IFCHR | S_IRUSR;
        fd->flags = O_RDWR_;
        int err = dev_open(4, 1, DEV_CHAR, fd);
        if (err < 0)
            return err;
    }

    fd->refcount = 0;
    current->files->files[0] = fd_retain(fd);
    current->files->files[1] = fd_retain(fd);
    current->files->files[2] = fd_retain(fd);
    return 0;
}

static struct fd *open_fd_from_actual_fd(int fd_no) {
    struct fd *fd = adhoc_fd_create(&realfs_fdops);
    if (fd == NULL) {
        return NULL;
    }
    fd->real_fd = fd_no;
    fd->dir = NULL;
    return fd;
}

int create_piped_stdio() {
    if (!(current->files->files[0] = open_fd_from_actual_fd(STDIN_FILENO))) {
        return -1;
    }
    if (!(current->files->files[1] = open_fd_from_actual_fd(STDOUT_FILENO))) {
        return -1;
    }
    if (!(current->files->files[2] = open_fd_from_actual_fd(STDERR_FILENO))) {
        return -1;
    }
    return 0;
}
