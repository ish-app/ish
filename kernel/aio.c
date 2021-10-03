#include "debug.h"
#include "kernel/calls.h"
#include "kernel/task.h"
#include "kernel/aio.h"
#include "fs/aio.h"

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

dword_t sys_io_setup(dword_t nr_events, addr_t ctx_idp) {
    STRACE("io_setup(%d, 0x%x)", nr_events, ctx_idp);

    struct aioctx *ctx = aioctx_new(nr_events, current->pid);
    if (ctx == NULL) return _ENOMEM;
    if (IS_ERR(ctx)) return PTR_ERR(ctx);

    int ctx_id = aioctx_table_insert(current->aioctx, ctx);
    if (ctx_id < 0) {
        aioctx_release(ctx);
        return ctx_id;
    }

    dword_t ctx_id_guest = (dword_t)ctx_id;
    if (ctx_idp && user_write(ctx_idp, (char*)&ctx_id_guest, sizeof(dword_t)))
        return _EFAULT;

    return 0;
}

dword_t sys_io_destroy(addr_t p_ctx_id) {
    STRACE("io_destroy(0x%x)", p_ctx_id);

    unsigned int ctx_id = 0;
    if (user_read(p_ctx_id, &ctx_id, sizeof(ctx_id))) return _EFAULT;

    int err = aioctx_table_remove(current->aioctx, ctx_id) < 0;
    if (err < 0) {
        return err;
    }

    return 0;
}

dword_t sys_io_getevents(addr_t ctx_id, dword_t min_nr, dword_t nr, addr_t events, addr_t timeout) {
    STRACE("io_getevents(0x%x, %d, %d, 0x%x, 0x%x)", ctx_id, min_nr, nr, events, timeout);

    return _ENOSYS;
}

dword_t sys_io_submit(addr_t ctx_id, dword_t u_nr, addr_t iocbpp) {
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
        if (user_get(iocbp + AIO_IOCB_LIO_OPCODE, host_iocb.op)) goto fault;
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

        err = aioctx_submit_pending_event(ctx, user_data, host_iocb);
        if (err < 0) {
            // TODO: This assumes the usual pattern of "first IOCB errors, all
            // others stop processing without erroring"
            unlock(&current->files->lock);

            if (i == 0) goto err;
            break;
        }

        // TODO: Actually submit the event to the FD.
        unlock(&current->files->lock);
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

dword_t sys_io_cancel(addr_t ctx_id, addr_t iocb, addr_t result) {
    STRACE("io_submit(0x%x, 0x%x, 0x%x)", ctx_id, iocb, result);

    return _ENOSYS;
}