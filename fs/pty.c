#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <ctype.h>
#include "kernel/task.h"
#include "kernel/errno.h"
#include "fs/tty.h"
#include "fs/devices.h"

extern struct tty_driver pty_slave;

// the master holds a reference to the slave, so the slave will always be cleaned up second
// when the master cleans up it hangs up the slave, making any operation that references the master unreachable

static void pty_slave_init_inode(struct tty *tty) {
    tty->pty.uid = current->euid;
    // TODO make these mount options
    tty->pty.gid = current->egid;
    tty->pty.perms = 0620;
}

static int pty_master_init(struct tty *tty) {
    tty->termios.iflags = 0;
    tty->termios.oflags = 0;
    tty->termios.lflags = 0;

    struct tty *slave = tty_alloc(&pty_slave, TTY_PSEUDO_SLAVE_MAJOR, tty->num);
    slave->refcount = 1;
    pty_slave.ttys[tty->num] = slave;
    tty->pty.other = slave;
    slave->pty.other = tty;
    slave->pty.locked = true;
    pty_slave_init_inode(slave);
    return 0;
}

static void pty_master_cleanup(struct tty *tty) {
    struct tty *slave = tty->pty.other;
    slave->pty.other = NULL;
    lock(&slave->lock);
    tty_hangup(slave);
    unlock(&slave->lock);
    tty_release(slave);
}

static int pty_slave_open(struct tty *tty) {
    if (tty->pty.other == NULL)
        return _EIO;
    if (tty->pty.locked)
        return _EIO;
    return 0;
}

static int pty_master_ioctl(struct tty *tty, int cmd, void *arg) {
    struct tty *slave = tty->pty.other;
    switch (cmd) {
        case TIOCSPTLCK_:
            slave->pty.locked = !!*(dword_t *) arg;
            break;
        case TIOCGPTN_:
            *(dword_t *) arg = slave->num;
            break;
        case TIOCPKT_:
            tty->pty.packet_mode = !!*(dword_t *) arg;
            break;
        case TIOCGPKT_:
            *(dword_t *) arg = tty->pty.packet_mode;
            break;
        default:
            return _ENOTTY;
    }
    return 0;
}

static int pty_write(struct tty *tty, const void *buf, size_t len, bool blocking) {
    return tty_input(tty->pty.other, buf, len, blocking);
}

static int pty_return_eio(struct tty *UNUSED(tty)) {
    return _EIO;
}

#define MAX_PTYS (1 << 12)

const struct tty_driver_ops pty_master_ops = {
    .init = pty_master_init,
    .open = pty_return_eio,
    .write = pty_write,
    .ioctl = pty_master_ioctl,
    .cleanup = pty_master_cleanup,
};
DEFINE_TTY_DRIVER(pty_master, &pty_master_ops, TTY_PSEUDO_MASTER_MAJOR, MAX_PTYS);

const struct tty_driver_ops pty_slave_ops = {
    .init = pty_return_eio,
    .open = pty_slave_open,
    .write = pty_write,
};
DEFINE_TTY_DRIVER(pty_slave, &pty_slave_ops, TTY_PSEUDO_SLAVE_MAJOR, MAX_PTYS);

static int pty_reserve_next() {
    int pty_num;
    lock(&ttys_lock);
    for (pty_num = 0; pty_num < MAX_PTYS; pty_num++) {
        if (pty_slave.ttys[pty_num] == NULL)
            break;
    }
    pty_slave.ttys[pty_num] = (void *) 1; // anything non-null to reserve it
    unlock(&ttys_lock);
    return pty_num;
}

int ptmx_open(struct fd *fd) {
    int pty_num = pty_reserve_next();
    if (pty_num == MAX_PTYS)
        return _ENOSPC;
    struct tty *master = tty_get(&pty_master, TTY_PSEUDO_MASTER_MAJOR, pty_num);
    if (IS_ERR(master))
        return PTR_ERR(master);
    return tty_open(master, fd);
}

struct tty *pty_open_fake(struct tty_driver *driver) {
    int pty_num = pty_reserve_next();
    if (pty_num == MAX_PTYS)
        return ERR_PTR(_ENOSPC);
    // TODO this is a bit of a hack
    driver->ttys = pty_slave.ttys;
    driver->limit = pty_slave.limit;
    driver->major = TTY_PSEUDO_SLAVE_MAJOR;
    struct tty *tty = tty_get(driver, TTY_PSEUDO_SLAVE_MAJOR, pty_num);
    if (IS_ERR(tty))
        return tty;
    pty_slave_init_inode(tty);
    return tty;
}

static bool isdigits(const char *str) {
    for (int i = 0; str[i] != '\0'; i++)
        if (!isdigit(str[i]))
            return false;
    return true;
}

static const struct fd_ops devpts_fdops;

static bool devpts_pty_exists(int pty_num) {
    if (pty_num < 0 || pty_num > MAX_PTYS)
        return false;
    lock(&ttys_lock);
    bool exists = pty_slave.ttys[pty_num] != NULL;
    unlock(&ttys_lock);
    return exists;
}

// this has a slightly weird error returning convention
// I'm lucky that ENOENT is -2 and not -1
static int devpts_get_pty_num(const char *path) {
    if (strcmp(path, "") == 0)
        return -1; // root
    if (path[0] != '/' || path[1] == '\0' || strchr(path + 1, '/') != NULL)
        return _ENOENT;

    // there's one path component here, which had better be a pty number
    const char *name = path + 1; // skip the initial /
    if (!isdigits(name))
        return _ENOENT;
    // it's not possible to correctly use atoi
    long pty_long = atol(name);
    if (pty_long > INT_MAX)
        return _ENOENT;
    int pty_num = (int) pty_long;
    if (!devpts_pty_exists(pty_num))
        return _ENOENT;
    return pty_num;
}

static struct fd *devpts_open(struct mount *UNUSED(mount), const char *path, int UNUSED(flags), int UNUSED(mode)) {
    int pty_num = devpts_get_pty_num(path);
    if (pty_num == _ENOENT)
        return ERR_PTR(_ENOENT);
    struct fd *fd = fd_create(&devpts_fdops);
    fd->devpts.num = pty_num;
    return fd;
}

static int devpts_getpath(struct fd *fd, char *buf) {
    if (fd->devpts.num == -1)
        strcpy(buf, "");
    else
        sprintf(buf, "/%d", fd->devpts.num);
    return 0;
}

static void devpts_stat_num(int pty_num, struct statbuf *stat) {
    if (pty_num == -1) {
        // root
        stat->mode = S_IFDIR | 0755;
        stat->inode = 1;
    } else {
        lock(&ttys_lock);
        struct tty *tty = pty_slave.ttys[pty_num];
        assert(tty != NULL);
        lock(&tty->lock);

        stat->mode = S_IFCHR | tty->pty.perms;
        stat->uid = tty->pty.uid;
        stat->gid = tty->pty.gid;
        stat->inode = pty_num + 3;
        stat->rdev = dev_make(TTY_PSEUDO_SLAVE_MAJOR, pty_num);

        unlock(&tty->lock);
        unlock(&ttys_lock);
    }
}

static int devpts_setattr_num(int pty_num, struct attr attr) {
    if (pty_num == -1)
        return _EROFS;
    if (attr.type == attr_size)
        return _EINVAL;

    lock(&ttys_lock);
    struct tty *tty = pty_slave.ttys[pty_num];
    assert(tty != NULL);
    lock(&tty->lock);

    switch (attr.type) {
        case attr_uid:
            tty->pty.uid = attr.uid;
            break;
        case attr_gid:
            tty->pty.gid = attr.gid;
            break;
        case attr_mode:
            tty->pty.perms = attr.mode;
            break;
    }

    unlock(&tty->lock);
    unlock(&ttys_lock);
    return 0;
}

static int devpts_fstat(struct fd *fd, struct statbuf *stat) {
    devpts_stat_num(fd->devpts.num, stat);
    return 0;
}

static int devpts_stat(struct mount *UNUSED(mount), const char *path, struct statbuf *stat) {
    int pty_num = devpts_get_pty_num(path);
    if (pty_num == _ENOENT)
        return _ENOENT;
    devpts_stat_num(pty_num, stat);
    return 0;
}

static int devpts_setattr(struct mount *UNUSED(mount), const char *path, struct attr attr) {
    int pty_num = devpts_get_pty_num(path);
    if (pty_num == _ENOENT)
        return _ENOENT;
    devpts_setattr_num(pty_num, attr);
    return 0;
}

static int devpts_fsetattr(struct fd *fd, struct attr attr) {
    devpts_setattr_num(fd->devpts.num, attr);
    return 0;
}

static int devpts_readdir(struct fd *fd, struct dir_entry *entry) {
    assert(fd->devpts.num == -1); // there shouldn't be anything to list but the root

    int pty_num = fd->offset;
    while (pty_num < MAX_PTYS && !devpts_pty_exists(pty_num))
        pty_num++;
    if (pty_num >= MAX_PTYS)
        return 0;
    fd->offset = pty_num + 1;
    sprintf(entry->name, "%d", pty_num);
    entry->inode = pty_num + 3;
    return 1;
}

const struct fs_ops devptsfs = {
    .name = "devpts", .magic = 0x1cd1,
    .open = devpts_open,
    .getpath = devpts_getpath,
    .stat = devpts_stat,
    .fstat = devpts_fstat,
    .setattr = devpts_setattr,
    .fsetattr = devpts_fsetattr,
};

static const struct fd_ops devpts_fdops = {
    .readdir = devpts_readdir,
};
