#ifndef BUFFER_H
#define BUFFER_H

#include <pthread.h>
#include "misc.h"

// A circular buffer.
struct buffer {
    char *data;
    size_t capacity;
    size_t start; // first byte of data
    size_t unread;

    pthread_mutex_t lock;
    pthread_cond_t changed;
};

int buf_init(struct buffer *buf, size_t capacity);
void buf_free(struct buffer *buf);

// Returns how many bytes of data are currently in the buffer.
size_t buf_unread(struct buffer *buf);
// Returns how much more room there is in the buffer.
size_t buf_remaining(struct buffer *buf);

// If flags is specified as BUF_BLOCK, will block if there's not enough data or
// not enough space. Otherwise, just return 0.
#define BUF_BLOCK 1
size_t buf_read(struct buffer *buf, char *str, size_t len, int flags);
size_t buf_write(struct buffer *buf, char *str, size_t len, int flags);

#endif
