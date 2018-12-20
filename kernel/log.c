#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#if LOG_HANDLER_NSLOG
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "kernel/calls.h"
#include "util/sync.h"
#include "kernel/task.h"
#include "misc.h"

#define LOG_BUF_SHIFT 20
static char log_buf[1 << LOG_BUF_SHIFT];
static size_t log_head = 0;
static size_t log_size = 0;
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

static int syslog_read(size_t start, addr_t buf_addr, int_t total) {
    if (total > log_size)
        total = log_size;
    size_t remaining = total;
    size_t log_p = log_head;
    char buf[total];
    char *b = buf;
    while (remaining > 0) {
        size_t len = remaining;
        if (len > sizeof(log_buf) - log_head)
            len = sizeof(log_buf) - log_head;
        memcpy(b, &log_buf[log_p], len);
        log_p = (log_p + len) % sizeof(log_buf);
        b += len;
        remaining -= len;
    }
    if (user_write(buf_addr, buf, total))
        return -1;
    return total;
}
static int syslog_read_all(addr_t buf_addr, addr_t len) {
    if (len > log_size)
        len = log_size;
    return syslog_read((log_head + log_size - len) % sizeof(log_buf), buf_addr, len);
}

static int do_syslog(int type, addr_t buf_addr, int_t len) {
    switch (type) {
        case SYSLOG_ACTION_READ_:
            len = syslog_read(log_head, buf_addr, len);
            if (len < 0)
                return _EFAULT;
            log_head = (log_head + len) % sizeof(log_buf);
            log_size -= len;
            return len;
        case SYSLOG_ACTION_READ_ALL_:
            return syslog_read_all(buf_addr, len);
        case SYSLOG_ACTION_READ_CLEAR_:
            len = syslog_read_all(buf_addr, len);
            log_head = log_size = 0;
            return len;
        case SYSLOG_ACTION_CLEAR_:
            log_head = log_size = 0;
            return 0;

        case SYSLOG_ACTION_SIZE_UNREAD_:
            return log_size;
        case SYSLOG_ACTION_SIZE_BUFFER_:
            return sizeof(log_buf);

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
    size_t log_p = (log_head + log_size) % sizeof(log_buf);
    size_t remaining = strlen(msg);
    while (*msg != '\0') {
        size_t log_tail = (log_head + log_size) % sizeof(log_buf);
        size_t len = sizeof(log_buf) - log_tail;
        if (len > remaining)
            len = remaining;
        memcpy(&log_buf[log_p], msg, len);
        log_size += len;
        if (log_size > sizeof(log_buf)) {
            log_head = (log_head + log_size - sizeof(log_buf)) % sizeof(log_buf);
            log_size = sizeof(log_buf);
        }
        log_p = (log_p + len) % sizeof(log_buf);
        msg += len;
        remaining -= len;
    }
}
static void log_line(const char *line);
static void output_line(const char *line) {
    // send it to stdout or wherever
    log_line(line);
    // add it to the circular buffer
    log_buf_append(line);
    log_buf_append("\n");
}

void vprintk(const char *msg, va_list args) {
    // format the message
    // I'm trusting you to not pass an absurdly long message
    static __thread char buf[4096] = "";
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
void printk(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vprintk(msg, args);
    va_end(args);
}

#if LOG_HANDLER_DPRINTF
#define NEWLINE "\r\n"
static void log_line(const char *line) {
    // glibc has a bug where dprintf to an invalid file descriptor leaks memory
    // https://sourceware.org/bugzilla/show_bug.cgi?id=22876
    if (fcntl(666, F_GETFL) == -1 && errno == EBADF)
        return;
    dprintf(666, "%s" NEWLINE, line);
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
    return current->pid;
}
