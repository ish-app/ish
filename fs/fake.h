#ifndef ISH_INTERNAL
#error "for internal use only"
#endif

#ifndef FS_FAKE_H
#define FS_FAKE_H

#include "kernel/fs.h"
#include "fs/fake-db.h"
#include "misc.h"

struct fd *fakefs_open_inode(struct mount *mount, ino_t inode);

#endif
