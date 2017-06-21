#include <string.h>

#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/fs.h"

struct linux_dirent64 {
    qword_t inode;
    qword_t offset;
    word_t reclen;
    byte_t type;
    char name[];
} __attribute__((packed));

int_t sys_getdents64(fd_t f, addr_t dirents, dword_t count) {
    struct fd *fd = current->files[f];
    if (fd == NULL)
        return _EBADF;

    dword_t orig_count = count;

    while (true) {
        /* debugger; */
        struct dir_entry entry;
        int err = fd->ops->readdir(fd, &entry);
        if (err < 0)
            return err;
        if (err == 1)
            break;

        dword_t reclen = offsetof(struct linux_dirent64, name) +
            strlen(entry.name) + 4; // name, null terminator, padding, file type
        char dirent_data[reclen];
        struct linux_dirent64 *dirent = (struct linux_dirent64 *) dirent_data;
        dirent->inode = entry.inode;
        dirent->offset = entry.offset;
        dirent->reclen = reclen;
        strcpy(dirent->name, entry.name);

        if (reclen > count)
            break;
        user_put_count(dirents, dirent_data, reclen);
        dirents += reclen;
        count -= reclen;
    }

    return orig_count - count;
}
