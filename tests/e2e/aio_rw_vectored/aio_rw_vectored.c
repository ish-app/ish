#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

typedef uint64_t aio_context_t;

enum {
	IOCB_CMD_PREAD = 0,
	IOCB_CMD_PWRITE = 1,
	IOCB_CMD_FSYNC = 2,
	IOCB_CMD_FDSYNC = 3,
	IOCB_CMD_NOOP = 6,
	IOCB_CMD_PREADV = 7,
	IOCB_CMD_PWRITEV = 8,
};

struct iocb {
    uint64_t aio_data;
    uint32_t aio_key;
    uint32_t aio_rw_flags;
    uint16_t aio_lio_opcode;
    int16_t aio_reqprio;
    uint32_t aio_fildes;
    uint64_t aio_buf;
    uint64_t aio_nbytes;
    int64_t aio_offset;
    uint64_t aio_reserved2;
    uint32_t aio_flags;
    uint32_t aio_resfd;
};

struct io_event {
    uint64_t data;
    uint64_t obj;
    int64_t res;
    int64_t res2;
};

const char* info0 = "Welcome to ASYNC world - ";
const char* info1 = "now you're writing with vectors";

void main() {
	aio_context_t ctx = 0;
	
	int err = syscall(__NR_io_setup, 10, &ctx);
	if (err < 0) {
		printf("io_setup err: %d\n", errno);
		return;
	}
	
	printf("io_setup success\n");
	
	err = open("test.txt", O_CREAT | O_RDWR, 0777);
	if (err < 0) {
		printf("open err: %d\n", errno);
		return;
	}
	
	printf("open success\n");
	
	int fd = err;
	struct iovec req_vecs[2];
	req_vecs[0].iov_base = info0;
	req_vecs[0].iov_len = strlen(info0);
	req_vecs[1].iov_base = info1;
	req_vecs[1].iov_len = strlen(info1);
	
	struct iocb req;
	memset(&req, 0, sizeof(req));
	
	req.aio_data = 0x31337;
	req.aio_lio_opcode = IOCB_CMD_PWRITEV;
	req.aio_fildes = fd;
	req.aio_buf = (uintptr_t)&req_vecs;
	req.aio_nbytes = 2;
	req.aio_offset = 0;
	
	struct iocb* reqs[1];
	reqs[0] = &req;
	
	err = syscall(__NR_io_submit, ctx, 1, &reqs);
	if (err < 0) {
		printf("io_submit err: %d\n", errno);
		return;
	} else if (err == 0) {
		printf("io_submit reports 0 successful IOCBs, but no error code. errno is %d", errno);
	}
	
	printf("io_submit success: %d\n", err);
	
	struct io_event evt;
	
	do {
		err = syscall(__NR_io_getevents, ctx, 0, 1, &evt, NULL);
		if (err < 0) {
			printf("io_getevents err: %d\n", errno);
			return;
		}
	} while (err < 1);
	
	printf("io_getevents success: %d\n", err);
	printf("evt.data: 0x%llX\n", evt.data);
	
	if (evt.obj == (uintptr_t)&req) {
		printf("evt.obj matches &req\n");
	} else {
		printf("evt.obj does NOT match &req, 0x%llX given\n", evt.obj);
		printf("(&req is 0x%llX)\n", (uint64_t)(uintptr_t)&req);
	}
	
	printf("evt.res:  %lld\n", evt.res);
	printf("evt.res2: %lld\n", evt.res2);
	
	memset(&req, 0, sizeof(req));
	char rbuf[2][25];
	
	rbuf[0][24] = 0;
	rbuf[1][24] = 0;
	
	req_vecs[0].iov_base = rbuf[0];
	req_vecs[0].iov_len = 24;
	req_vecs[1].iov_base = rbuf[1];
	req_vecs[1].iov_len = 24;
	
	req.aio_data = 0xCAFEBABE;
	req.aio_lio_opcode = IOCB_CMD_PREADV;
	req.aio_fildes = fd;
	req.aio_buf = (uintptr_t)req_vecs;
	req.aio_nbytes = 2;
	req.aio_offset = 0;
	
	reqs[0] = &req;
	
	err = syscall(__NR_io_submit, ctx, 1, &reqs);
	if (err < 0) {
		printf("io_submit err: %d\n", errno);
		return;
	} else if (err == 0) {
		printf("io_submit reports 0 successful IOCBs, but no error code. errno is %d", errno);
	}
	
	do {
		err = syscall(__NR_io_getevents, ctx, 0, 1, &evt, NULL);
		if (err < 0) {
			printf("io_getevents err: %d\n", errno);
			return;
		}
	} while (err < 1);
	
	printf("io_getevents success: %d\n", err);
	printf("evt.data: 0x%llX\n", evt.data);
	
	if (evt.obj == (uintptr_t)&req) {
		printf("evt.obj matches &req\n");
	} else {
		printf("evt.obj does NOT match &req, 0x%llX given\n", evt.obj);
		printf("(&req is 0x%llX)\n", (uint64_t)(uintptr_t)&req);
	}
	
	printf("evt.res:  %lld\n", evt.res);
	printf("evt.res2: %lld\n", evt.res2);
	
	if (evt.res <= 48) {
		printf("rbuf[0]: %s\n", rbuf[0]);
		printf("rbuf[1]: %s\n", rbuf[1]);
	} else {
		printf("rbuf: <exceeds buffer size>\n");
	}
	
	err = close(fd);
	if (err < 0) {
		printf("close err: %d\n", err);
		return;
	}
	
	printf("close success\n");
	
	err = syscall(__NR_io_destroy, ctx);
	if (err < 0) {
		printf("io_destroy err: %d\n", errno);
		return;
	}
	
	printf("io_destroy success\n");
}
