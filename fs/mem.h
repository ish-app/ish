#ifndef FS_NULL_H
#define FS_NULL_H

#include "kernel/fs.h"
#include "fs/dev.h"

extern struct dev_ops
    mem_dev,
    null_dev,
    zero_dev,
    full_dev,
    random_dev,
    iac_dev;

#endif
