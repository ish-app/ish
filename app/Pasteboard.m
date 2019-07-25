#include <string.h>
#import <UIKit/UIKit.h>
#include "fs/poll.h"
#include "fs/dyndev.h"
#include "kernel/errno.h"
#include "debug.h"

#if 0
#define CLIP_DEBUG_PRINTK(...) printk
#else
#define CLIP_DEBUG_PRINTK(...)
#endif

static void inline dump_clipboard_state(const char* tag, struct fd *fd) {
    CLIP_DEBUG_PRINTK("%s offset=%d pb_gen=%d len=%lu\n", tag, fd->offset, fd->eventfd.val, UIPasteboard.generalPasteboard.string.length);
}

// Generation aka UIPasteboard changeCount is stored at fd->eventfd.val
//
// If pasteboard contents were changed since file was opened,
// all read operations on in return IO Error
static int check_read_generation(struct fd* fd, UIPasteboard *pb) {
    uint64_t pb_gen = (uint64_t)pb.changeCount;
    uint64_t fd_gen = fd->eventfd.val;

    CLIP_DEBUG_PRINTK("%s(%p): pb_gen=%d fd_gen=%d offset=%d\n", __func__, fd, pb_gen, fd_gen, fd->offset);
    // XXX: fd_gen modifications should be locked
    if (fd_gen == 0 || fd->offset == 0) {
        fd->eventfd.val = pb_gen;
    } else if (fd_gen != pb_gen) {
        return _EIO;
    }

    return 0;
}

static int clipboard_poll(struct fd *fd) {
    CLIP_DEBUG_PRINTK("%s(%p)\n", fd, __func__);
    int rv = 0;

    UIPasteboard* pb = UIPasteboard.generalPasteboard;
    if (check_read_generation(fd, pb) == 0) {
        if (fd->offset < pb.string.length) {
            rv |= POLL_READ;
        }
    }

    return rv;
}

static ssize_t clipboard_read(struct fd *fd, void *buf, size_t bufsize) {
    CLIP_DEBUG_PRINTK("%s(%p, buf, %zu)\n", __func__, fd, bufsize);
    dump_clipboard_state("read pre", fd);
    UIPasteboard* pb = UIPasteboard.generalPasteboard;

    NSString *contents = pb.string;

    int err = check_read_generation(fd, pb);
    if (err != 0) {
        return err;
    }

    size_t length = contents.length;

    size_t remaining = length - fd->offset;
    if ((size_t) fd->offset > length)
        remaining = 0;
    size_t n = bufsize;
    if (n > remaining)
        n = remaining;

    if (n != 0) {
        memcpy(buf, contents.UTF8String + fd->offset, n);
        fd->offset += n;
    }

    dump_clipboard_state("read post", fd);
    return n;
}

static off_t_ clipboard_lseek(struct fd *fd, off_t_ off, int whence) {
    CLIP_DEBUG_PRINTK("%s(%p, off=%d, whence=%d)\n", __func__, fd, off, whence);
    dump_clipboard_state("lseek pre", fd);
    off_t_ old_off = fd->offset;
    size_t length = 0;

    if (whence != LSEEK_SET || off != 0) {
        UIPasteboard *pb = UIPasteboard.generalPasteboard;
        length = pb.string.length;

        int err = check_read_generation(fd, pb);
        if (err != 0) {
            return err;
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

    dump_clipboard_state("lseek post", fd);
    return fd->offset;
}

static int clipboard_close(struct fd *fd) {
    CLIP_DEBUG_PRINTK("%s(%p)\n", __func__, fd);
    return 0;
}

static int clipboard_open(int major, int minor, struct fd *fd) {
    CLIP_DEBUG_PRINTK("%s(%p)\n", __func__, fd);
    return 0;
}

struct dev_ops clipboard_dev = {
    .open = clipboard_open,
    .fd.read = clipboard_read,
    .fd.lseek = clipboard_lseek,
    .fd.poll = clipboard_poll,
    .fd.close = clipboard_close,
};
