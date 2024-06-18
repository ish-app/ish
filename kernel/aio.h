#ifndef KERNEL_AIO_H
#define KERNEL_AIO_H

#include "fs/fd.h"
#include "fs/aio.h"

// Synchronous fallback for non-async files.
// 
// This is equivalent to the `io_submit` field on `fd_ops`, but is intended to
// be called if that field is NULL.
int aio_fallback_submit(struct fd *fd, struct aioctx *ctx, unsigned int event_id);

#endif