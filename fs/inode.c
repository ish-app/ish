#include <stdlib.h>
#include "util/list.h"
#include "kernel/fs.h"
#include "fs/inode.h"

static lock_t inodes_lock = LOCK_INITIALIZER;
#define INODES_HASH_SIZE (1 << 10)
static struct list inodes_hash[INODES_HASH_SIZE];

struct inode_data *inode_get(struct mount *mount, ino_t ino) {
    lock(&inodes_lock);
    int index = ino % INODES_HASH_SIZE;
    if (list_null(&inodes_hash[index]))
        list_init(&inodes_hash[index]);
    struct inode_data *inode;
    list_for_each_entry(&inodes_hash[index], inode, chain) {
        if (inode->mount == mount && inode->number == ino)
            goto out;
    }

    inode = malloc(sizeof(struct inode_data));
    inode->refcount = 0;
    inode->number = ino;
    mount_retain(mount);
    inode->mount = mount;
    list_init(&inode->chain);
    lock_init(&inode->lock);

out:
    lock(&inode->lock);
    inode->refcount++;
    unlock(&inode->lock);
    unlock(&inodes_lock);
    return inode;
}

void inode_retain(struct inode_data *inode) {
    lock(&inode->lock);
    inode->refcount++;
    unlock(&inode->lock);
}

void inode_release(struct inode_data *inode) {
    lock(&inodes_lock);
    lock(&inode->lock);
    if (--inode->refcount == 0) {
        unlock(&inode->lock);
        list_remove(&inode->chain);
        mount_release(inode->mount);
        free(inode);
    } else {
        unlock(&inode->lock);
    }
    unlock(&inodes_lock);
}
