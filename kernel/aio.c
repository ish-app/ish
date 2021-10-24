#include "debug.h"
#include "kernel/calls.h"
#include "kernel/task.h"
#include "kernel/aio.h"
#include "fs/aio.h"
#include "fs/fd.h"

// Guest memory offsets for the IOCB structure.
// Calculated by a test program compiled and ran in iSH itself.
const size_t AIO_IOCB_DATA = 0;
const size_t AIO_IOCB_KEY = 8;
const size_t AIO_IOCB_RW_FLAGS = 12;
const size_t AIO_IOCB_LIO_OPCODE = 16;
const size_t AIO_IOCB_REQPRIO = 18;
const size_t AIO_IOCB_FILDES = 20;
const size_t AIO_IOCB_BUF = 24;
const size_t AIO_IOCB_NBYTES = 32;
const size_t AIO_IOCB_OFFSET = 40;
const size_t AIO_IOCB_RESERVED2 = 48;
const size_t AIO_IOCB_FLAGS = 56;
const size_t AIO_IOCB_RESFD = 60;

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

    int ctx_id = aioctx_table_insert(current->aioctx, ctx);
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

    int err = aioctx_table_remove(current->aioctx, ctx_id) < 0;
    if (err < 0) {
        return err;
    }

    return 0;
}

dword_t sys_io_getevents(dword_t ctx_id, dword_t min_nr, dword_t nr, addr_t events, addr_t timeout) {
    STRACE("io_getevents(0x%x, %d, %d, 0x%x, 0x%x)", ctx_id, min_nr, nr, events, timeout);

    struct aioctx *ctx = aioctx_table_get_and_retain(current->aioctx, ctx_id);
    if (ctx == NULL) return _EINVAL;
    if (events == 0) return _EFAULT;

    dword_t i = 0;
    for (i = 0; i < nr; i += 1) {
        uint64_t user_data;
        addr_t iocbp;
        struct aioctx_event_complete cdata;

        if (!aioctx_consume_completed_event(ctx, &user_data, &iocbp, &cdata)) {
            //TODO: Block until min_nr events recieved or timeout exceeded
            break;
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

    struct aioctx *ctx = aioctx_table_get_and_retain(current->aioctx, ctx_id);
    if (ctx == NULL) return _EINVAL;

    sdword_t i;
    signed int err;
    for (i = 0; i < nr; i += 1) {
        addr_t iocbp = 0;
        if (user_get(iocbpp + i * sizeof(addr_t), iocbp)) goto fault;

        uint64_t user_data = 0;
        if (user_get(iocbp + AIO_IOCB_DATA, user_data)) goto fault;

        struct aioctx_event_pending host_iocb;
        uint16_t op = 0;
        
        if (user_get(iocbp + AIO_IOCB_LIO_OPCODE, op)) goto fault;
        host_iocb.op = (enum aioctx_op)op;

        if (user_get(iocbp + AIO_IOCB_FILDES, host_iocb.fd)) goto fault;
        if (user_get(iocbp + AIO_IOCB_BUF, host_iocb.buf)) goto fault;
        if (user_get(iocbp + AIO_IOCB_NBYTES, host_iocb.nbytes)) goto fault;
        if (user_get(iocbp + AIO_IOCB_OFFSET, host_iocb.offset)) goto fault;

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

        err = aioctx_submit_pending_event(ctx, user_data, iocbp, host_iocb);
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

    char *buf = NULL;
    switch (evt->op) {
        case AIOCTX_PREAD:
            if (evt->nbytes > 0xFFFFFFFE) {
                sync_err = _ENOMEM;
                break;
            }

            // Don't ask me why, but the sync I/O code null-terminates it's
            // buffers, so I'm doing it here too.
            buf = malloc(evt->nbytes + 1);
            if (buf == NULL) {
                sync_err = _ENOMEM;
                break;
            }
            
            if (fd->ops->pread) {
                async_result0 = fd->ops->pread(fd, buf, evt->nbytes, evt->offset);
            } else if (fd->ops->read && fd->ops->lseek) {
                off_t_ saved_off = fd->ops->lseek(fd, 0, LSEEK_CUR);
                if ((async_result0 = fd->ops->lseek(fd, evt->offset, LSEEK_SET))) {
                    free(buf);
                    break;
                }

                ssize_t read_bytes = fd->ops->read(fd, buf, evt->nbytes);

                off_t_ seek_result = fd->ops->lseek(fd, saved_off, LSEEK_SET);
                if (seek_result < 0) {
                    async_result0 = seek_result;
                    free(buf);
                    break;
                }
                
                async_result0 = read_bytes;
            } else {
                free(buf);
                sync_err = _EINVAL;
                break;
            }

            if (async_result0 < 0) {
                free(buf);
                break;
            }

            buf[async_result0] = '\0';
            if (user_write((addr_t)evt->buf, buf, async_result0)) sync_err = _EFAULT;

            free(buf);
            break;
        case AIOCTX_PWRITE:
            if (evt->nbytes > 0xFFFFFFFE) {
                sync_err = _ENOMEM;
                break;
            }

            buf = malloc(evt->nbytes);
            if (buf == NULL) {
                sync_err = _ENOMEM;
                break;
            }

            if (user_read((addr_t)evt->buf, buf, evt->nbytes)) {
                free(buf);
                sync_err = _EFAULT;
                break;
            }

            ssize_t written_bytes;
            if (fd->ops->pwrite) {
                written_bytes = fd->ops->pwrite(fd, buf, evt->nbytes, evt->offset);
            } else if (fd->ops->write && fd->ops->lseek) {
                off_t_ saved_off = fd->ops->lseek(fd, 0, LSEEK_CUR);
                if ((async_result0 = fd->ops->lseek(fd, evt->offset, LSEEK_SET))) {
                    free(buf);
                    break;
                }

                written_bytes = fd->ops->write(fd, buf, evt->nbytes);

                off_t_ seek_result = fd->ops->lseek(fd, saved_off, LSEEK_SET);
                if (seek_result < 0) {
                    async_result0 = seek_result;
                    free(buf);
                    break;
                }
            } else {
                free(buf);
                sync_err = _EINVAL;
                break;
            }

            async_result0 = (signed int)written_bytes;
            free(buf);
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
        //TODO: AIOCTX_POLL, AIOCTX_PREADV, AIOCTX_PWRITEV
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
