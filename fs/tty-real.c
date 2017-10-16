#include "debug.h"
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>

#include "kernel/calls.h"
#include "fs/tty.h"

static void real_tty_read_thread(struct tty *tty) {
    char ch;
    while (read(STDIN_FILENO, &ch, 1) == 1) {
        if (ch == '\x1c') {
            // ^\ (so ^C still works for emulated SIGINT)
            raise(SIGINT);
        }
        tty_input(tty, &ch, 1);
    }
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
int real_tty_open(struct tty *tty) {
    struct winsize winsz;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &winsz) < 0)
        return err_map(errno);
    tty->winsize.col = winsz.ws_col;
    tty->winsize.row = winsz.ws_row;
    tty->winsize.xpixel = winsz.ws_xpixel;
    tty->winsize.ypixel = winsz.ws_ypixel;

    struct termios termios;
    if (tcgetattr(STDIN_FILENO, &termios) < 0)
        return err_map(errno);
    tty->termios = termios_from_real(termios);

    old_termios = termios;
    cfmakeraw(&termios);
#ifdef NO_CRLF
    termios.c_oflag |= OPOST | ONLCR;
#endif
    if (tcsetattr(STDIN_FILENO, TCSANOW, &termios) < 0)
        DIE("failed to set terminal to raw mode");

    if (pthread_create(&tty->thread, NULL, (void *(*)(void *)) real_tty_read_thread, tty) < 0)
        // ok if this actually happened it would be weird AF
        return _EIO;
    pthread_detach(tty->thread);
    return 0;
}

ssize_t real_tty_write(struct tty *tty, const void *buf, size_t len) {
    return write(STDOUT_FILENO, buf, len);
}

void real_tty_close(struct tty *tty) {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &old_termios) < 0)
        DIE("failed to reset terminal");
    pthread_cancel(tty->thread);
}

struct tty_driver real_tty_driver = {
    .open = real_tty_open,
    .write = real_tty_write,
    .close = real_tty_close,
};
