#include <sys/stat.h>
#include <string.h>

#include "kernel/calls.h"
#include "kernel/errno.h"
#include "kernel/fs.h"
#include "fs/fd.h"

static unsigned long fd_telldir(struct fd *fd) {
    unsigned long off = fd->offset;
    if (fd->ops->telldir)
        off = fd->ops->telldir(fd);
    return off;
}

static void fd_seekdir(struct fd *fd, unsigned long off) {
    fd->offset = off;
    if (fd->ops->seekdir)
        fd->ops->seekdir(fd, off);
}

struct linux_dirent_ {
    dword_t inode;
    dword_t offset;
    word_t reclen;
    char name[];
} __attribute__((packed));

struct linux_dirent64_ {
    qword_t inode;
    qword_t offset;
    word_t reclen;
    byte_t type;
    char name[];
} __attribute__((packed));

size_t fill_dirent_32(void *dirent_data, ino_t inode, off_t_ offset, const char *name, int type) {
    struct linux_dirent_ *dirent = dirent_data;
    dirent->inode = inode;
    dirent->offset = offset;
    dirent->reclen = offsetof(struct linux_dirent_, name) +
        strlen(name) + 2; // name, null terminator, type
    strcpy(dirent->name, name);
    *((char *) dirent + dirent->reclen - 1) = type;
    return dirent->reclen;
}

size_t fill_dirent_64(void *dirent_data, ino_t inode, off_t_ offset, const char *name, int type) {
    struct linux_dirent64_ *dirent = dirent_data;
    dirent->inode = inode;
    dirent->offset = offset;
    dirent->reclen = offsetof(struct linux_dirent64_, name) +
        strlen(name) + 1; // name, null terminator
    dirent->type = type;
    strcpy(dirent->name, name);
    return dirent->reclen;
}

int_t sys_getdents_common(fd_t f, addr_t dirents, dword_t count,
        size_t (*fill_dirent)(void *, ino_t, off_t_, const char *, int)) {
    STRACE("getdents(%d, %#x, %#x)", f, dirents, count);
    struct fd *fd = f_get(f);
    if (fd == NULL)
        return _EBADF;
    if (!S_ISDIR(fd->type) || fd->ops->readdir == NULL)
        return _ENOTDIR;

    dword_t orig_count = count;

    long ptr;
    int err;
    int printed = 0;
    while (true) {
        ptr = fd_telldir(fd);
        struct dir_entry entry;
        err = fd->ops->readdir(fd, &entry);
        if (err < 0)
            return err;
        if (err == 0)
            break;

        size_t max_reclen = sizeof(struct linux_dirent64_) + strlen(entry.name) + 4;
        char dirent_data[max_reclen];
        ino_t inode = entry.inode;
        off_t_ offset = fd_telldir(fd);
        const char *name = entry.name;
        int type = 0;
        size_t reclen = fill_dirent(dirent_data, inode, offset, name, type);
        if (printed < 20) {
            STRACE(" {inode=%d, offset=%d, name=%s, type=%d, reclen=%d}",
                    inode, offset, name, type, reclen);
            printed++;
        }

        if (reclen > count)
            break;
        if (user_write(dirents, dirent_data, reclen))
            return _EFAULT;
        dirents += reclen;
        count -= reclen;
    }

    fd_seekdir(fd, ptr);
    return orig_count - count;
}

int_t sys_getdents(fd_t f, addr_t dirents, uint_t count) {
    return sys_getdents_common(f, dirents, count, fill_dirent_32);
}

int_t sys_getdents64(fd_t f, addr_t dirents, uint_t count) {
    return sys_getdents_common(f, dirents, count, fill_dirent_64);
}

