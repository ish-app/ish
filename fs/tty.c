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
        cond_init(&tty->produced);
        memset(tty->buf_flag, false, sizeof(tty->buf_flag));
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
        cond_destroy(&tty->produced);
        free(tty);
    } else {
        unlock(&tty->lock);
    }
    unlock(&ttys_lock);
}

static void tty_set_controlling(struct tgroup *group, struct tty *tty) {
    lock(&current->group->lock);
    if (current->group->tty == NULL) {
        current->group->tty = tty;
        tty->session = current->group->sid;
        tty->fg_group = current->group->pgid;
    }
    unlock(&current->group->lock);
}

static int tty_open(int major, int minor, int type, struct fd *fd) {
    assert(type == DEV_CHAR);

    struct tty *tty;
    if (major == 5 && minor == 0) {
        lock(&ttys_lock);
        lock(&current->group->lock);
        tty = current->group->tty;
        if (tty == NULL) {
            unlock(&current->group->lock);
            return _ENXIO;
        }
        tty->refcount++;
        unlock(&current->group->lock);
        unlock(&ttys_lock);
    } else {
        if (major == 4 && minor < 64)
            type = TTY_VIRTUAL;
        else if (major >= 136 && major <= 143)
            type = TTY_PSEUDO;
        else
            assert(false);
        int err = tty_get(type, minor, &tty);
        if (err < 0)
            return err;
    }
    fd->tty = tty;

    lock(&tty->fds_lock);
    list_add(&tty->fds, &fd->other_fds);
    unlock(&tty->fds_lock);

    lock(&pids_lock);
    if (current->group->sid == current->pid) {
        tty_set_controlling(current->group, tty);
    }
    unlock(&pids_lock);

    return 0;
}

static int tty_close(struct fd *fd) {
    if (fd->tty != NULL) {
        lock(&fd->tty->fds_lock);
        list_remove(&fd->other_fds);
        unlock(&fd->tty->fds_lock);
        tty_release(fd->tty);
    }
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

static void tty_push_char(struct tty *tty, char ch, bool flag) {
    if (tty->bufsize >= sizeof(tty->buf))
        return;
    tty->buf[tty->bufsize] = ch;
    tty->buf_flag[tty->bufsize++] = flag;
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
                if (tty->fg_group != 0) {
                    unlock(&tty->lock);
                    send_group_signal(tty->fg_group, SIGINT_);
                    lock(&tty->lock);
                }
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
                if (!(lflags & ECHO_))
                    echo = false;
                for (int i = 0; i < count; i++) {
                    // don't delete past a flag
                    if (tty->buf_flag[tty->bufsize - 1])
                        break;
                    tty->bufsize--;
                    if (echo) {
                        tty->driver->write(tty, "\b \b", 3);
                        if (SHOULD_ECHOCTL(tty->buf[tty->bufsize]))
                            tty->driver->write(tty, "\b \b", 3);
                    }
                }
                echo = false;
            } else if (ch == cc[VEOF_]) {
                ch = '\0';
                goto canon_wake;
            } else if (ch == '\n' || ch == cc[VEOL_]) {
                // echo it now, before the read call goes through
                if (echo)
                    tty->driver->write(tty, "\r\n", 2);
canon_wake:
                tty_push_char(tty, ch, true);
                echo = false;
                tty_wake(tty);
            } else {
                tty_push_char(tty, ch, false);
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

// expects bufsize <= tty->bufsize
static void tty_read_into_buf(struct tty *tty, void *buf, size_t bufsize) {
    memcpy(buf, tty->buf, bufsize);
    tty->bufsize -= bufsize;
    memmove(tty->buf, tty->buf + bufsize, tty->bufsize); // magic!
    memmove(tty->buf_flag, tty->buf_flag + bufsize, tty->bufsize);
}

static ssize_t tty_canon_size(struct tty *tty) {
    bool *flag_ptr = memchr(tty->buf_flag, true, tty->bufsize);
    if (flag_ptr == NULL)
        return -1;
    return flag_ptr - tty->buf_flag + 1;
}

static ssize_t tty_read(struct fd *fd, void *buf, size_t bufsize) {
    if (bufsize == 0)
        return 0;

    struct tty *tty = fd->tty;
    lock(&tty->lock);
    if (tty->termios.lflags & ICANON_) {
        ssize_t canon_size = -1;
        while ((canon_size = tty_canon_size(tty)) == -1) {
            if (wait_for(&tty->produced, &tty->lock, NULL))
                goto eintr;
        }
        // null byte means eof was typed
        if (tty->buf[canon_size-1] == '\0')
            canon_size--;

        if (bufsize > canon_size)
            bufsize = canon_size;
    } else {
        dword_t min = tty->termios.cc[VMIN_];
        dword_t time = tty->termios.cc[VTIME_];
        if (min == 0 && time == 0) {
            // no need to wait for anything
        } else if (min > 0 && time == 0) {
            while (tty->bufsize < min) {
                if (wait_for(&tty->produced, &tty->lock, NULL))
                    goto eintr;
            }
        } else {
            TODO("VTIME != 0");
        }
    }

    if (bufsize > tty->bufsize)
        bufsize = tty->bufsize;
    tty_read_into_buf(tty, buf, bufsize);
    if (tty->bufsize > 0 && tty->buf[0] == '\0' && tty->buf_flag[0]) {
        // remove the eof so the next read can succeed
        char dummy;
        tty_read_into_buf(tty, &dummy, 1);
    }

    unlock(&tty->lock);
    return bufsize;
eintr:
    unlock(&tty->lock);
    return _EINTR;
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
        if (tty_canon_size(tty) != -1)
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
#define TCSETSF_ 0x5404
#define TCFLSH_ 0x540b
#define TIOCSCTTY_ 0x540e
#define TIOCGPRGP_ 0x540f
#define TIOCSPGRP_ 0x5410
#define TIOCGWINSZ_ 0x5413
#define TIOCSWINSZ_ 0x5414
#define TCIFLUSH_ 0
#define TCOFLUSH_ 1
#define TCIOFLUSH_ 2

static ssize_t tty_ioctl_size(struct fd *fd, int cmd) {
    switch (cmd) {
        case TCGETS_: case TCSETS_: case TCSETSF_: case TCSETSW_:
            return sizeof(struct termios_);
        case TCFLSH_: case TIOCSCTTY_: return 0;
        case TIOCGPRGP_: case TIOCSPGRP_: return sizeof(dword_t);
        case TIOCGWINSZ_: case TIOCSWINSZ_: return sizeof(struct winsize_);
        case FIONREAD_: return sizeof(dword_t);
    }
    return -1;
}

static bool tty_is_current(struct tty *tty) {
    lock(&current->group->lock);
    bool is_current = current->group->tty == tty;
    unlock(&current->group->lock);
    return is_current;
}

static int tiocsctty(struct tty *tty, int force) {
    int err = 0;
    lock(&pids_lock);
    // do nothing if this is already our controlling tty
    if (current->group->sid == current->pid && current->group->sid == tty->session)
        goto out;
    // must not already have a tty
    if (current->group->tty != NULL) {
        err = _EPERM;
        goto out;
    }

    if (tty->session) {
        if (force == 1 && superuser()) {
            // steal it
            struct pid *pid = pid_get(tty->session);
            struct tgroup *tgroup;
            list_for_each_entry(&pid->session, tgroup, session) {
                lock(&tgroup->lock);
                tgroup->tty = NULL;
                unlock(&tgroup->lock);
            }
        } else {
            err = _EPERM;
            goto out;
        }
    }

    tty_set_controlling(current->group, tty);
out:
    unlock(&pids_lock);
    return err;
}

static int tty_ioctl(struct fd *fd, int cmd, void *arg) {
    int err = 0;
    struct tty *tty = fd->tty;
    lock(&tty->lock);

    switch (cmd) {
        case TCGETS_:
            *(struct termios_ *) arg = tty->termios;
            break;
        case TCSETSF_:
            tty->bufsize = 0;
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
                    break;
                case TCOFLUSH_:
                    break;
                default:
                    err = _EINVAL;
                    break;
            };
            break;

        case TIOCSCTTY_:
            err = tiocsctty(tty, (dword_t) arg);
            break;

        case TIOCGPRGP_:
            if (!tty_is_current(tty) || tty->fg_group == 0) {
                err = _ENOTTY;
                break;
            }
            STRACE("tty group = %d\n", tty->fg_group);
            *(dword_t *) arg = tty->fg_group; break;
        case TIOCSPGRP_:
            lock(&pids_lock);
            pid_t_ sid = current->group->sid;
            unlock(&pids_lock);
            if (!tty_is_current(tty) || sid != tty->session) {
                err = _ENOTTY;
                break;
            }
            // TODO group must be in the right session
            tty->fg_group = *(dword_t *) arg;
            STRACE("tty group set to = %d\n", tty->fg_group);
            break;

        case TIOCGWINSZ_:
            *(struct winsize_ *) arg = fd->tty->winsize;
            break;
        case TIOCSWINSZ_:
            tty_set_winsize(fd->tty, *(struct winsize_ *) arg);
            break;

        case FIONREAD_:
            *(dword_t *) arg = tty->bufsize;
            break;
    }

    unlock(&tty->lock);
    return err;
}

void tty_set_winsize(struct tty *tty, struct winsize_ winsize) {
    tty->winsize = winsize;
    if (tty->fg_group != 0) {
        unlock(&tty->lock);
        send_group_signal(tty->fg_group, SIGWINCH_);
        lock(&tty->lock);
    }
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
