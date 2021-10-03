#include "debug.h"
#include "kernel/calls.h"
#include "kernel/task.h"
#include "kernel/aio.h"
#include "fs/aio.h"

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

dword_t sys_io_submit(addr_t ctx_id, dword_t nr, addr_t iocbpp) {
    STRACE("io_submit(0x%x, %d, 0x%x)", ctx_id, nr, iocbpp);

    return _ENOSYS;
}

dword_t sys_io_cancel(addr_t ctx_id, addr_t iocb, addr_t result) {
    STRACE("io_submit(0x%x, 0x%x, 0x%x)", ctx_id, iocb, result);
    
    return _ENOSYS;
}