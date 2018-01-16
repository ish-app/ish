#ifndef FDTABLE_H
#define FDTABLE_H
#include "util/bits.h"

struct fdtable {
    atomic_uint refcount;
    unsigned size;
    struct fd **files;
    bits_t *cloexec;
};

struct fdtable *fdtable_alloc();
void fdtable_release(struct fdtable *table);
int fdtable_resize(struct fdtable *table, unsigned size);
struct fdtable *fdtable_copy(struct fdtable *table);
void fdtable_free(struct fdtable *table);

struct fd *f_get(fd_t f);
bool f_is_cloexec(fd_t f);
void f_put(fd_t f, struct fd *fd);
// steals a reference to the fd, gives it to the table on success and destroys it on error
fd_t f_install(struct fd *fd);
int f_close(fd_t f);

#endif
