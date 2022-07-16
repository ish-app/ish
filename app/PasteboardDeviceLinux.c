//
//  PasteboardDeviceLinux.c
//  iSH+Linux
//
//  Created by Theodore Dubois on 2/19/22.
//

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/fcntl.h>
#include "LinuxInterop.h"

#define INITIAL_BUFFER_CAP 1024
// 8MB: https://stackoverflow.com/a/3523175
#define MAXIMAL_BUFFER_CAP 8*1024*1024

struct pasteboard_file {
    char *buffer;
    size_t cap;
    size_t len;
    long generation;
};

static int realloc_buffer_to_fit(struct file *file, size_t fit_len) {
    struct pasteboard_file *pb = file->private_data;
    // (Re)allocate buffer if there's not enough space to fit fit_len
    if (fit_len <= pb->cap)
        return 0;
    if (fit_len > MAXIMAL_BUFFER_CAP)
        return -ENOSPC;

    size_t size = pb->cap * 2;
    if (size == 0)
        size = INITIAL_BUFFER_CAP;
    while (size < fit_len) size *= 2;

    void *new_buf = krealloc(pb->buffer, size, GFP_KERNEL);
    if (new_buf == NULL)
        return -ENOMEM;

    pb->buffer = new_buf;
    pb->cap = size;
    return 0;
}

static int read_pasteboard_to_buffer(struct file *file) {
    struct pasteboard_file *pb = file->private_data;
    nsobj_t data = UIPasteboard_get();
    int err = realloc_buffer_to_fit(file, NSData_length(data));
    if (err < 0) {
        objc_put(data);
        return err;
    }
    pb->len = NSData_length(data);
    memcpy(pb->buffer, NSData_bytes(data), pb->len);
    objc_put(data);
    return 0;
}

static int pasteboard_open(struct inode *ino, struct file *file) {
    struct pasteboard_file *pb = kzalloc(sizeof(struct pasteboard_file), GFP_KERNEL);
    int err = -ENOMEM;
    if (pb == NULL)
        goto fail;
    file->private_data = pb;

    if (!(file->f_flags & O_TRUNC)) {
        err = read_pasteboard_to_buffer(file);
        if (err < 0)
            goto fail_free_pb;
    }

    return 0;

fail_free_pb:
    if (pb->buffer != NULL)
        kfree(pb->buffer);
    kfree(pb);
fail:
    file->private_data = NULL;
    return err;
}

static loff_t pasteboard_llseek(struct file *file, loff_t off, int whence) {
    struct pasteboard_file *pb = file->private_data;
    return generic_file_llseek_size(file, off, whence, MAXIMAL_BUFFER_CAP, pb->len);
}

static ssize_t pasteboard_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    struct pasteboard_file *pb = file->private_data;
    return simple_read_from_buffer(buf, count, ppos, pb->buffer, pb->len);
}

static ssize_t pasteboard_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    struct pasteboard_file *pb = file->private_data;
    if (file->f_flags & O_APPEND)
        *ppos = pb->len;
    loff_t new_len = *ppos + count;
    int err = realloc_buffer_to_fit(file, new_len);
    if (err < 0)
        return err;
    ssize_t result = simple_write_to_buffer(pb->buffer, pb->cap, ppos, buf, count);
    if (result < 0)
        return result;
    pb->len = new_len;
    return result;
}

static int pasteboard_fsync(struct file *file, loff_t start, loff_t end, int datasync) {
    struct pasteboard_file *pb = file->private_data;
    UIPasteboard_set(pb->buffer, pb->len);
    return 0;
}

static int pasteboard_release(struct inode *inode, struct file *file) {
    struct pasteboard_file *pb = file->private_data;
    pasteboard_fsync(file, 0, 0, 0);
    if (pb->buffer != NULL)
        kfree(pb->buffer);
    kfree(pb);
    return 0;
}

static struct file_operations pasteboard_fops = {
    .owner = THIS_MODULE,
    .open = pasteboard_open,
    .read = pasteboard_read,
    .write = pasteboard_write,
    .llseek = pasteboard_llseek,
    .fsync = pasteboard_fsync,
    .release = pasteboard_release,
};

static struct miscdevice pasteboard_dev = {
    .name = "clipboard",
    .minor = MISC_DYNAMIC_MINOR,
    .fops = &pasteboard_fops,
};

int __init pasteboard_init(void) {
    return misc_register(&pasteboard_dev);
}

device_initcall(pasteboard_init);
