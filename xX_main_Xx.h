#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "kernel/init.h"
#include "kernel/fs.h"
#include "fs/devices.h"
#ifdef __APPLE__
#include <sys/resource.h>
#define IOPOL_TYPE_VFS_HFS_CASE_SENSITIVITY 1
#define IOPOL_VFS_HFS_CASE_SENSITIVITY_FORCE_CASE_SENSITIVE 1
#endif

static void exit_handler(struct task *task, int code) {
    if (task->parent != NULL)
        return;
    if (code & 0xff)
        raise(code & 0xff);
    else
        exit(code >> 8);
}

// this function parses command line arguments and initializes global
// data structures. thanks programming discussions discord server for the name.
// https://discord.gg/9zT7NHP
static inline int xX_main_Xx(int argc, char *const argv[], const char *envp) {
#ifdef __APPLE__
    // Enable case-sensitive filesystem mode on macOS, if possible.
    // In order for this to succeed, either we need to be running as root, or
    // be given the com.apple.private.iopol.case_sensitivity entitlement. The
    // second option isn't possible so you'll need to give iSH the setuid root
    // bit. In that case it's important to drop root permissions ASAP.
    // https://worthdoingbadly.com/casesensitive-iossim/
    int iopol_err = setiopolicy_np(IOPOL_TYPE_VFS_HFS_CASE_SENSITIVITY,
            IOPOL_SCOPE_PROCESS,
            IOPOL_VFS_HFS_CASE_SENSITIVITY_FORCE_CASE_SENSITIVE);
    if (iopol_err != 0 && errno != EPERM)
        perror("could not enable case sensitivity");
    setgid(getgid());
    setuid(getuid());
#endif

    // parse cli options
    int opt;
    const char *root = "";
    bool has_root = false;
    const struct fs_ops *fs = &realfs;
    const char *console = "/dev/tty1";
    while ((opt = getopt(argc, argv, "+r:f:c:")) != -1) {
        switch (opt) {
            case 'r':
            case 'f':
                root = optarg;
                has_root = true;
                if (opt == 'f')
                    fs = &fakefs;
                break;
            case 'c':
                console = optarg;
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

    become_first_process();
    current->thread = pthread_self();
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
    tty_drivers[TTY_CONSOLE_MAJOR] = &real_tty_driver;
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        err = create_stdio(console);
        if (err < 0)
            return err;
    } else {
        err = create_piped_stdio();
        if (err < 0)
            return err;
    }
    exit_hook = exit_handler;
    return 0;
}
