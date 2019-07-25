#include <string.h>
#import <UIKit/UIKit.h>
#include "fs/poll.h"
#include "fs/dyndev.h"
#include "kernel/errno.h"
#include "debug.h"

// Prepare for fd separation
#define fd_priv(fd) fd->clipboard

#define INITIAL_WBUFFER_SIZE 1024

// If pasteboard contents were changed since file was opened,
// all read operations on in return error
static int check_read_generation(struct fd *fd) {
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

static const char *get_data(struct fd *fd, size_t *len) {
    if (fd_priv(fd).wbuffer != NULL) {
        *len = fd_priv(fd).wbuffer_len;
        return fd_priv(fd).wbuffer;
    }

    if (check_read_generation(fd) != 0) {
        return NULL;
    }

    NSString *contents = UIPasteboard.generalPasteboard.string;
    *len = contents.length;
    return contents.UTF8String;
}

// wbuffer => UIPasteboard
static int clipboard_wsync(struct fd *fd) {
    if (fd_priv(fd).wbuffer == NULL) {
        return 0;
    }

    void *data = fd_priv(fd).wbuffer;
    size_t len = fd_priv(fd).wbuffer_len;

    fd_priv(fd).wbuffer = NULL;
    fd_priv(fd).wbuffer_size = 0;
    fd_priv(fd).wbuffer_len = 0;

    // FIXME(stek29): This logs "Returning local object of class NSString"
    // and I have no idea why (or how to fix it)
    UIPasteboard.generalPasteboard.string = [[NSString alloc]
                                             initWithBytesNoCopy:data
                                             length:len
                                             encoding:NSUTF8StringEncoding
                                             freeWhenDone:YES];

    // Reset generation since we've just updated UIPasteboard
    // note: offset doesn't change
    fd_priv(fd).generation = 0;

    return 0;
}

// UIPasteboard => wbuffer, return len
static ssize_t clipboard_rsync(struct fd *fd) {
    if (fd_priv(fd).wbuffer != NULL) {
        free(fd_priv(fd).wbuffer);
        fd_priv(fd).wbuffer = NULL;
        fd_priv(fd).wbuffer_size = 0;
        fd_priv(fd).wbuffer_len = 0;
    }

    size_t len;
    const void *data = get_data(fd, &len);
    size_t size = INITIAL_WBUFFER_SIZE;

    // Make sure size is still INITIAL_WBUFFER_SIZE based
    while (size < len) size *= 2;

    void *wbuffer = malloc(size);
    if (wbuffer == NULL) {
        return _ENOMEM;
    }
    memcpy(wbuffer, data, len);

    fd_priv(fd).wbuffer = wbuffer;
    fd_priv(fd).wbuffer_len = len;
    fd_priv(fd).wbuffer_size = size;

    return len;
}

static int clipboard_poll(struct fd *fd) {
    int rv = POLL_WRITE;

    size_t len = 0;
    if (get_data(fd, &len) != NULL && fd->offset < len) {
        rv |= POLL_READ;
    }

    return rv;
}

static ssize_t clipboard_read(struct fd *fd, void *buf, size_t bufsize) {
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

static ssize_t clipboard_write(struct fd *fd, const void *buf, size_t bufsize) {
    size_t new_len = fd->offset + bufsize;
    size_t old_len = fd_priv(fd).wbuffer_len;

    // (Re)allocate wbuffer if there's not enough space to fit new_len
    if (new_len > fd_priv(fd).wbuffer_size) {
        size_t new_size = fd_priv(fd).wbuffer_size * 2;
        if (new_size == 0) {
            new_size = INITIAL_WBUFFER_SIZE;
        }
        void *new_buf = realloc(fd_priv(fd).wbuffer, new_size);
        if (new_buf == NULL) {
            return _ENOMEM;
        }
        fd_priv(fd).wbuffer = new_buf;
        fd_priv(fd).wbuffer_size = new_size;
    }

    // fill the hole between new offset and old len
    if (old_len < fd->offset) {
        memset(fd_priv(fd).wbuffer + old_len, '\0', fd->offset - old_len);
    }

    memcpy(fd_priv(fd).wbuffer + fd->offset, buf, bufsize);
    fd_priv(fd).wbuffer_len = new_len;

    return bufsize;
}

static off_t_ clipboard_lseek(struct fd *fd, off_t_ off, int whence) {
    off_t_ old_off = fd->offset;
    size_t length = 0;

    if (whence != LSEEK_SET || off != 0) {
        if (get_data(fd, &length) == NULL) {
            return _EIO;
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

static int clipboard_close(struct fd *fd) {
    clipboard_wsync(fd);
    if (fd_priv(fd).wbuffer != NULL) {
        free(fd_priv(fd).wbuffer);
    }
    return 0;
}

static int clipboard_open(int major, int minor, struct fd *fd) {
    // Zero fd_priv data
    memset(&fd_priv(fd), 0, sizeof(fd_priv(fd)));

    // If O_APPEND_ is set, initialize wbuffer with current pasteboard contents
    if (fd->flags & O_APPEND_) {
        ssize_t len = clipboard_rsync(fd);
        if (len < 0) {
            return (int) len;
        }
        fd->offset = (size_t)len;
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
    .fd.fsync = clipboard_wsync,
};
