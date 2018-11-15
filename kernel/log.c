#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#if LOG_HANDLER_NSLOG
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "util/sync.h"

#define LOG_BUF_SHIFT 20
static char log_buf[1 << LOG_BUF_SHIFT];
static size_t log_head = 0;
static size_t log_size = 0;

static void log_line(const char *line);
static void output_line(const char *line) {
    // send it to stdout or wherever
    log_line(line);
    // add it to the circular buffer

    // while any left: copy to log_p size sizeof(log_buf)-log_head, 
    char *log_p = log_buf;
    size_t remaining = strlen(line);
    while (*line != '\0') {
        size_t len = sizeof(log_buf) - log_head;
        if (len > remaining)
            len = remaining;
        memcpy(log_p, line, len);
        log_size += len;
        if (log_size > sizeof(log_buf))
            log_size = sizeof(log_buf);
        remaining -= len;
        line += len;
    }
}

lock_t log_lock = LOCK_INITIALIZER;

void printk(const char *msg, ...) {
    va_list args;
    va_start(args, msg);

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

    va_end(args);
}

#if LOG_HANDLER_DPRINTF
#define NEWLINE "\r\n"
static void log_line(const char *line) {
    dprintf(666, "%s" NEWLINE, line);
}
#elif LOG_HANDLER_NSLOG
static void log_line(const char *line) {
    extern void NSLogv(CFStringRef msg, va_list args);
    NSLogv(CFSTR("%s"), line);
}
#endif

