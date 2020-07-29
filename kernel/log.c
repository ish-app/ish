#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/uio.h>
#if LOG_HANDLER_NSLOG
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "kernel/calls.h"
#include "util/sync.h"
#include "util/fifo.h"
#include "kernel/task.h"
#include "misc.h"

#define LOG_BUF_SHIFT 20
static char log_buffer[1 << LOG_BUF_SHIFT];
static struct fifo log_buf = FIFO_INIT(log_buffer);
static size_t log_max_since_clear = 0;
static lock_t log_lock = LOCK_INITIALIZER;

#define SYSLOG_ACTION_CLOSE_ 0
#define SYSLOG_ACTION_OPEN_ 1
#define SYSLOG_ACTION_READ_ 2
#define SYSLOG_ACTION_READ_ALL_ 3
#define SYSLOG_ACTION_READ_CLEAR_ 4
#define SYSLOG_ACTION_CLEAR_ 5
#define SYSLOG_ACTION_CONSOLE_OFF_ 6
#define SYSLOG_ACTION_CONSOLE_ON_ 7
#define SYSLOG_ACTION_CONSOLE_LEVEL_ 8
#define SYSLOG_ACTION_SIZE_UNREAD_ 9
#define SYSLOG_ACTION_SIZE_BUFFER_ 10

static int syslog_read(addr_t buf_addr, int_t len, int flags) {
    if (len < 0)
        return _EINVAL;
    if (flags & FIFO_LAST) {
        if ((size_t) len > log_max_since_clear)
            len = log_max_since_clear;
    } else {
        if ((size_t) len > fifo_capacity(&log_buf))
            len = fifo_capacity(&log_buf);
    }
    char *buf = malloc(len);
    fifo_read(&log_buf, buf, len, flags);
    int fail = user_write(buf_addr, buf, len);
    free(buf);
    if (fail)
        return _EFAULT;
    return len;
}

static int do_syslog(int type, addr_t buf_addr, int_t len) {
    int res;
    switch (type) {
        case SYSLOG_ACTION_READ_:
            return syslog_read(buf_addr, len, 0);
        case SYSLOG_ACTION_READ_ALL_:
            return syslog_read(buf_addr, len, FIFO_LAST | FIFO_PEEK);

        case SYSLOG_ACTION_READ_CLEAR_:
            res = syslog_read(buf_addr, len, FIFO_LAST | FIFO_PEEK);
            if (res < 0)
                return res;
            fallthrough;
        case SYSLOG_ACTION_CLEAR_:
            log_max_since_clear = 0;
            return 0;

        case SYSLOG_ACTION_SIZE_UNREAD_:
            return fifo_size(&log_buf);
        case SYSLOG_ACTION_SIZE_BUFFER_:
            return fifo_capacity(&log_buf);

        case SYSLOG_ACTION_CLOSE_:
        case SYSLOG_ACTION_OPEN_:
        case SYSLOG_ACTION_CONSOLE_OFF_:
        case SYSLOG_ACTION_CONSOLE_ON_:
        case SYSLOG_ACTION_CONSOLE_LEVEL_:
            return 0;
        default:
            return _EINVAL;
    }
}
int_t sys_syslog(int_t type, addr_t buf_addr, int_t len) {
    lock(&log_lock);
    int retval = do_syslog(type, buf_addr, len);
    unlock(&log_lock);
    return retval;
}

static void log_buf_append(const char *msg) {
    fifo_write(&log_buf, msg, strlen(msg), FIFO_OVERWRITE);
    log_max_since_clear += strlen(msg);
    if (log_max_since_clear > fifo_capacity(&log_buf))
        log_max_since_clear = fifo_capacity(&log_buf);
}
static void log_line(const char *line);
static void output_line(const char *line) {
    // send it to stdout or wherever
    log_line(line);
    // add it to the circular buffer
    log_buf_append(line);
    log_buf_append("\n");
}

void ish_vprintk(const char *msg, va_list args) {
    // format the message
    // I'm trusting you to not pass an absurdly long message
    static __thread char buf[16384] = "";
    static __thread size_t buf_size = 0;
    buf_size += vsprintf(buf + buf_size, msg, args);

    // output up to the last newline, leave the rest in the buffer
    lock(&log_lock);
    char *b = buf;
    char *p;
    while ((p = strchr(b, '\n')) != NULL) {
        *p = '\0';
        output_line(b);
        *p = '\n';
        buf_size -= p + 1 - b;
        b = p + 1;
    }
    unlock(&log_lock);
    memmove(buf, b, strlen(b) + 1);
}
void ish_printk(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    ish_vprintk(msg, args);
    va_end(args);
}

#if LOG_HANDLER_DPRINTF
#define NEWLINE "\r\n"
static void log_line(const char *line) {
    struct iovec output[2] = {{(void *) line, strlen(line)}, {"\n", 1}};
    writev(666, output, 2);
}
#elif LOG_HANDLER_NSLOG
static void log_line(const char *line) {
    extern void NSLog(CFStringRef msg, ...);
    NSLog(CFSTR("%s"), line);
}
#endif

static void default_die_handler(const char *msg) {
    printk("%s\n", msg);
}
void (*die_handler)(const char *msg) = default_die_handler;
_Noreturn void die(const char *msg, ...);
void die(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    char buf[4096];
    vsprintf(buf, msg, args);
    die_handler(buf);
    abort();
    va_end(args);
}

// fun little utility function
int current_pid() {
    if (current)
        return current->pid;
    return -1;
}
