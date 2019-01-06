#include <stdlib.h>
#include <string.h>
#include "util/fifo.h"

void fifo_init(struct fifo *fifo, size_t capacity) {
    fifo->buf = malloc(capacity);
    fifo->capacity = capacity;
    fifo->size = fifo->start = 0;
}

void fifo_destroy(struct fifo *fifo) {
    free(fifo->buf);
}

size_t fifo_capacity(struct fifo *fifo) {
    return fifo->capacity;
}
size_t fifo_size(struct fifo *fifo) {
    return fifo->size;
}
size_t fifo_remaining(struct fifo *fifo) {
    return fifo->capacity - fifo->size;
}

int fifo_write(struct fifo *fifo, const void *data, size_t size, int flags) {
    if (size > fifo_remaining(fifo)) {
        if (!(flags & FIFO_OVERWRITE))
            return 1;
        size_t excess = size - fifo_remaining(fifo);
        fifo->start = (fifo->start + excess) % fifo->capacity;
        fifo->size -= excess;
    }

    size_t tail = (fifo->start + fifo->size) % fifo->capacity;;
    size_t first_copy_size = fifo->capacity - tail;
    if (first_copy_size > size)
        first_copy_size = size;
    memcpy(&fifo->buf[tail], data, first_copy_size);
    memcpy(&fifo->buf[0], (char *) data + first_copy_size, size - first_copy_size);
    fifo->size += size;
    return 0;
}

int fifo_read(struct fifo *fifo, void *buf, size_t size, int flags) {
    if (size > fifo_size(fifo))
        return 1;

    size_t start = fifo->start;
    if (flags & FIFO_LAST)
        start = (start + (fifo->size - size)) % fifo->capacity;

    size_t first_copy_size = fifo->capacity - fifo->start;
    if (first_copy_size > size)
        first_copy_size = size;
    memcpy(buf, &fifo->buf[start], first_copy_size);
    memcpy((char *) buf + first_copy_size, &fifo->buf[0], size - first_copy_size);

    if (!(flags & FIFO_PEEK)) {
        fifo->start = (start + size) % fifo->capacity;
        fifo->size -= size;
    }
    return 0;
}

void fifo_flush(struct fifo *fifo) {
    fifo->size = 0;
}
