#include "debug.h"
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>

#include "kernel/calls.h"
#include "fs/tty.h"
#include "fs/devices.h"

// Only /dev/tty1 will be connected, the rest will go to a black hole.
#define REAL_TTY_NUM 1

void real_tty_reset_term(void);

static void *real_tty_read_thread(void *_tty) {
    struct tty *tty = _tty;
    char ch;
    for (;;) {
        int err = read(STDIN_FILENO, &ch, 1);
        if (err != 1) {
            printk("tty read returned %d\n", err);
            if (err < 0)
                printk("error: %s\n", strerror(errno));
            continue;
        }
        if (ch == '\x1c') {
            // ^\ (so ^C still works for emulated SIGINT)
            real_tty_reset_term();
            raise(SIGINT);
        }
        tty_input(tty, &ch, 1, 0);
    }
    return NULL;
}

static struct termios_ termios_from_real(struct termios real) {
    struct termios_ fake = {};
#define FLAG(t, x) \
    if (real.c_##t##flag & x) \
        fake.t##flags |= x##_
    FLAG(o, OPOST);
    FLAG(o, ONLCR);
    FLAG(o, OCRNL);
    FLAG(o, ONOCR);
    FLAG(o, ONLRET);
    FLAG(i, INLCR);
    FLAG(i, IGNCR);
    FLAG(i, ICRNL);
    FLAG(l, ISIG);
    FLAG(l, ICANON);
    FLAG(l, ECHO);
    FLAG(l, ECHOE);
    FLAG(l, ECHOK);
    FLAG(l, NOFLSH);
    FLAG(l, ECHOCTL);
#undef FLAG

#define CC(x) \
    fake.cc[V##x##_] = real.c_cc[V##x]
    CC(INTR);
    CC(QUIT);
    CC(ERASE);
    CC(KILL);
    CC(EOF);
    CC(TIME);
    CC(MIN);
    CC(START);
    CC(STOP);
    CC(SUSP);
    CC(EOL);
    CC(REPRINT);
    CC(DISCARD);
    CC(WERASE);
    CC(LNEXT);
    CC(EOL2);
#undef CC
    return fake;
}

static struct termios old_termios;
static bool real_tty_is_open;
static int real_tty_init(struct tty *tty) {
    if (tty->num != REAL_TTY_NUM)
        return 0;

    struct winsize winsz;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &winsz) < 0) {
        if (errno == ENOTTY)
            goto notty;
        return errno_map();
    }
    tty->winsize.col = winsz.ws_col;
    tty->winsize.row = winsz.ws_row;
    tty->winsize.xpixel = winsz.ws_xpixel;
    tty->winsize.ypixel = winsz.ws_ypixel;

    struct termios termios;
    if (tcgetattr(STDIN_FILENO, &termios) < 0)
        return errno_map();
    tty->termios = termios_from_real(termios);

    old_termios = termios;
    cfmakeraw(&termios);
#ifdef NO_CRLF
    termios.c_oflag |= OPOST | ONLCR;
#endif
    if (tcsetattr(STDIN_FILENO, TCSANOW, &termios) < 0)
        ERRNO_DIE("failed to set terminal to raw mode");
notty:

    if (pthread_create(&tty->thread, NULL,  real_tty_read_thread, tty) < 0)
        // ok if this actually happened it would be weird AF
        return _EIO;
    pthread_detach(tty->thread);
    real_tty_is_open = true;
    return 0;
}

static int real_tty_write(struct tty *tty, const void *buf, size_t len, bool UNUSED(blocking)) {
    if (tty->num != REAL_TTY_NUM)
        return len;
    return write(STDOUT_FILENO, buf, len);
}

void real_tty_reset_term() {
    if (!real_tty_is_open) return;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &old_termios) < 0 && errno != ENOTTY) {
        printk("failed to reset terminal: %s\n", strerror(errno));
        abort();
    }
}

static void real_tty_cleanup(struct tty *tty) {
    if (tty->num != REAL_TTY_NUM)
        return;
    real_tty_reset_term();
    pthread_cancel(tty->thread);
}

struct tty_driver_ops real_tty_ops = {
    .init = real_tty_init,
    .write = real_tty_write,
    .cleanup = real_tty_cleanup,
};
DEFINE_TTY_DRIVER(real_tty_driver, &real_tty_ops, TTY_CONSOLE_MAJOR, 64);
