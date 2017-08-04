#include <string.h>
#include "util/buffer.h"
#include "sys/errno.h"

int buf_init(struct buffer *buf, size_t capacity) {
    buf->data = malloc(capacity);
    if (buf->data == NULL)
        return _ENOMEM;
    buf->capacity = capacity;
    buf->start = buf->unread = 0;
    pthread_mutex_init(&buf->lock, NULL);
    pthread_cond_init(&buf->changed, NULL);
    return 0;
}

void buf_free(struct buffer *buf, size_t capacity) {
    free(buf->data);
}

size_t buf_unread(struct buffer *buf) {
    lock(buf);
    size_t unread = buf->unread;
    unlock(buf);
    return unread;
}

size_t buf_remaining(struct buffer *buf) {
    lock(buf);
    size_t remaining = buf->capacity - buf->unread;
    unlock(buf);
    return remaining;
}

size_t buf_read(struct buffer *buf, char *str, size_t len, int flags) {
    lock(buf);
    while (len > buf_unread(buf)) {
        if (flags & BUF_BLOCK)
            wait_for(buf, changed);
        else
            return 0;
    }

    // read up to the end of the buffer
    size_t len1 = len;
    if (buf->start + len > buf->capacity)
        len1 = buf->capacity - buf->start;
    memcpy(str, buf->data + buf->start, len1);
    // wrap around if necessary
    memcpy(str + len1, buf->data, len - len1);

    buf->start += len;
    if (buf->start >= buf->capacity)
        buf->start -= buf->capacity;
    buf->unread -= len;
    signal(buf, changed);
    unlock(buf);
    return len;
}

size_t buf_write(struct buffer *buf, char *str, size_t len, int flags) {
    lock(buf);
    while (len > buf_remaining(buf)) {
        if (flags & BUF_BLOCK)
            wait_for(buf, changed);
        else
            return 0;
    }

    size_t end = buf->start + buf->unread;

    // copy data up to the end of the buffer
    size_t len1 = len;
    if (end + len > buf->capacity)
        len1 = buf->capacity - end;
    memcpy(buf->data + end, str, len1);
    // if we have to wrap around, do so
    memcpy(buf->data, str + len1, len - len1);

    buf->unread += len;
    signal(buf, changed);
    unlock(buf);
    return len;
}

