#ifndef UTIL_FIFO_H
#define UTIL_FIFO_H
#include <sys/types.h>

struct fifo {
    char *buf;
    size_t capacity;
    size_t size;
    size_t start;
};

#define FIFO_INIT(b) ((struct fifo) {.buf = b, .capacity = sizeof(b)})
void fifo_init(struct fifo *fifo, size_t capacity);
void fifo_destroy(struct fifo *fifo);

size_t fifo_capacity(struct fifo *fifo);
size_t fifo_size(struct fifo *fifo);
size_t fifo_remaining(struct fifo *fifo);

// return 0 on success, 1 if size > fifo_remaining and FIFO_OVERWRITE was not specified
#define FIFO_OVERWRITE 1
int fifo_write(struct fifo *fifo, const void *data, size_t size, int flags);

// return 0 on success, 1 if size > fifo_size
#define FIFO_PEEK 1 // don't remove the bytes
#define FIFO_LAST 2 // return bytes from the end, not the start
int fifo_read(struct fifo *fifo, void *buf, size_t size, int flags);

void fifo_flush(struct fifo *fifo);

#endif
