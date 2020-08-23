#define DEFAULT_CHANNEL debug
#include "debug.h"
#include <string.h>
#include "kernel/calls.h"
#include "fs/poll.h"
#include "fs/tty.h"
#include "fs/devices.h"

extern struct tty_driver pty_master;
extern struct tty_driver pty_slave;

struct tty_driver *tty_drivers[256] = {
    [TTY_CONSOLE_MAJOR] = NULL, // will be filled in by create_stdio
    [TTY_PSEUDO_MASTER_MAJOR] = &pty_master,
    [TTY_PSEUDO_SLAVE_MAJOR] = &pty_slave,
};

// lock this before locking a tty
lock_t ttys_lock = LOCK_INITIALIZER;

struct tty *tty_alloc(struct tty_driver *driver, int type, int num) {
    struct tty *tty = malloc(sizeof(struct tty));
    if (tty == NULL)
        return NULL;

    tty->refcount = 0;
    tty->driver = driver;
    tty->type = type;
    tty->num = num;
    tty->hung_up = false;
    tty->ever_opened = false;
    tty->session = 0;
    tty->fg_group = 0;
    list_init(&tty->fds);

    tty->termios.iflags = ICRNL_ | IXON_;
    tty->termios.oflags = OPOST_ | ONLCR_;
    tty->termios.cflags = 0;
    tty->termios.lflags = ISIG_ | ICANON_ | ECHO_ | ECHOE_ | ECHOK_ | ECHOCTL_ | ECHOKE_ | IEXTEN_;
    // from include/asm-generic/termios.h
    memcpy(tty->termios.cc, "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0\0\0", 19);
    memset(&tty->winsize, 0, sizeof(tty->winsize));

    lock_init(&tty->lock);
    lock_init(&tty->fds_lock);
    cond_init(&tty->produced);
    cond_init(&tty->consumed);
    memset(tty->buf_flag, false, sizeof(tty->buf_flag));
    tty->bufsize = 0;
    tty->packet_flags = 0;

    return tty;
}

struct tty *tty_get(struct tty_driver *driver, int type, int num) {
    lock(&ttys_lock);
    struct tty *tty = driver->ttys[num];
    // pty_reserve_next stores 1 to avoid races on the same tty
    if (tty == NULL || tty == (void *) 1 /* ew */) {
        tty = tty_alloc(driver, type, num);
        if (tty == NULL) {
            unlock(&ttys_lock);
            return ERR_PTR(_ENOMEM);
        }

        if (driver->ops->init) {
            int err = driver->ops->init(tty);
            if (err < 0) {
                unlock(&ttys_lock);
                return ERR_PTR(err);
            }
        }
        driver->ttys[num] = tty;
    }
    lock(&tty->lock);
    tty->refcount++;
    tty->ever_opened = true;
    unlock(&tty->lock);
    unlock(&ttys_lock);
    return tty;
}

static void tty_poll_wakeup(struct tty *tty, int events) {
    unlock(&tty->lock);
    struct fd *fd;
    lock(&tty->fds_lock);
    list_for_each_entry(&tty->fds, fd, tty_other_fds) {
        poll_wakeup(fd, events);
    }
    unlock(&tty->fds_lock);
    lock(&tty->lock);
}

void tty_release(struct tty *tty) {
    lock(&tty->lock);
    if (--tty->refcount == 0) {
        struct tty_driver *driver = tty->driver;
        if (driver->ops->cleanup)
            driver->ops->cleanup(tty);
        driver->ttys[tty->num] = NULL;
        unlock(&tty->lock);
        cond_destroy(&tty->produced);
        free(tty);
    } else {
        // bit of a hack
        struct tty *master = NULL;
        if (tty->driver == &pty_slave && tty->refcount == 1)
            master = tty->pty.other;
        unlock(&tty->lock);
        if (master != NULL) {
            lock(&master->lock);
            tty_poll_wakeup(master, POLL_READ | POLL_HUP);
            unlock(&master->lock);
        }
    }
}

// must call with tty lock
static void tty_set_controlling(struct tgroup *group, struct tty *tty) {
    lock(&group->lock);
    if (group->tty == NULL) {
        tty->refcount++;
        group->tty = tty;
        tty->session = group->sid;
        tty->fg_group = group->pgid;
    }
    unlock(&group->lock);
}

// by default, /dev/console is /dev/tty1
int console_major = TTY_CONSOLE_MAJOR;
int console_minor = 1;

int tty_open(struct tty *tty, struct fd *fd) {
    fd->tty = tty;

    lock(&tty->fds_lock);
    list_add(&tty->fds, &fd->tty_other_fds);
    unlock(&tty->fds_lock);

    if (!(fd->flags & O_NOCTTY_)) {
        // Make this our controlling terminal if:
        // - the terminal doesn't already have a session
        // - we're a session leader
        lock(&pids_lock);
        lock(&tty->lock);
        if (tty->session == 0 && current->group->sid == current->pid)
            tty_set_controlling(current->group, tty);
        unlock(&tty->lock);
        unlock(&pids_lock);
    }

    return 0;
}

static int tty_device_open(int major, int minor, struct fd *fd) {
    struct tty *tty;
    if (major == TTY_ALTERNATE_MAJOR) {
        if (minor == DEV_TTY_MINOR) {
            lock(&ttys_lock);
            lock(&current->group->lock);
            tty = current->group->tty;
            unlock(&current->group->lock);
            if (tty != NULL) {
                lock(&tty->lock);
                tty->refcount++;
                unlock(&tty->lock);
            }
            unlock(&ttys_lock);
            if (tty == NULL)
                return _ENXIO;
        } else if (minor == DEV_CONSOLE_MINOR) {
            return tty_device_open(console_major, console_minor, fd);
        } else if (minor == DEV_PTMX_MINOR) {
            return ptmx_open(fd);
        } else {
            return _ENXIO;
        }
    } else {
        struct tty_driver *driver = tty_drivers[major];
        assert(driver != NULL);
        tty = tty_get(driver, major, minor);
        if (IS_ERR(tty))
            return PTR_ERR(tty);
    }

    if (tty->driver->ops->open) {
        int err = tty->driver->ops->open(tty);
        if (err < 0) {
            lock(&ttys_lock);
            tty_release(tty);
            unlock(&ttys_lock);
            return err;
        }
    }

    return tty_open(tty, fd);
}

static int tty_close(struct fd *fd) {
    if (fd->tty != NULL) {
        lock(&fd->tty->fds_lock);
        list_remove_safe(&fd->tty_other_fds);
        unlock(&fd->tty->fds_lock);
        lock(&ttys_lock);
        tty_release(fd->tty);
        unlock(&ttys_lock);
    }
    return 0;
}

static void tty_input_wakeup(struct tty *tty) {
    notify(&tty->produced);
    tty_poll_wakeup(tty, POLL_READ);
}

static int tty_push_char(struct tty *tty, char ch, bool flag, int blocking) {
    while (tty->bufsize >= sizeof(tty->buf)) {
        if (!blocking)
            return _EAGAIN;
        if (wait_for(&tty->consumed, &tty->lock, NULL))
            return _EINTR;
    }
    tty->buf[tty->bufsize] = ch;
    tty->buf_flag[tty->bufsize++] = flag;
    return 0;
}

static void tty_echo(struct tty *tty, const char *data, size_t size) {
    tty->driver->ops->write(tty, data, size, false);
}

static bool tty_send_input_signal(struct tty *tty, char ch, sigset_t_ *queue) {
    if (!(tty->termios.lflags & ISIG_))
        return false;
    unsigned char *cc = tty->termios.cc;
    int sig;
    if (ch == '\0')
        return false; // '\0' is used to disable cc entries
    else if (ch == cc[VINTR_])
        sig = SIGINT_;
    else if (ch == cc[VQUIT_])
        sig = SIGQUIT_;
    else if (ch == cc[VSUSP_])
        sig = SIGTSTP_;
    else
        return false;

    if (tty->fg_group != 0) {
        if (!(tty->termios.lflags & NOFLSH_))
            tty->bufsize = 0;
        sigset_add(queue, sig);
    }
    return true;
}

ssize_t tty_input(struct tty *tty, const char *input, size_t size, bool blocking) {
    int err = 0;
    size_t done_size = 0;
    sigset_t_ queue = 0; // to prevent having to lock tty->lock and pids_lock at the same time

    lock(&tty->lock);
    dword_t lflags = tty->termios.lflags;
    dword_t iflags = tty->termios.iflags;
    unsigned char *cc = tty->termios.cc;

#define SHOULD_ECHOCTL(ch) \
    (lflags & ECHOCTL_ && \
     ((0 <= ch && ch < ' ') || ch == '\x7f') && \
     !(ch == '\t' || ch == '\n' || ch == cc[VSTART_] || ch == cc[VSTOP_]))

    if (lflags & ICANON_) {
        for (size_t i = 0; i < size; i++) {
            done_size++;
            char ch = input[i];
            bool echo = lflags & ECHO_;

            if (iflags & INLCR_ && ch == '\n')
                ch = '\r';
            else if (iflags & ICRNL_ && ch == '\r')
                ch = '\n';
            if (iflags & IGNCR_ && ch == '\r')
                continue;

            if (ch == '\0') {
                // '\0' is used to disable cc entries
                goto no_special;
            } else if (ch == cc[VERASE_] || ch == cc[VKILL_]) {
                // FIXME ECHOE and ECHOK are supposed to enable these
                // ECHOKE enables erasing the line instead of echoing the kill char and outputting a newline
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
                        tty_echo(tty, "\b \b", 3);
                        if (SHOULD_ECHOCTL(tty->buf[tty->bufsize]))
                            tty_echo(tty, "\b \b", 3);
                    }
                }
                echo = false;
            } else if (ch == cc[VEOF_]) {
                ch = '\0';
                goto canon_wake;
            } else if (ch == '\n' || ch == cc[VEOL_]) {
                // echo it now, before the read call goes through
                if (echo)
                    tty_echo(tty, "\r\n", 2);
canon_wake:
                err = tty_push_char(tty, ch, /*flag*/true, blocking);
                if (err < 0) {
                    done_size--;
                    break;
                }
                echo = false;
                tty_input_wakeup(tty);
            } else {
                if (!tty_send_input_signal(tty, ch, &queue)) {
no_special:
                    err = tty_push_char(tty, ch, /*flag*/false, blocking);
                    if (err < 0) {
                        done_size--;
                        break;
                    }
                }
            }

            if (echo) {
                if (SHOULD_ECHOCTL(ch)) {
                    tty_echo(tty, "^", 1);
                    ch ^= '\100';
                }
                tty_echo(tty, &ch, 1);
            }
        }
    } else {
        for (size_t i = 0; i < size; i++) {
            done_size++;
            if (tty_send_input_signal(tty, input[i], &queue))
                continue;
            while (tty->bufsize >= sizeof(tty->buf)) {
                err = _EAGAIN;
                if (!blocking)
                    break;
                err = wait_for(&tty->consumed, &tty->lock, NULL);
                if (err < 0)
                    break;
            }
            if (err < 0) {
                done_size--;
                break;
            }
            assert(tty->bufsize < sizeof(tty->buf));
            tty->buf[tty->bufsize++] = input[i];
        }
        if (tty->bufsize > 0)
            tty_input_wakeup(tty);
    }

    pid_t_ fg_group = tty->fg_group;
    assert(tty->bufsize <= sizeof(tty->buf));
    unlock(&tty->lock);

    if (fg_group != 0) {
        for (int sig = 1; sig < NUM_SIGS; sig++) {
            if (sigset_has(queue, sig))
                send_group_signal(fg_group, sig, SIGINFO_NIL);
        }
    }

    if (done_size > 0)
        return done_size;
    return err;
}

// expects bufsize <= tty->bufsize
static void tty_read_into_buf(struct tty *tty, void *buf, size_t bufsize) {
    assert(bufsize <= tty->bufsize);
    memcpy(buf, tty->buf, bufsize);
    tty->bufsize -= bufsize;
    memmove(tty->buf, tty->buf + bufsize, tty->bufsize); // magic!
    memmove(tty->buf_flag, tty->buf_flag + bufsize, tty->bufsize);
    notify(&tty->consumed);
}

static size_t tty_canon_size(struct tty *tty) {
    bool *flag_ptr = memchr(tty->buf_flag, true, tty->bufsize);
    if (flag_ptr == NULL)
        return -1;
    return flag_ptr - tty->buf_flag + 1;
}

static bool pty_is_half_closed_master(struct tty *tty) {
    if (tty->driver != &pty_master)
        return false;

    struct tty *slave = tty->pty.other;
    // only time one tty lock is nested in another
    lock(&slave->lock);
    bool half_closed = slave->ever_opened && slave->refcount == 1;
    unlock(&slave->lock);
    return half_closed;
}

static bool tty_is_current(struct tty *tty) {
    lock(&current->group->lock);
    bool is_current = current->group->tty == tty;
    unlock(&current->group->lock);
    return is_current;
}

static int tty_signal_if_background(struct tty *tty, pid_t_ current_pgid, int sig) {
    // you can apparently access a terminal that's not your controlling
    // terminal all you want
    if (!tty_is_current(tty))
        return 0;
    // check if we're in the foreground
    if (tty->fg_group == 0 || current_pgid == tty->fg_group)
        return 0;

    if (!try_self_signal(sig))
        return _EIO;
    else
        return _EINTR;
}

static ssize_t tty_read(struct fd *fd, void *buf, size_t bufsize) {
    // important because otherwise we'll block
    if (bufsize == 0)
        return 0;

    int err = 0;
    struct tty *tty = fd->tty;
    lock(&pids_lock);
    lock(&tty->lock);
    if (tty->hung_up) {
        unlock(&pids_lock);
        goto error;
    }

    pid_t_ current_pgid = current->group->pgid;
    unlock(&pids_lock);
    err = tty_signal_if_background(tty, current_pgid, SIGTTIN_);
    if (err < 0)
        goto error;

    int bufsize_extra = 0;
    if (tty->driver == &pty_master && tty->pty.packet_mode) {
        char *cbuf = buf;
        *cbuf++ = tty->packet_flags;
        bufsize--;
        bufsize_extra++;
        buf = cbuf;
        if (tty->packet_flags != 0) {
            bufsize = 0;
            goto out;
        }

        // check again in case bufsize was 1
        if (bufsize == 0)
            goto out;
    }

    // wait loop(s)
    if (tty->termios.lflags & ICANON_) {
        size_t canon_size;
        while ((canon_size = tty_canon_size(tty)) == (size_t) -1) {
            err = _EIO;
            if (pty_is_half_closed_master(tty))
                goto error;
            err = _EAGAIN;
            if (fd->flags & O_NONBLOCK_)
                goto error;
            err = wait_for(&tty->produced, &tty->lock, NULL);
            if (err < 0)
                goto error;
        }
        // null byte means eof was typed
        if (tty->buf[canon_size-1] == '\0')
            canon_size--;

        if (bufsize > canon_size)
            bufsize = canon_size;
    } else {
        dword_t min = tty->termios.cc[VMIN_];
        dword_t time = tty->termios.cc[VTIME_];

        struct timespec timeout;
        // time is in tenths of a second
        timeout.tv_sec = time / 10;
        timeout.tv_nsec = (time % 10) * 100000000;
        struct timespec *timeout_ptr = &timeout;
        if (time == 0)
            timeout_ptr = NULL;

        while (tty->bufsize < min) {
            err = _EIO;
            if (pty_is_half_closed_master(tty))
                goto error;
            err = _EAGAIN;
            if (fd->flags & O_NONBLOCK_)
                goto error;
            // there should be no timeout for the first character read
            err = wait_for(&tty->produced, &tty->lock, tty->bufsize == 0 ? NULL : timeout_ptr);
            if (err == _ETIMEDOUT)
                break;
            if (err == _EINTR)
                goto error;
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

out:
    unlock(&tty->lock);
    return bufsize + bufsize_extra;
error:
    unlock(&tty->lock);
    return err;
}

static ssize_t tty_write(struct fd *fd, const void *buf, size_t bufsize) {
    struct tty *tty = fd->tty;
    lock(&tty->lock);
    if (tty->hung_up) {
        unlock(&tty->lock);
        return _EIO;
    }

    bool blocking = !(fd->flags & O_NONBLOCK_);
    dword_t oflags = tty->termios.oflags;
    // we have to unlock it now to avoid lock ordering problems with ptys
    // the code below is safe because it only accesses tty->driver which is immutable
    // I reviewed real driver and ios driver and they're safe
    unlock(&tty->lock);

    int err = 0;
    char *postbuf = NULL;
    size_t postbufsize = bufsize;
    if (oflags & OPOST_) {
        postbuf = malloc(bufsize * 2);
        postbufsize = 0;
        const char *cbuf = buf;
        for (size_t i = 0; i < bufsize; i++) {
            char ch = cbuf[i];
            if (ch == '\r' && oflags & ONLRET_)
                continue;
            else if (ch == '\r' && oflags & OCRNL_)
                ch = '\n';
            else if (ch == '\n' && oflags & ONLCR_)
                postbuf[postbufsize++] = '\r';
            postbuf[postbufsize++] = ch;
        }
        buf = postbuf;
    }
    err = tty->driver->ops->write(tty, buf, postbufsize, blocking);
    if (postbuf)
        free(postbuf);
    if (err < 0)
        return err;
    return bufsize;
}

static int tty_poll(struct fd *fd) {
    struct tty *tty = fd->tty;
    lock(&tty->lock);
    int types = 0;
    types |= POLL_WRITE; // FIXME now that we have ptys, you can't always write without blocking
    if (tty->hung_up) {
        types |= POLL_READ | POLL_WRITE | POLL_ERR | POLL_HUP;
    } else if (pty_is_half_closed_master(tty)) {
        types |= POLL_READ | POLL_HUP;
    } else if (tty->termios.lflags & ICANON_) {
        if (tty_canon_size(tty) != (size_t) -1)
            types |= POLL_READ;
    } else {
        if (tty->bufsize > 0)
            types |= POLL_READ;
    }
    if (tty->driver == &pty_master && tty->packet_flags != 0)
        types |= POLL_PRI;
    unlock(&tty->lock);
    return types;
}

static ssize_t tty_ioctl_size(int cmd) {
    switch (cmd) {
        case TCGETS_: case TCSETS_: case TCSETSF_: case TCSETSW_:
            return sizeof(struct termios_);
        case TIOCGWINSZ_: case TIOCSWINSZ_:
            return sizeof(struct winsize_);
        case TIOCGPRGP_: case TIOCSPGRP_:
        case TIOCSPTLCK_: case TIOCGPTN_:
        case TIOCPKT_: case TIOCGPKT_:
        case FIONREAD_:
            return sizeof(dword_t);
        case TCFLSH_: case TIOCSCTTY_:
            return 0;
    }
    return -1;
}

static int tiocsctty(struct tty *tty, int force) {
    int err = 0;
    unlock(&tty->lock); //aaaaaaaa
    // it's safe because literally nothing happens between that unlock and the last lock, and repulsive for the same reason
    // locking is ***hard**
    lock(&pids_lock);
    lock(&tty->lock);
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
                if (tgroup->tty == tty) {
                    tgroup->tty = NULL;
                    tty->refcount--;
                }
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

// These ioctls are separated out because they have to operate on the slave
// side of a pseudoterminal pair even if the master is specified
static int tty_mode_ioctl(struct tty *in_tty, int cmd, void *arg) {
    int err = 0;
    struct tty *tty = in_tty;
    if (in_tty->driver == &pty_master) {
        tty = in_tty->pty.other;
        lock(&tty->lock);
    }

    switch (cmd) {
        case TCGETS_:
            *(struct termios_ *) arg = tty->termios;
            break;
        case TCSETSF_:
            tty->bufsize = 0;
            notify(&tty->consumed);
            fallthrough;
        case TCSETSW_:
            // we have no output buffer currently
        case TCSETS_:
            tty->termios = *(struct termios_ *) arg;
            break;

        case TIOCGWINSZ_:
            *(struct winsize_ *) arg = tty->winsize;
            break;
        case TIOCSWINSZ_:
            tty_set_winsize(tty, *(struct winsize_ *) arg);
            break;

        default:
            err = _ENOTTY;
            break;
    }

    if (in_tty->driver == &pty_master)
        unlock(&tty->lock);
    return err;
}

static int tty_ioctl(struct fd *fd, int cmd, void *arg) {
    int err = 0;
    struct tty *tty = fd->tty;
    lock(&tty->lock);
    if (tty->hung_up) {
        unlock(&tty->lock);
        if (cmd == TIOCSPGRP_)
            return _ENOTTY;
        return _EIO;
    }

    switch (cmd) {
        case TCFLSH_:
            // only input flushing is currently useful
            switch ((uintptr_t) arg) {
                case TCIFLUSH_:
                case TCIOFLUSH_:
                    tty->bufsize = 0;
                    notify(&tty->consumed);
                    break;
                case TCOFLUSH_:
                    break;
                default:
                    err = _EINVAL;
                    break;
            };
            break;

        case TIOCSCTTY_:
            err = tiocsctty(tty, (uintptr_t) arg);
            break;

        case TIOCGPRGP_:
            if (!tty_is_current(tty) || tty->fg_group == 0) {
                err = _ENOTTY;
                break;
            }
            STRACE("tty group = %d\n", tty->fg_group);
            *(dword_t *) arg = tty->fg_group; break;
        case TIOCSPGRP_:
            // see "aaaaaaaa" comment above
            unlock(&tty->lock);
            lock(&pids_lock);
            lock(&tty->lock);
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

        case FIONREAD_:
            *(dword_t *) arg = tty->bufsize;
            break;

        default:
            err = tty_mode_ioctl(tty, cmd, arg);
            if (err == _ENOTTY && tty->driver->ops->ioctl)
                err = tty->driver->ops->ioctl(tty, cmd, arg);
    }

    unlock(&tty->lock);
    return err;
}

void tty_set_winsize(struct tty *tty, struct winsize_ winsize) {
    tty->winsize = winsize;
    if (tty->fg_group != 0)
        send_group_signal(tty->fg_group, SIGWINCH_, SIGINFO_NIL);
}

void tty_hangup(struct tty *tty) {
    tty->hung_up = true;
    tty_poll_wakeup(tty, POLL_READ | POLL_WRITE | POLL_ERR | POLL_HUP);
}

struct dev_ops tty_dev = {
    .open = tty_device_open,
    .fd.close = tty_close,
    .fd.read = tty_read,
    .fd.write = tty_write,
    .fd.poll = tty_poll,
    .fd.ioctl_size = tty_ioctl_size,
    .fd.ioctl = tty_ioctl,
};
