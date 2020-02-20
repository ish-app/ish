#include <string.h>
#import <UIKit/UIKit.h>
#include "fs/poll.h"
#include "fs/dyndev.h"
#include "kernel/errno.h"
#include "debug.h"

/**
 * buffer is dynamically sized buffer of size buffer_cap
 * All writes go to it, and buffer_len is length of data held in buffer
 */

// Prepare for fd separation
#define fd_priv(fd) fd->clipboard
typedef struct fd clip_fd;

#define INITIAL_BUFFER_CAP 1024
// 8MB: https://stackoverflow.com/a/3523175
#define MAXIMAL_BUFFER_CAP 8*1024*1024

// If pasteboard contents were changed since file was opened,
// all read operations on in return error
static int check_read_generation(clip_fd *fd) {
    UIPasteboard *pb = UIPasteboard.generalPasteboard;

    uint64_t pb_gen = (uint64_t) pb.changeCount;
    uint64_t fd_gen = fd_priv(fd).generation;

    if (fd_gen == 0 || fd->offset == 0) {
        fd_priv(fd).generation = pb_gen;
    } else if (fd_gen != pb_gen) {
        return -1;
    }

    return 0;
}

static const char *get_data(clip_fd *fd, size_t *len) {
    if (fd_priv(fd).buffer != NULL) {
        *len = fd_priv(fd).buffer_len;
        return fd_priv(fd).buffer;
    }

    if (check_read_generation(fd) != 0) {
        return NULL;
    }

    NSString __autoreleasing *contents = UIPasteboard.generalPasteboard.string;
    *len = contents.length;
    return contents.UTF8String;
}

static int realloc_to_fit(clip_fd* fd, size_t fit_len) {
    // (Re)allocate buffer if there's not enough space to fit fit_len
    if (fit_len <= fd_priv(fd).buffer_cap) {
        return 0;
    }
    if (fit_len > MAXIMAL_BUFFER_CAP) {
        return 1;
    }

    size_t size = fd_priv(fd).buffer_cap * 2;
    if (size == 0) {
        size = INITIAL_BUFFER_CAP;
    }
    while (size < fit_len) size *= 2;

    void *new_buf = realloc(fd_priv(fd).buffer, size);
    if (new_buf == NULL) {
        return 1;
    }

    fd_priv(fd).buffer = new_buf;
    fd_priv(fd).buffer_cap = size;

    return 0;
}

// buffer => UIPasteboard
static int clipboard_write_sync(clip_fd *fd) {
    if (fd_priv(fd).buffer == NULL) {
        return 0;
    }

    void *data = fd_priv(fd).buffer;
    size_t len = fd_priv(fd).buffer_len;

    // FIXME(stek29): This logs "Returning local object of class NSString"
    // and I have no idea why (or how to fix it)
    UIPasteboard.generalPasteboard.string = [[NSString alloc]
                                             initWithBytes:data
                                             length:len
                                             encoding:NSUTF8StringEncoding];

    // Reset generation since we've just updated UIPasteboard
    // note: offset doesn't change
    fd_priv(fd).generation = 0;

    return 0;
}

// UIPasteboard => buffer, return len
static ssize_t clipboard_read_sync(clip_fd *fd) {
    if (fd_priv(fd).buffer != NULL) {
        free(fd_priv(fd).buffer);
        fd_priv(fd).buffer = NULL;
        fd_priv(fd).buffer_cap = 0;
        fd_priv(fd).buffer_len = 0;
    }

    @autoreleasepool {
        size_t len;
        const void *data = get_data(fd, &len);

        // Make sure size is still INITIAL_BUFFER_CAP based
        if (realloc_to_fit(fd, len)) {
            return _ENOMEM;
        }

        memcpy(fd_priv(fd).buffer, data, len);
        fd_priv(fd).buffer_len = len;

        return len;
    }
}

static int clipboard_poll(clip_fd *fd) {
    return POLL_READ | POLL_WRITE;
}

static ssize_t clipboard_read(clip_fd *fd, void *buf, size_t bufsize) {
    @autoreleasepool {
        size_t length = 0;
        const char *data = get_data(fd, &length);

        if (data == NULL) {
            return _EIO;
        }

        size_t remaining = length - fd->offset;
        if ((size_t) fd->offset > length)
            remaining = 0;

        size_t n = bufsize;
        if (n > remaining)
            n = remaining;

        if (n != 0) {
            memcpy(buf, data + fd->offset, n);
            fd->offset += n;
        }

        return n;
    }
}

static ssize_t clipboard_write(clip_fd *fd, const void *buf, size_t bufsize) {
    size_t new_len = fd->offset + bufsize;
    size_t old_len = fd_priv(fd).buffer_len;

    if (old_len > new_len) {
        new_len = old_len;
    }

    if (realloc_to_fit(fd, new_len)) {
        return _ENOMEM;
    }

    // fill the hole between new offset and old len
    if (old_len < fd->offset) {
        memset(fd_priv(fd).buffer + old_len, '\0', fd->offset - old_len);
    }

    memcpy(fd_priv(fd).buffer + fd->offset, buf, bufsize);
    fd->offset += bufsize;
    fd_priv(fd).buffer_len = new_len;

    return bufsize;
}

static off_t_ clipboard_lseek(clip_fd *fd, off_t_ off, int whence) {
    off_t_ old_off = fd->offset;
    size_t length = 0;

    if (whence != LSEEK_SET || off != 0) {
        @autoreleasepool {
            if (get_data(fd, &length) == NULL) {
                return _EIO;
            }
        }
    }

    switch (whence) {
    case LSEEK_SET:
        fd->offset = off;
        break;

    case LSEEK_CUR:
        fd->offset += off;
        break;

    case LSEEK_END:
        fd->offset = length + off;
        break;

    default:
        return _EINVAL;
    }

    if (fd->offset < 0) {
        fd->offset = old_off;
        return _EINVAL;
    }

    return fd->offset;
}

static int clipboard_close(clip_fd *fd) {
    clipboard_write_sync(fd);
    if (fd_priv(fd).buffer != NULL) {
        free(fd_priv(fd).buffer);
    }
    return 0;
}

static int clipboard_open(int major, int minor, clip_fd *fd) {
    // Zero fd_priv data
    memset(&fd_priv(fd), 0, sizeof(fd_priv(fd)));

    // If O_TRUNC is not set, initialize buffer with current pasteboard contents
    if (!(fd->flags & O_TRUNC_)) {
        ssize_t len = clipboard_read_sync(fd);
        if (len < 0) {
            return (int) len;
        }
        if (fd->flags & O_APPEND_) {
            fd->offset = (size_t) len;
        }
    }

    return 0;
}

struct dev_ops clipboard_dev = {
    .open = clipboard_open,
    .fd.read = clipboard_read,
    .fd.write = clipboard_write,
    .fd.lseek = clipboard_lseek,
    .fd.poll = clipboard_poll,
    .fd.close = clipboard_close,
    .fd.fsync = clipboard_write_sync,
};
