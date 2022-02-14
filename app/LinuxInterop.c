//
//  LinuxInterop.c
//  iSH
//
//  Created by Theodore Dubois on 7/3/21.
//

#include "LinuxInterop.h"
#include <Block.h>
#include <linux/start_kernel.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/termios.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/file.h>
#include <linux/umh.h>
#include <linux/syscalls.h>
#include <linux/utsname.h>
#include <linux/panic_notifier.h>
#include <linux/init_syscalls.h>
#include <asm/irq.h>
#include <user/fs.h>
#include <user/irq.h>

extern void run_kernel(void);

void actuate_kernel(const char *cmdline) {
    strcpy(boot_command_line, cmdline);
    run_kernel();
}

static int panic_report(struct notifier_block *nb, unsigned long action, void *data) {
    const char *message = data;
    async_do_in_ios(^{
        ReportPanic(message);
    });
    return 0;
}

static struct notifier_block panic_report_block = {
    .notifier_call = panic_report,
    .priority = INT_MAX,
};
static int __init panic_report_init(void) {
    atomic_notifier_chain_register(&panic_notifier_list, &panic_report_block);
    return 0;
}
core_initcall(panic_report_init);

static int block_request_read;
static int block_request_write;
static irqreturn_t call_block_irq(int irq, void *dev) {
    void (^block)(void);
    for (;;) {
        int err = host_read(block_request_read, &block, sizeof(block));
        if (err <= 0)
            break;
        block();
        Block_release(block);
    }
    return IRQ_HANDLED;
}

void async_do_in_irq(void (^block)(void)) {
    block = Block_copy(block);
    int err = host_write(block_request_write, &block, sizeof(block));
    if (err < 0)
        __builtin_trap();
    trigger_irq(CALL_BLOCK_IRQ);
}

struct ios_work {
    void (^block)(void);
    struct work_struct work;
};

static void do_ios_work(struct work_struct *work) {
    struct ios_work *ios_work = container_of(work, struct ios_work, work);
    ios_work->block();
    Block_release(ios_work->block);
    kfree(ios_work);
}

void async_do_in_workqueue(void (^block)(void)) {
    async_do_in_irq(^{
        struct ios_work *work = kzalloc(sizeof(*work), GFP_ATOMIC);
        work->block = Block_copy(block);
        INIT_WORK(&work->work, do_ios_work);
        schedule_work(&work->work);
    });
}

static int __init call_block_init(void) {
    int err = host_pipe(&block_request_read, &block_request_write);
    if (err < 0)
        return err;
    err = fd_set_nonblock(block_request_read);
    if (err < 0)
        return err;
    err = request_irq(CALL_BLOCK_IRQ, call_block_irq, 0, "block", NULL);
    if (err < 0)
        return err;
    return 0;
}
subsys_initcall(call_block_init);

struct ish_session {
    struct file *tty;
    nsobj_t terminal;
    int pid;
    StartSessionDoneBlock callback;
};

static int session_init(struct subprocess_info *info, struct cred *cred) {
    struct ish_session *session = info->data;
    int err = ksys_setsid();
    if (err < 0)
        return err;
    err = vfs_ioctl(session->tty, TIOCSCTTY, 0);
    if (err < 0)
        return err;
    for (int fd = 0; fd <= 2; fd++) {
        int err = replace_fd(fd, session->tty, 0);
        if (err < 0)
            return err;
    }
    session->pid = task_pid_nr(current);
    return 0;
}

static void session_cleanup(struct subprocess_info *info) {
    struct ish_session *session = info->data;
    if (session->pid != 0 || info->retval != 0)
        session->callback(info->retval, session->pid, objc_get(session->terminal));
    else; // otherwise, there was a synchronous failure, returned directly from call_usermodehelper_exec
    if (session->tty != NULL)
        fput(session->tty);
    objc_put(session->terminal);
    kfree(session);
}

void linux_start_session(const char *exe, const char *const *argv, const char *const *envp, StartSessionDoneBlock done) {
    struct ish_session *session = kzalloc(sizeof(*session), GFP_KERNEL);
    session->tty = ios_pty_open(&session->terminal);
    session->callback = done;
    struct subprocess_info *proc = call_usermodehelper_setup(exe, (char **) argv, (char **) envp, GFP_KERNEL, session_init, session_cleanup, session);
    int err = call_usermodehelper_exec(proc, UMH_WAIT_EXEC);
    if (err < 0)
        done(err, 0, NULL);
}

void linux_sethostname(const char *hostname) {
    int len = strlen(hostname);
    if (len > __NEW_UTS_LEN)
        len = __NEW_UTS_LEN;
    down_write(&uts_sem);
    struct new_utsname *u = utsname();
    if (strncmp(u->nodename, hostname, len) != 0) {
        memcpy(u->nodename, hostname, len);
        memset(u->nodename + len, 0, sizeof(u->nodename) - len);
        uts_proc_notify(UTS_PROC_HOSTNAME);
    }
    up_write(&uts_sem);
}

ssize_t linux_read_file(const char *path, char *buf, size_t size) {
    struct file *filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp))
        return PTR_ERR(filp);
    ssize_t res = vfs_read(filp, buf, size, NULL);
    filp_close(filp, NULL);
    if (res >= size)
        return -ENAMETOOLONG;
    return res;
}
ssize_t linux_write_file(const char *path, const char *buf, size_t size) {
    struct file *filp = filp_open(path, O_WRONLY, 0);
    ssize_t res = vfs_write(filp, buf, size, NULL);
    filp_close(filp, NULL);
    return res;
}
int linux_remove_directory(const char *path) {
    return init_rmdir(path);
}
