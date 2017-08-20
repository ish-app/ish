#include <string.h>
#include "sys/calls.h"
#include "fs/tty.h"

// TODO remove magic number
struct tty_driver tty_drivers[2];

// currently supports 64 ptys
// TODO replace with hashtable
// for future reference, if you run out of ptys the error code is ENOSPC
static struct tty *ttys[2][64];
// lock this before locking a tty
static pthread_mutex_t ttys_lock = PTHREAD_MUTEX_INITIALIZER;

static int tty_get(int type, int num, struct tty **tty_out) {
    pthread_mutex_lock(&ttys_lock);
    struct tty *tty = ttys[type][num];
    if (tty == NULL) {
        tty = malloc(sizeof(struct tty));
        if (tty == NULL)
            return _ENOMEM;
        tty->refcnt = 0;
        tty->type = type;
        tty->num = num;
        // TODO default termios
        memset(&tty->winsize, sizeof(tty->winsize), 0);
        pthread_mutex_init(&tty->lock, NULL);
        pthread_cond_init(&tty->produced, NULL);
        pthread_cond_init(&tty->consumed, NULL);

        tty->driver = &tty_drivers[type];
        int err = tty->driver->open(tty);
        if (err < 0) {
            pthread_mutex_unlock(&ttys_lock);
            return err;
        }

        ttys[type][num] = tty;
        pthread_mutex_unlock(&ttys_lock);
    }
    lock(tty);
    tty->refcnt++;
    unlock(tty);
    pthread_mutex_unlock(&ttys_lock);
    *tty_out = tty;
    return 0;
}

static void tty_release(struct tty *tty) {
    lock(tty);
    if (--tty->refcnt == 0) {
        tty->driver->close(tty);
        // dance necessary to prevent deadlock
        unlock(tty);
        pthread_mutex_lock(&ttys_lock);
        lock(tty);
        ttys[tty->type][tty->num] = NULL;
        free(tty);
        pthread_mutex_unlock(&ttys_lock);
    } else {
        unlock(tty);
    }
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
    return 0;
}

static int tty_close(struct fd *fd) {
    tty_release(fd->tty);
    return 0;
}

int tty_input(struct tty *tty, const char *input, size_t size) {
    lock(tty);
    dword_t lflags = tty->termios.lflags;
    dword_t iflags = tty->termios.iflags;
    unsigned char *cc = tty->termios.cc;

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
                int count = 0;
                if (ch == cc[VERASE_] && tty->bufsize > 0)
                    count = 1;
                else
                    count = tty->bufsize;
                for (int i = 0; i < count; i++) {
                    tty->bufsize--;
                    if (lflags & ECHOE_) {
                        tty->driver->write(tty, "\b \b", 3);
                        echo = false;
                    }
                }
            } else if (ch == '\n' || ch == cc[VEOF_]) {
                if (ch == '\n') {
                    tty->buf[tty->bufsize++] = ch;
                    // echo it now, before the read call goes through
                    if (echo) {
                        tty->driver->write(tty, "\r\n", 2);
                        echo = false;
                    }
                }

                tty->canon_ready = true;
                signal(tty, produced);
                while (tty->canon_ready)
                    wait_for(tty, consumed);
            } else {
                if (tty->bufsize < sizeof(tty->buf))
                    tty->buf[tty->bufsize++] = ch;
            }

            if (echo)
                tty->driver->write(tty, &ch, 1);
        }
    } else {
        if (size > sizeof(tty->buf) - 1 - tty->bufsize)
            size = sizeof(tty->buf) - 1 - tty->bufsize;
        memcpy(tty->buf + tty->bufsize, input, size);
        tty->bufsize += size;
        signal(tty, produced);
    }

    unlock(tty);
    return 0;
}

static ssize_t tty_read(struct fd *fd, void *buf, size_t bufsize) {
    struct tty *tty = fd->tty;
    lock(tty);
    if (tty->termios.lflags & ICANON_) {
        while (!tty->canon_ready)
            wait_for(tty, produced);
    } else {
        dword_t min = tty->termios.cc[VMIN_];
        dword_t time = tty->termios.cc[VTIME_];
        if (min == 0 && time == 0) {
            // no need to wait for anything
        } else if (min > 0 && time == 0) {
            while (tty->bufsize < min) {
                println("have %zu, waiting for %d", tty->bufsize, min);
                wait_for(tty, produced);
            }
        } else {
            TODO("VTIME != 0");
        }
    }

    if (bufsize > tty->bufsize)
        bufsize = tty->bufsize;
    memcpy(buf, tty->buf, bufsize);
    tty->bufsize -= bufsize;
    memmove(tty->buf, tty->buf + bufsize, tty->bufsize); // magic!
    tty->canon_ready = false;
    signal(tty, consumed);

    unlock(tty);
    for (size_t i = 0; i < bufsize; i++) {
        printf("read %x %c\r\n", ((char*)buf)[i], ((char*)buf)[i]);
    }
    println("done");
    return bufsize;
}

static ssize_t tty_write(struct fd *fd, const void *buf, size_t bufsize) {
    struct tty *tty = fd->tty;
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
    return bufsize;
}

static ssize_t tty_ioctl_size(struct fd *fd, int cmd) {
    return fd->tty->driver->ioctl_size(fd->tty, cmd);
}
static int tty_ioctl(struct fd *fd, int cmd, void *arg) {
    return fd->tty->driver->ioctl(fd->tty, cmd, arg);
}

struct dev_ops tty_dev = {
    .open = tty_open,
    .fd = {
        .close = tty_close,
        .read = tty_read,
        .write = tty_write,
        .ioctl_size = tty_ioctl_size,
        .ioctl = tty_ioctl,
    },
};
/* struct dev_ops ptmx_dev = {.open = ptmx_open}; */
