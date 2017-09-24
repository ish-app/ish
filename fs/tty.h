#ifndef TTY_H
#define TTY_H

#include "sys/fs.h"
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

#define INLCR_ (1 << 6)
#define IGNCR_ (1 << 7)
#define ICRNL_ (1 << 8)

#define OPOST_ (1 << 0)
#define ONLCR_ (1 << 2)
#define OCRNL_ (1 << 3)
#define ONOCR_ (1 << 4)
#define ONLRET_ (1 << 5)

#define TTY_VIRTUAL 0
#define TTY_PSEUDO 1

struct tty_driver {
    int (*open)(struct tty *tty);
    ssize_t (*write)(struct tty *tty, const void *buf, size_t len);
    void (*close)(struct tty *tty);
};

// TODO remove magic number
extern struct tty_driver tty_drivers[2];
extern struct tty_driver real_tty_driver;

struct tty {
    unsigned refcount;
    struct pollable pl;
    struct tty_driver *driver;

    char buf[4096];
    size_t bufsize;
    bool canon_ready;
    pthread_cond_t produced;
    pthread_cond_t consumed;

    struct winsize_ winsize;
    struct termios_ termios;
    int type;
    int num;

    dword_t session;
    dword_t fg_group;

    pthread_mutex_t lock;

    // for real tty driver
    pthread_t thread;
};

int tty_input(struct tty *tty, const char *input, size_t len);

extern struct dev_ops tty_dev;
extern struct dev_ops ptmx_dev;
extern struct fd_ops tty_master_fd;
extern struct fd_ops tty_slave_fd;

#endif
