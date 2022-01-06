//
//  LinuxPTY.c
//  libiSHLinux
//
//  Created by Theodore Dubois on 12/30/21.
//

#include <linux/init.h>
#include <linux/namei.h>
#include <linux/errname.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/syscalls.h>
#include <linux/init_task.h>
#include <linux/termios.h>
#include <linux/fcntl.h>
#include <linux/vmalloc.h>
#include <uapi/linux/mount.h>
#include "LinuxInterop.h"

static struct path ptmx_path;

struct ios_pty_wq {
    struct ios_pty *pty;
    struct wait_queue_entry wq;
    struct wait_queue_head *head;
};

struct ios_pty {
    dev_t pts_rdev;
    struct file *ptm;
    nsobj_t terminal;
    struct linux_tty linux_tty;
    // pseudoterminals have multiple wait queues and you need a different wait_queue_entry for each one. fun fact!
    int n_wqs;
    struct ios_pty_wq wqs[4];
    poll_table pt;

    struct work_struct output_work;
    struct work_struct cleanup_work;
};


static void ios_pty_output_work(struct work_struct *output_work) {
    struct ios_pty *pty = container_of(output_work, struct ios_pty, output_work);
    size_t room = Terminal_roomForOutput(pty->terminal);
    if (room == 0) {
        printk(KERN_WARNING "ios: no room for pty output\n");
        return;
    }
    char *buf = kvmalloc(room, GFP_KERNEL);
    ssize_t size;
    for (;;) {
        size = kernel_read(pty->ptm, buf, PAGE_SIZE, NULL);
        if (size < 0) {
            printk(KERN_WARNING "ios: pty read failed: %s\n", errname(size));
            break;
        }
        int sent = Terminal_sendOutput_length(pty->terminal, buf, size);
        if (sent != size) {
            printk(KERN_WARNING "ios: dropped %ld bytes of pty output\n", size - sent);
            break;
        }
    }
    kvfree(buf);
}

static void ios_pty_cleanup_work(struct work_struct *cleanup_work) {
    struct ios_pty *pty = container_of(cleanup_work, struct ios_pty, cleanup_work);
    for (int i = 0; i < pty->n_wqs; i++)
        remove_wait_queue(pty->wqs[i].head, &pty->wqs[i].wq);
    fput(pty->ptm);
    nsobj_t terminal = pty->terminal;
    Terminal_setLinuxTTY(terminal, NULL);
    objc_put(terminal);
    kfree(pty);
}

static void ios_pty_cb_can_output(struct linux_tty *linux_tty) {
    struct ios_pty *pty = container_of(linux_tty, struct ios_pty, linux_tty);
    schedule_work(&pty->output_work);
}

static void ios_pty_cb_send_input(struct linux_tty *linux_tty, const char *data, size_t length) {
    struct ios_pty *pty = container_of(linux_tty, struct ios_pty, linux_tty);
    ssize_t written = kernel_write(pty->ptm, data, length, NULL);
    if (written < 0)
        printk(KERN_WARNING "ios: pty input failed: %s\n", errname(written));
    else if (written != length)
        printk(KERN_WARNING "ios: dropped %ld bytes of pty input\n", length - written);
}

static void ios_pty_cb_hangup(struct linux_tty *linux_tty) {
    
}

static struct linux_tty_callbacks ios_pty_callbacks = {
    .can_output = ios_pty_cb_can_output,
    .send_input = ios_pty_cb_send_input,
    .hangup = ios_pty_cb_hangup,
};

static int ptm_callback(struct wait_queue_entry *wq_entry, unsigned mode, int flags, void *key) {
    struct ios_pty *pty = container_of(wq_entry, struct ios_pty_wq, wq)->pty;
    __poll_t events = vfs_poll(pty->ptm, NULL);
    if (events & EPOLLHUP)
        schedule_work(&pty->cleanup_work);
    if (events & EPOLLIN)
        schedule_work(&pty->output_work);
    return 0;
}

static void poll_callback(struct file *file, wait_queue_head_t *whead, poll_table *pt) {
    struct ios_pty *pty = container_of(pt, struct ios_pty, pt);
    if (pty->n_wqs >= ARRAY_SIZE(pty->wqs))
        panic("ios pty: too many wait queues!");
    struct ios_pty_wq *pty_wq = &pty->wqs[pty->n_wqs++];
    pty_wq->pty = pty;
    pty_wq->head = whead;
    init_waitqueue_func_entry(&pty_wq->wq, ptm_callback);
    add_wait_queue(whead, &pty_wq->wq);
}

struct file *ios_pty_open(nsobj_t *terminal_out) {
    struct file *ptm_file = dentry_open(&ptmx_path, O_RDWR, current_cred());
    if (IS_ERR(ptm_file))
        return ptm_file;

    int lock_pty = 0;
    vfs_ioctl(ptm_file, TIOCSPTLCK, (unsigned long) &lock_pty);

    // sadly this api can't just return a struct file *
    int fd = vfs_ioctl(ptm_file, TIOCGPTPEER, O_RDWR);
    if (fd < 0) {
        fput(ptm_file);
        return ERR_PTR(fd);
    }
    struct file *pts_file = fget(fd);
    ksys_close(fd);

    struct ios_pty *pty = kzalloc(sizeof(*pty), GFP_KERNEL);
    if (pty == NULL) {
        fput(pts_file);
        fput(ptm_file);
        return ERR_PTR(-ENOMEM);
    }
    pty->ptm = ptm_file;

    INIT_WORK(&pty->output_work, ios_pty_output_work);
    INIT_WORK(&pty->cleanup_work, ios_pty_cleanup_work);

    pty->pts_rdev = pts_file->f_inode->i_rdev;
    pty->terminal = Terminal_terminalWithType_number(MAJOR(pty->pts_rdev), MINOR(pty->pts_rdev));
    pty->linux_tty.ops = &ios_pty_callbacks;
    Terminal_setLinuxTTY(pty->terminal, &pty->linux_tty);
    *terminal_out = pty->terminal;

    init_poll_funcptr(&pty->pt, poll_callback);
    __poll_t revents = vfs_poll(pty->ptm, &pty->pt);
    if (revents)
        ptm_callback(&pty->wqs[pty->n_wqs-1].wq, 0, 0, NULL);
    return pts_file;
}

static __init int ios_pty_init(void) {
    ksys_mkdir("/dev/pts", 0755);
    int err = do_mount("devpts", "/dev/pts", "devpts", MS_SILENT, NULL);
    if (err < 0) {
        panic("ish: failed to mount devpts: %s", errname(err));
    }
    err = kern_path("/dev/pts/ptmx", 0, &ptmx_path);
    if (err < 0) {
        panic("ish: failed to acquire ptmx: %s", errname(err));
    }
    return 0;
}

device_initcall(ios_pty_init);
