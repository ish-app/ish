#define DEFAULT_CHANNEL debug
#include "debug.h"
#include <string.h>
#include "kernel/calls.h"
#include "fs/poll.h"
#include "fs/tty.h"

// TODO remove magic number
struct tty_driver tty_drivers[2];

// currently supports 64 ptys
// TODO replace with hashtable
// for future reference, if you run out of ptys the error code is ENOSPC
static struct tty *ttys[2][64];
// lock this before locking a tty
static lock_t ttys_lock = LOCK_INITIALIZER;

static int tty_get(int type, int num, struct tty **tty_out) {
    lock(&ttys_lock);
    struct tty *tty = ttys[type][num];
    if (tty == NULL) {
        tty = malloc(sizeof(struct tty));
        if (tty == NULL)
            return _ENOMEM;
        tty->refcount = 0;
        tty->type = type;
        tty->num = num;
        list_init(&tty->fds);
        // TODO default termios
        memset(&tty->winsize, 0, sizeof(tty->winsize));
        lock_init(&tty->lock);
        lock_init(&tty->fds_lock);
        pthread_cond_init(&tty->produced, NULL);
        pthread_cond_init(&tty->consumed, NULL);
        tty->bufsize = 0;

        tty->driver = &tty_drivers[type];
        if (tty->driver->open) {
            int err = tty->driver->open(tty);
            if (err < 0) {
                unlock(&ttys_lock);
                return err;
            }
        }

        tty->session = 0;
        tty->fg_group = 0;

        ttys[type][num] = tty;
    }
    lock(&tty->lock);
    tty->refcount++;
    unlock(&tty->lock);
    unlock(&ttys_lock);
    *tty_out = tty;
    return 0;
}

void tty_release(struct tty *tty) {
    lock(&ttys_lock);
    lock(&tty->lock);
    if (--tty->refcount == 0) {
        if (tty->driver->close)
            tty->driver->close(tty);
        ttys[tty->type][tty->num] = NULL;
        unlock(&tty->lock);
        free(tty);
    } else {
        unlock(&tty->lock);
    }
    unlock(&ttys_lock);
}

static int tty_open(int major, int minor, int type, struct fd *fd) {
    assert(type == DEV_CHAR);
    if (major == 4 && minor < 64)
        type = TTY_VIRTUAL;
    else if (major >= 136 && major <= 143)
        type = TTY_PSEUDO;
    else
        assert(false);

    struct tty *tty;
    int err = tty_get(type, minor, &tty);
    if (err < 0)
        return err;
    fd->tty = tty;

    lock(&tty->fds_lock);
    list_add(&tty->fds, &fd->other_fds);
    unlock(&tty->fds_lock);

    lock(&pids_lock);
    if (current->sid == current->pid) {
        lock(&current->group->lock);
        if (current->group->tty == NULL) {
            current->group->tty = tty;
            unlock(&current->group->lock);
            tty->session = current->sid;
            tty->fg_group = current->pgid;
        }
    }
    unlock(&pids_lock);

    return 0;
}

static int tty_close(struct fd *fd) {
    lock(&fd->tty->fds_lock);
    list_remove(&fd->other_fds);
    unlock(&fd->tty->fds_lock);
    tty_release(fd->tty);
    return 0;
}

static void tty_wake(struct tty *tty) {
    notify(&tty->produced);
    unlock(&tty->lock);
    struct fd *fd;
    lock(&tty->fds_lock);
    list_for_each_entry(&tty->fds, fd, other_fds) {
        poll_wake(fd);
    }
    unlock(&tty->fds_lock);
    lock(&tty->lock);
}

int tty_input(struct tty *tty, const char *input, size_t size) {
    lock(&tty->lock);
    dword_t lflags = tty->termios.lflags;
    dword_t iflags = tty->termios.iflags;
    unsigned char *cc = tty->termios.cc;

#define SHOULD_ECHOCTL(ch) \
    (lflags & ECHOCTL_ && \
     (ch < ' ' || ch == '\x7f') && \
     !(ch == '\t' || ch == '\n' || ch == cc[VSTART_] || ch == cc[VSTOP_]))

    if (lflags & ISIG_) {
        for (size_t i = 0; i < size; i++) {
            char ch = input[i];
            if (ch == cc[VINTR_]) {
                input += i + 1;
                size -= i + 1;
                i = 0;
                if (tty->fg_group != 0)
                    send_group_signal(tty->fg_group, SIGINT_);
            }
        }
    }

    if (lflags & ICANON_) {
        for (size_t i = 0; i < size; i++) {
            char ch = input[i];
            bool echo = lflags & ECHO_;

            if (iflags & INLCR_ && ch == '\n')
                ch = '\r';
            else if (iflags & ICRNL_ && ch == '\r')
                ch = '\n';
            if (iflags & IGNCR_ && ch == '\r')
                continue;

            if (ch == cc[VERASE_] || ch == cc[VKILL_]) {
                echo = lflags & ECHOK_;
                int count = tty->bufsize;
                if (ch == cc[VERASE_] && tty->bufsize > 0) {
                    echo = lflags & ECHOE_;
                    count = 1;
                }
                for (int i = 0; i < count; i++) {
                    tty->bufsize--;
                    if (lflags & ECHOE_) {
                        tty->driver->write(tty, "\b \b", 3);
                        if (SHOULD_ECHOCTL(tty->buf[tty->bufsize]))
                            tty->driver->write(tty, "\b \b", 3);
                    }
                }
                echo = false;
            } else if (ch == '\n' || ch == cc[VEOF_]) {
                if (ch == '\n') {
                    tty->buf[tty->bufsize++] = ch;
                    // echo it now, before the read call goes through
                    if (echo)
                        tty->driver->write(tty, "\r\n", 2);
                }

                echo = false;
                tty->canon_ready = true;
                tty_wake(tty);
                while (tty->canon_ready)
                    wait_for(&tty->consumed, &tty->lock);
            } else {
                if (tty->bufsize < sizeof(tty->buf))
                    tty->buf[tty->bufsize++] = ch;
            }

            if (echo) {
                if (SHOULD_ECHOCTL(ch)) {
                    tty->driver->write(tty, "^", 1);
                    ch ^= '\100';
                }
                tty->driver->write(tty, &ch, 1);
            }
        }
    } else {
        if (size > sizeof(tty->buf) - 1 - tty->bufsize)
            size = sizeof(tty->buf) - 1 - tty->bufsize;
        memcpy(tty->buf + tty->bufsize, input, size);
        tty->bufsize += size;
        tty_wake(tty);
    }

    unlock(&tty->lock);
    return 0;
}

static ssize_t tty_read(struct fd *fd, void *buf, size_t bufsize) {
    struct tty *tty = fd->tty;
    lock(&tty->lock);
    if (tty->termios.lflags & ICANON_) {
        while (!tty->canon_ready)
            wait_for(&tty->produced, &tty->lock);
    } else {
        dword_t min = tty->termios.cc[VMIN_];
        dword_t time = tty->termios.cc[VTIME_];
        if (min == 0 && time == 0) {
            // no need to wait for anything
        } else if (min > 0 && time == 0) {
            while (tty->bufsize < min)
                wait_for(&tty->produced, &tty->lock);
        } else {
            TODO("VTIME != 0");
        }
    }

    if (bufsize > tty->bufsize)
        bufsize = tty->bufsize;
    memcpy(buf, tty->buf, bufsize);
    tty->bufsize -= bufsize;
    memmove(tty->buf, tty->buf + bufsize, tty->bufsize); // magic!
    if (tty->bufsize == 0) {
        tty->canon_ready = false;
        notify(&tty->consumed);
    }

    unlock(&tty->lock);
    return bufsize;
}

static ssize_t tty_write(struct fd *fd, const void *buf, size_t bufsize) {
    struct tty *tty = fd->tty;
    lock(&tty->lock);
    dword_t oflags = tty->termios.oflags;
    if (oflags & OPOST_) {
        const char *cbuf = buf;
        for (size_t i = 0; i < bufsize; i++) {
            char ch = cbuf[i];
            if (ch == '\r' && oflags & ONLRET_)
                continue;
            else if (ch == '\r' && oflags & OCRNL_)
                ch = '\n';
            else if (ch == '\n' && oflags & ONLCR_)
                tty->driver->write(tty, "\r", 1);
            tty->driver->write(tty, &ch, 1);
        }
    } else {
        tty->driver->write(tty, buf, bufsize);
    }
    unlock(&tty->lock);
    return bufsize;
}

static int tty_poll(struct fd *fd) {
    struct tty *tty = fd->tty;
    lock(&tty->lock);
    int types = POLL_WRITE;
    if (tty->termios.lflags & ICANON_) {
        if (tty->canon_ready)
            types |= POLL_READ;
    } else {
        if (tty->bufsize > 0)
            types |= POLL_READ;
    }
    unlock(&tty->lock);
    return types;
}

#define TCGETS_ 0x5401
#define TCSETS_ 0x5402
#define TCSETSW_ 0x5403
#define TCFLSH_ 0x540b
#define TIOCGPRGP_ 0x540f
#define TIOCSPGRP_ 0x5410
#define TIOCGWINSZ_ 0x5413
#define TCIFLUSH_ 0
#define TCOFLUSH_ 1
#define TCIOFLUSH_ 2

static ssize_t tty_ioctl_size(struct fd *fd, int cmd) {
    switch (cmd) {
        case TCGETS_: case TCSETS_: case TCSETSW_:
            return sizeof(struct termios_);
        case TCFLSH_: return 0;
        case TIOCGPRGP_: case TIOCSPGRP_: return sizeof(dword_t);
        case TIOCGWINSZ_: return sizeof(struct winsize_);
    }
    return -1;
}

static bool tty_is_current(struct tty *tty) {
    lock(&current->group->lock);
    bool is_current = current->group->tty == tty;
    unlock(&current->group->lock);
    return is_current;
}

static int tty_ioctl(struct fd *fd, int cmd, void *arg) {
    int err = 0;
    struct tty *tty = fd->tty;
    lock(&tty->lock);

    switch (cmd) {
        case TCGETS_:
            *(struct termios_ *) arg = tty->termios;
            break;
        case TCSETSW_:
            // we have no output buffer currently
        case TCSETS_:
            tty->termios = *(struct termios_ *) arg;
            break;

        case TCFLSH_:
            // only input flushing is currently useful
            switch ((dword_t) arg) {
                case TCIFLUSH_:
                case TCIOFLUSH_:
                    tty->bufsize = 0;
                    tty->canon_ready = false;
                    notify(&tty->consumed);
                    break;
                case TCOFLUSH_:
                    break;
                default:
                    err = _EINVAL;
                    break;
            };
            break;

        case TIOCGPRGP_:
            if (!tty_is_current(tty) || tty->fg_group == 0) {
                err = _ENOTTY;
                break;
            }
            TRACELN("tty group = %d", tty->fg_group);
            *(dword_t *) arg = tty->fg_group; break;
        case TIOCSPGRP_:
            // FIXME I think current->sid needs to be locked
            if (!tty_is_current(tty) || current->sid != tty->session) {
                err = _ENOTTY;
                break;
            }
            // TODO group must be in the right session
            tty->fg_group = *(dword_t *) arg;
            TRACELN("tty group set to = %d", tty->fg_group);
            break;

        case TIOCGWINSZ_:
            *(struct winsize_ *) arg = fd->tty->winsize;
            break;
    }

    unlock(&tty->lock);
    return err;
}

struct dev_ops tty_dev = {
    .open = tty_open,
    .fd.close = tty_close,
    .fd.read = tty_read,
    .fd.write = tty_write,
    .fd.poll = tty_poll,
    .fd.ioctl_size = tty_ioctl_size,
    .fd.ioctl = tty_ioctl,
};
/* struct dev_ops ptmx_dev = {.open = ptmx_open}; */
