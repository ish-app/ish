#include "debug.h"
#include "kernel/calls.h"
#include "kernel/task.h"
#include "kernel/aio.h"
#include "kernel/fs.h"
#include "kernel/time.h"
#include "fs/aio.h"
#include "fs/fd.h"

// Guest memory offsets for the IOCB structure.
// Calculated by a test program compiled and ran in iSH itself.
struct _guest_iocb {
    uint64_t data;
    uint32_t key;
    uint32_t rw_flags;
    uint16_t lio_opcode;
    int16_t reqprio;
    uint32_t fildes;
    uint64_t buf;
    uint64_t nbytes;
    int64_t offset;
    uint64_t reserved2;
    uint32_t flags;
    uint32_t resfd;
};

static_assert(offsetof(struct _guest_iocb, data) == 0, "IOCB order");
static_assert(offsetof(struct _guest_iocb, key) == 8, "IOCB order");
static_assert(offsetof(struct _guest_iocb, rw_flags) == 12, "IOCB order");
static_assert(offsetof(struct _guest_iocb, lio_opcode) == 16, "IOCB order");
static_assert(offsetof(struct _guest_iocb, reqprio) == 18, "IOCB order");
static_assert(offsetof(struct _guest_iocb, fildes) == 20, "IOCB order");
static_assert(offsetof(struct _guest_iocb, buf) == 24, "IOCB order");
static_assert(offsetof(struct _guest_iocb, nbytes) == 32, "IOCB order");
static_assert(offsetof(struct _guest_iocb, offset) == 40, "IOCB order");
static_assert(offsetof(struct _guest_iocb, reserved2) == 48, "IOCB order");
static_assert(offsetof(struct _guest_iocb, flags) == 56, "IOCB order");
static_assert(offsetof(struct _guest_iocb, resfd) == 60, "IOCB order");

// Guest memory offsets for the IO_EVENT structure.
// Also confirmed by test program.
const size_t AIO_IO_EVENT_DATA = 0;
const size_t AIO_IO_EVENT_OBJ = 8;
const size_t AIO_IO_EVENT_RES = 16;
const size_t AIO_IO_EVENT_RES2 = 24;
const size_t AIO_IO_EVENT_SIZEOF = 32;

dword_t sys_io_setup(dword_t nr_events, addr_t ctx_idp) {
    STRACE("io_setup(%d, 0x%x)", nr_events, ctx_idp);

    struct aioctx *ctx = aioctx_new(nr_events, current->pid);
    if (ctx == NULL) return _ENOMEM;
    if (IS_ERR(ctx)) return PTR_ERR(ctx);

    int ctx_id = aioctx_table_insert(&current->aioctx, ctx);
    aioctx_release(ctx);
    if (ctx_id < 0) {
        return ctx_id;
    }

    dword_t ctx_id_guest = (dword_t)ctx_id;
    if (ctx_idp && user_write(ctx_idp, (char*)&ctx_id_guest, sizeof(dword_t)))
        return _EFAULT;

    return 0;
}

dword_t sys_io_destroy(dword_t ctx_id) {
    STRACE("io_destroy(%d)", ctx_id);

    int err = aioctx_table_remove(&current->aioctx, ctx_id) < 0;
    if (err < 0) {
        return err;
    }

    return 0;
}

dword_t sys_io_getevents(dword_t ctx_id, dword_t min_nr, dword_t nr, addr_t events, addr_t timeout_addr) {
    STRACE("io_getevents(0x%x, %d, %d, 0x%x, 0x%x)", ctx_id, min_nr, nr, events, timeout_addr);

    struct aioctx *ctx = aioctx_table_get_and_retain(&current->aioctx, ctx_id);
    if (ctx == NULL) return _EINVAL;
    if (events == 0) return _EFAULT;
    
    struct timespec_ guest_timeout;
    struct timespec host_timeout;
    struct timespec *timeout = &host_timeout;

    if (timeout_addr != 0) {
        if (user_get(timeout_addr, guest_timeout)) return _EFAULT;
        host_timeout.tv_sec = guest_timeout.sec;
        host_timeout.tv_nsec = guest_timeout.nsec;
    } else {
        timeout = NULL;
    }

    dword_t i = 0;
    for (i = 0; i < nr; i += 1) {
        uint64_t user_data;
        addr_t iocbp;
        struct aioctx_event_complete cdata;

        if (!aioctx_consume_completed_event(ctx, &user_data, &iocbp, &cdata)) {
            if (i >= min_nr) break;

            int err = aioctx_wait_for_completion(ctx, timeout);

            if (err == _ETIMEDOUT) break;
            if (err < 0) return err;
            continue;
        }

        uint64_t obj = (uint64_t)iocbp;

        if (user_put(events + AIO_IO_EVENT_DATA, user_data)) return _EFAULT;
        if (user_put(events + AIO_IO_EVENT_OBJ, obj)) return _EFAULT;
        if (user_put(events + AIO_IO_EVENT_RES, cdata.result[0])) return _EFAULT;
        if (user_put(events + AIO_IO_EVENT_RES2, cdata.result[1])) return _EFAULT;

        events += AIO_IO_EVENT_SIZEOF;
    }

    return i;
}

dword_t sys_io_submit(dword_t ctx_id, dword_t u_nr, addr_t iocbpp) {
    sdword_t nr = (sdword_t)u_nr;
    STRACE("io_submit(0x%x, %d, 0x%x)", ctx_id, nr, iocbpp);

    if (nr < 0) return _EINVAL;

    struct aioctx *ctx = aioctx_table_get_and_retain(&current->aioctx, ctx_id);
    if (ctx == NULL) return _EINVAL;

    sdword_t i;
    signed int err;
    for (i = 0; i < nr; i += 1) {
        addr_t iocbp = 0;
        if (user_get(iocbpp + i * sizeof(addr_t), iocbp)) goto fault;

        struct _guest_iocb giocb = {0};
        if (user_get(iocbp, giocb)) goto fault;

        struct aioctx_event_pending host_iocb;
        
        host_iocb.op = (enum aioctx_op)giocb.lio_opcode;
        host_iocb.fd = giocb.fildes;
        host_iocb.buf = giocb.buf;
        host_iocb.nbytes = giocb.nbytes;
        host_iocb.offset = giocb.offset;

        lock(&current->files->lock);

        struct fd *fdp = fdtable_get(current->files, host_iocb.fd);
        if (fdp == NULL) {
            unlock(&current->files->lock);
            
            // Linux man pages mention only the FIRST FD is checked (why?).
            // TODO: It also doesn't say what happens if further IOCBs are
            // unchecked, so I'm assuming it halts IOCB processing at this
            // point.
            if (i == 0) goto badf;
            break;
        }

        err = aioctx_submit_pending_event(ctx, giocb.data, iocbp, host_iocb);
        if (err < 0) {
            // TODO: This assumes the usual pattern of "first IOCB errors, all
            // others stop processing without erroring"
            unlock(&current->files->lock);

            if (i == 0) goto err;
            break;
        }

        unsigned int event_id = (unsigned int)err;
        if (fdp->ops->io_submit) {
            err = fdp->ops->io_submit(fdp, ctx, event_id);
        } else {
            err = aio_fallback_submit(fdp, ctx, event_id);
        }
        
        unlock(&current->files->lock);

        if (err < 0) {
            aioctx_cancel_event(ctx, event_id);

            if (i == 0) goto err;
            break;
        }
    }

    aioctx_release(ctx);
    return i;

fault:
    aioctx_release(ctx);
    return _EFAULT;

badf:
    aioctx_release(ctx);
    return _EBADF;

err:
    aioctx_release(ctx);
    return err;
}

dword_t sys_io_cancel(dword_t ctx_id, addr_t iocb, addr_t result) {
    STRACE("io_submit(0x%x, 0x%x, 0x%x)", ctx_id, iocb, result);

    return _ENOSYS;
}

/**
 * Do a single PREAD operation, falling back to seek-and-read if necessary.
 * 
 * The return code corresponds to the 'sync error' concept of fallback_submit,
 * while async errors should be flagged by writing to `*err`.
 * 
 * `*err` also is used to return the total number of bytes read.
 */
static signed int __aio_fallback_pread(
    struct fd *fd,
    addr_t guest_buf,
    uint64_t nbytes,
    int64_t offset,
    signed int *err) {
    
    if (nbytes > 0xFFFFFFFE) return _ENOMEM;

    // Don't ask me why, but the sync I/O code null-terminates it's
    // buffers, so I'm doing it here too.
    char *buf = malloc(nbytes + 1);
    if (buf == NULL) return _ENOMEM;
    
    if (fd->ops->pread) {
        *err = fd->ops->pread(fd, buf, nbytes, offset);
    } else if (fd->ops->read && fd->ops->lseek) {
        off_t_ saved_off = fd->ops->lseek(fd, 0, LSEEK_CUR);
        if (saved_off < 0) {
            *err = saved_off;
            goto fail_async;
        }

        off_t_ seek_result = fd->ops->lseek(fd, offset, LSEEK_SET);
        if (seek_result < 0) {
            *err = seek_result;
            goto fail_async;
        }

        ssize_t read_bytes = fd->ops->read(fd, buf, nbytes);

        seek_result = fd->ops->lseek(fd, saved_off, LSEEK_SET);
        if (seek_result < 0) {
            *err = seek_result;
            goto fail_async;
        }
        
        *err = read_bytes;
    } else {
        goto fail_einval;
    }

    if (*err < 0) goto fail_async;

    buf[*err] = '\0';
    if (user_write(guest_buf, buf, *err)) goto fail_efault;

fail_async:
    free(buf);
    return 0;

fail_einval:
    free(buf);
    return _EINVAL;

fail_efault:
    free(buf);
    return _EFAULT;
}

/**
 * Do a single PWRITE operation, falling back to seek-and-write if necessary.
 * 
 * The return code corresponds to the 'sync error' concept of fallback_submit,
 * while async errors should be flagged by writing to `*err`.
 * 
 * `*err` also is used to return the total number of bytes written.
 */
static signed int __aio_fallback_pwrite(
    struct fd *fd,
    addr_t guest_buf,
    uint64_t nbytes,
    int64_t offset,
    signed int *err) {
    
    if (nbytes > 0xFFFFFFFE) return _ENOMEM;

    char *buf = malloc(nbytes);
    if (buf == NULL) return _ENOMEM;

    if (user_read(guest_buf, buf, nbytes)) {
        free(buf);
        return _EFAULT;
    }

    ssize_t written_bytes;
    if (fd->ops->pwrite) {
        written_bytes = fd->ops->pwrite(fd, buf, nbytes, offset);
    } else if (fd->ops->write && fd->ops->lseek) {
        off_t_ saved_off = fd->ops->lseek(fd, 0, LSEEK_CUR);
        if (saved_off < 0) {
            *err = saved_off;
            goto fail_async;
        }

        off_t_ seek_result = fd->ops->lseek(fd, offset, LSEEK_SET);
        if (seek_result < 0) {
            *err = seek_result;
            goto fail_async;
        }

        written_bytes = fd->ops->write(fd, buf, nbytes);

        seek_result = fd->ops->lseek(fd, saved_off, LSEEK_SET);
        if (seek_result < 0) {
            *err = seek_result;
            goto fail_async;
        }
    } else {
        free(buf);
        return _EINVAL;
    }

    *err = (signed int)written_bytes;

fail_async:
    free(buf);
    return 0;
}

int aio_fallback_submit(struct fd *fd, struct aioctx *ctx, unsigned int event_id) {
    aioctx_lock(ctx);

    struct aioctx_event_pending *evt = NULL;

    // General structure of the fallback:
    // 
    // Some errors we're going to treat as fatal and return immediately. These
    // should trigger event cancellation. We call these "sync errors".
    // 
    // Other errors will be returned as the result of event completion. We call
    // these "async errors". These do NOT cancel the AIO event, but are treated
    // as the event's completion.
    // 
    // Why do it this way? Because the manpages for io_submit say so - only a
    // certain subset of errors are returned by it, and only for the first
    // IOCB submitted.
    signed int sync_err = aioctx_get_pending_event(ctx, event_id, &evt);
    signed int async_result0 = 0;
    if (sync_err < 0 || evt == NULL) {
        aioctx_unlock(ctx);

        if (sync_err == 0) return _EINVAL;
        return sync_err;
    }

    struct iovec_ *iov_list = NULL;
    switch (evt->op) {
        case AIOCTX_PREAD:
            sync_err = __aio_fallback_pread(fd, (addr_t)evt->buf, evt->nbytes, evt->offset, &async_result0);
            break;
        case AIOCTX_PWRITE:
            sync_err = __aio_fallback_pwrite(fd, (addr_t)evt->buf, evt->nbytes, evt->offset, &async_result0);
            break;
        case AIOCTX_FSYNC:
        case AIOCTX_FDSYNC:
            if (fd->ops->fsync) {
                async_result0 = fd->ops->fsync(fd);
            } else {
                sync_err = _EINVAL;
            }

            break;
        case AIOCTX_NOOP:
            async_result0 = 0;
            sync_err = 0;
            break;
        case AIOCTX_PREADV:
            iov_list = read_iovec((addr_t)evt->buf, evt->nbytes);
            if (IS_ERR(iov_list)) {
                sync_err = PTR_ERR(iov_list);
                break;
            }

            ssize_t total_read = 0;

            for (unsigned int i = 0; i < evt->nbytes; i += 1) {
                signed int cur_read = 0;
                sync_err = __aio_fallback_pread(fd, iov_list[i].base, iov_list[i].len, evt->offset + total_read, &cur_read);
                if (sync_err < 0 || cur_read < 0) {
                    async_result0 = cur_read;
                    break;
                }

                total_read += cur_read;

                if ((uint_t)cur_read < iov_list[i].len) break;
            }

            free(iov_list);

            if (sync_err < 0 || async_result0 < 0) break;

            async_result0 = total_read;
            break;
        case AIOCTX_PWRITEV:
            iov_list = read_iovec((addr_t)evt->buf, evt->nbytes);
            if (IS_ERR(iov_list)) {
                sync_err = PTR_ERR(iov_list);
                break;
            }

            ssize_t total_write = 0;

            for (unsigned int i = 0; i < evt->nbytes; i += 1) {
                signed int cur_write = 0;
                sync_err = __aio_fallback_pwrite(fd, iov_list[i].base, iov_list[i].len, evt->offset + total_write, &cur_write);
                if (sync_err < 0 || cur_write < 0) {
                    async_result0 = cur_write;
                    break;
                }

                total_write += cur_write;

                if ((uint_t)cur_write < iov_list[i].len) break;
            }

            free(iov_list);

            if (sync_err < 0 || async_result0 < 0) break;

            async_result0 = total_write;
            break;
        //TODO: AIOCTX_POLL
        default:
            sync_err = _EINVAL;
            break;
    }

    aioctx_unlock(ctx);

    if (sync_err == 0) {
        aioctx_complete_event(ctx, event_id, async_result0, 0);
    }

    return sync_err;
}
