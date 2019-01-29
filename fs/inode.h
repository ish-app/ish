#ifndef FS_INODE_H
#define FS_INODE_H
#include <sys/types.h>
#include "util/sync.h"
struct mount;

struct inode_data {
    unsigned refcount;
    ino_t number;
    struct mount *mount;
    struct list chain;
    // useful stuff will go here blah blah blah blah blah
    // fcntl lock management
    lock_t lock;
};

struct inode_data *inode_get(struct mount *mount, ino_t inode);
void inode_retain(struct inode_data *inode);
void inode_release(struct inode_data *inode);

#endif
