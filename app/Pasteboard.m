#include <string.h>
#import <UIKit/UIKit.h>
#include "fs/poll.h"
#include "fs/dyndev.h"
#include "kernel/errno.h"
#include "debug.h"

/**
 * wbuffer is dynamically sized buffer of size wbuffer_size
 * All writes go to it, and wbuffer_len is length of data held in buffer
 *
 * wbuffer can be "flushed" to UIPasteboard. When that happens, all data
 * in wbuffer is copied to it, and wbuffer_off is adjusted.
 * 
 * wbuffer_off is the length of data currently in UIPasteboard (starting
 * offset for wbuffer)
 */

// Prepare for fd separation
#define fd_priv(fd) fd->clipboard
typedef struct fd clipboard_fd;

#define INITIAL_WBUFFER_SIZE 1024
#define MAX_WBUFFER_SIZE INITIAL_WBUFFER_SIZE*8

// If pasteboard contents were changed since file was opened,
// all read operations on it return error
static int check_read_generation(clipboard_fd *fd) {
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

// wbuffer => UIPasteboard
static int clipboard_wsync(clipboard_fd *fd) {
    if (fd_priv(fd).wbuffer == NULL) {
        return 0;
    }

    void *data = fd_priv(fd).wbuffer;
    size_t len = fd_priv(fd).wbuffer_len;

    fd_priv(fd).wbuffer = NULL;
    fd_priv(fd).wbuffer_len = 0;
    fd_priv(fd).wbuffer_size = 0;

    NSString *wbuffer_str = [[NSString alloc]
                             initWithBytesNoCopy:data
                             length:len
                             encoding:NSUTF8StringEncoding
                             freeWhenDone:YES];

    if (fd_priv(fd).wbuffer_off != 0) {
        NSString *current = UIPasteboard.generalPasteboard.string;

        int err = check_read_generation(fd);
        if (err != 0) {
            return err;
        }

        if (fd_priv(fd).wbuffer_off != current.length) {
            current = [current substringToIndex:fd_priv(fd).wbuffer_off];
        }

        wbuffer_str = [current stringByAppendingString:wbuffer_str];
    }

    // Update wbuffer_off with newly written string
    fd_priv(fd).wbuffer_off = [wbuffer_str length];

    // FIXME(stek29): This logs "Returning local object of class NSString"
    // and I have no idea why (or how to fix it)
    UIPasteboard.generalPasteboard.string = wbuffer_str;

    // Reset generation since we've just updated UIPasteboard
    // note: offset doesn't change
    fd_priv(fd).generation = 0;

    return 0;
}

static const char *get_data_offseted(clipboard_fd *fd, size_t *len) {
    size_t clen = 0;
    const char *data = NULL;
    size_t off = 0;

    // If wbuffer isn't empty, but offset is pre wbuffer_off, flush it
    // and return data from UIPasteboard
    if (fd_priv(fd).wbuffer != NULL && fd->offset >= fd_priv(fd).wbuffer_off) {
        off = fd->offset - fd_priv(fd).wbuffer_off;
        clen = fd_priv(fd).wbuffer_len;
        data = fd_priv(fd).wbuffer;
    } else {
        if (fd_priv(fd).wbuffer != NULL) {
            clipboard_wsync(fd);
            assert(fd_priv(fd).wbuffer == NULL);
        }

        if (check_read_generation(fd) != 0) {
            return NULL;
        }

        NSString *contents = UIPasteboard.generalPasteboard.string;
        off = fd->offset;
        clen = contents.length;
        data = contents.UTF8String;
    }

    if (off < clen) {
        *len = clen - off;
    } else {
        *len = 0;
    }
    return data + off;
}

static int clipboard_append_init(clipboard_fd *fd) {
    assert(fd_priv(fd).wbuffer == NULL);
    size_t len;
    if (get_data_offseted(fd, &len) == NULL) {
        return _EIO;
    }
    fd->offset = fd_priv(fd).wbuffer_off = len;
    return 0;
}

static int clipboard_poll(clipboard_fd *fd) {
    int rv = POLL_WRITE;

    size_t len = 0;
    if (get_data_offseted(fd, &len) != NULL && len > 0) {
        rv |= POLL_READ;
    }

    return rv;
}

static ssize_t clipboard_read(clipboard_fd *fd, void *buf, size_t bufsize) {
    size_t length = 0;
    const char *data = get_data_offseted(fd, &length);
    if (data == NULL) {
        return _EIO;
    }
    size_t n = bufsize;
    if (n > length)
        n = length;

    if (n != 0) {
        memcpy(buf, data, n);
        fd->offset += n;
    }

    return n;
}

static ssize_t clipboard_write(clipboard_fd *fd, const void *buf, size_t bufsize) {
    // FIXME(stek29): should adjust wbuffer instead of just failing
    if (fd->offset < fd_priv(fd).wbuffer_off) {
        return _EIO;
    }

    size_t off = fd->offset - fd_priv(fd).wbuffer_off;
    size_t new_len = off + bufsize;
    size_t old_len = fd_priv(fd).wbuffer_len;
    int force_wsync = 0;

    if (new_len > MAX_WBUFFER_SIZE) {
        size_t diff = new_len - MAX_WBUFFER_SIZE;
        new_len = MAX_WBUFFER_SIZE;
        bufsize -= diff;
        force_wsync = 1;
    }

    // (Re)allocate wbuffer if there's not enough space to fit new_len
    if (fd_priv(fd).wbuffer_size < new_len) {
        size_t new_size = fd_priv(fd).wbuffer_size;
        // handle emtpy buffer
        if (new_size == 0) {
            new_size = INITIAL_WBUFFER_SIZE;
        }
        // keep growing until it fits
        while (new_size < new_len) new_size *= 2;
        void *new_buf = realloc(fd_priv(fd).wbuffer, new_size);
        if (new_buf == NULL) {
            return _ENOMEM;
        }
        fd_priv(fd).wbuffer = new_buf;
        fd_priv(fd).wbuffer_size = new_size;
    }

    // fill the hole between new offset and old len
    if (old_len < off) {
        memset(fd_priv(fd).wbuffer + old_len, '\0', off - old_len);
    }

    memcpy(fd_priv(fd).wbuffer + off, buf, bufsize);
    fd->offset += bufsize;
    fd_priv(fd).wbuffer_len = new_len;

    if (force_wsync) {
        clipboard_wsync(fd);
    }

    return bufsize;
}

static off_t_ clipboard_lseek(clipboard_fd *fd, off_t_ off, int whence) {
    off_t_ old_off = fd->offset;
    size_t length = 0;

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

static int clipboard_close(clipboard_fd *fd) {
    clipboard_wsync(fd);
    assert(fd_priv(fd).wbuffer == NULL);
    return 0;
}

static int clipboard_open(int major, int minor, struct fd *fd) {
    // Zero fd_priv data
    memset(&fd_priv(fd), 0, sizeof(fd_priv(fd)));

    // If O_APPEND_ is set, initialize wbuffer with current pasteboard contents
    if (fd->flags & O_APPEND_) {
        int err = clipboard_append_init(fd);
        if (err != 0) {
            return err;
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
    .fd.fsync = clipboard_wsync,
};
