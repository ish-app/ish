#include "kernel/calls.h"
#include "kernel/aio.h"

dword_t sys_io_setup(dword_t nr_events, addr_t ctx_idp) {
    return _ENOSYS;
}

dword_t sys_io_destroy(addr_t ctx_id) {
    return _ENOSYS;
}

dword_t sys_io_getevents(addr_t ctx_id, dword_t min_nr, dword_t nr, addr_t events, addr_t timeout) {
    return _ENOSYS;
}

dword_t sys_io_submit(addr_t ctx_id, dword_t nr, addr_t iocbpp) {
    return _ENOSYS;
}

dword_t sys_io_cancel(addr_t ctx_id, addr_t iocb, addr_t result) {
    return _ENOSYS;
}