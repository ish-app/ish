#include "kernel/errno.h"
#include "fs/null.h"

static int null_open(int major, int minor, int type, struct fd *fd) {
    return 0;
}   

static ssize_t null_read(struct fd *fd, void *buf, size_t bufsize) {
    return 0;
}   

static ssize_t null_write(struct fd *fd, const void *buf, size_t bufsize) {
    return bufsize;
}   

struct dev_ops null_dev = {
    .open = null_open,
    .fd.read = null_read,
    .fd.write = null_write,
};  
