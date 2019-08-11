#ifndef TTY_H
#define TTY_H

#include "kernel/fs.h"
#include "fs/dev.h"

struct winsize_ {
    word_t row;
    word_t col;
    word_t xpixel;
    word_t ypixel;
};

// This is the definition of __kernel_termios from glibc
struct termios_ {
    dword_t iflags;
    dword_t oflags;
    dword_t cflags;
    dword_t lflags;
    byte_t line;
    byte_t cc[19];
};

#define VINTR_ 0
#define VQUIT_ 1
#define VERASE_ 2
#define VKILL_ 3
#define VEOF_ 4
#define VTIME_ 5
#define VMIN_ 6
#define VSWTC_ 7
#define VSTART_ 8
#define VSTOP_ 9
#define VSUSP_ 10
#define VEOL_ 11
#define VREPRINT_ 12
#define VDISCARD_ 13
#define VWERASE_ 14
#define VLNEXT_ 15
#define VEOL2_ 16

#define ISIG_ (1 << 0)
#define ICANON_ (1 << 1)
#define ECHO_ (1 << 3)
#define ECHOE_ (1 << 4)
#define ECHOK_ (1 << 5)
#define ECHOKE_ (1 << 6)
#define NOFLSH_ (1 << 7)
#define ECHOCTL_ (1 << 9)
#define IEXTEN_ (1 << 15)

#define INLCR_ (1 << 6)
#define IGNCR_ (1 << 7)
#define ICRNL_ (1 << 8)
#define IXON_ (1 << 10)

#define OPOST_ (1 << 0)
#define ONLCR_ (1 << 2)
#define OCRNL_ (1 << 3)
#define ONOCR_ (1 << 4)
#define ONLRET_ (1 << 5)

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
#define TIOCPKT_ 0x5420
#define TIOCGPTN_ 0x80045430
#define TIOCSPTLCK_ 0x40045431
#define TIOCGPKT_ 0x80045438

#define TCIFLUSH_ 0
#define TCOFLUSH_ 1
#define TCIOFLUSH_ 2

struct tty_driver {
    const struct tty_driver_ops *ops;
    struct tty **ttys;
    unsigned limit;
};

#define DEFINE_TTY_DRIVER(name, driver_ops, size) \
    static struct tty *name##_ttys[size]; \
    struct tty_driver name = {.ops = driver_ops, .ttys = name##_ttys, .limit = size}

struct tty_driver_ops {
    int (*init)(struct tty *tty);
    int (*open)(struct tty *tty);
    int (*write)(struct tty *tty, const void *buf, size_t len, bool blocking);
    int (*ioctl)(struct tty *tty, int cmd, void *arg);
    void (*cleanup)(struct tty *tty);
};

// indexed by major number
extern struct tty_driver *tty_drivers[256];
extern struct tty_driver real_tty_driver;

struct tty {
    unsigned refcount;
    struct tty_driver *driver;
    bool hung_up;
    bool ever_opened;

#define TTY_BUF_SIZE 4096
    char buf[TTY_BUF_SIZE];
    // A flag is a marker indicating the end of a canonical mode input. Flags
    // are created by EOL and EOF characters. You can't backspace past a flag.
    bool buf_flag[TTY_BUF_SIZE];
    size_t bufsize;
    uint8_t packet_flags;
    cond_t produced;
    cond_t consumed;

    struct winsize_ winsize;
    struct termios_ termios;
    int type;
    int num;

    pid_t_ session;
    pid_t_ fg_group;

    struct list fds;
    // only locks fds, to keep the lock order
    lock_t fds_lock;

    // this never nests with itself, except in pty_is_half_closed_master
    lock_t lock;

    union {
        pthread_t thread; // for real tty driver
        struct {
            struct tty *other;
            mode_t_ perms;
            uid_t_ uid;
            uid_t_ gid;
            bool locked;
            bool packet_mode;
        } pty;
        void *data;
    };
};

// if blocking, may return _EINTR, otherwise, may return _EAGAIN
int tty_input(struct tty *tty, const char *input, size_t len, bool blocking);
void tty_set_winsize(struct tty *tty, struct winsize_ winsize);
void tty_hangup(struct tty *tty);

// public for the benefit of ptys
struct tty *tty_get(struct tty_driver *driver, int type, int num);
struct tty *tty_alloc(struct tty_driver *driver, int type, int num);
extern lock_t ttys_lock;
void tty_release(struct tty *tty); // must be called with ttys_lock

extern struct dev_ops tty_dev;
extern struct dev_ops ptmx_dev;

int ptmx_open(struct fd *fd);

#endif
