// Hack to work around the idiotic way iOS handles suspending apps that have
// listening sockets.
// Basically the actual socket part of the file just gets freed, and the socket
// ceases to be a socket. Any attempt to do socket things with it will just
// immediately fail, and anyone blocked on accept will never wake up.
// Solution: keep track of all the listening sockets, and the threads that are
// blocked on them. On suspend, make a record of the names and configuration of
// all the listening sockets. On resume, open new sockets, reconfigure them,
// use dup2 to replace the original sockets, and get any thread waiting on them
// to restart the wait.
// This file contains hooks into various other places to do all that.
// https://developer.apple.com/library/archive/technotes/tn2277/_index.html
#ifndef FS_SOCKRESTART_H
#define FS_SOCKRESTART_H
#include <stdbool.h>
#include "util/list.h"
struct fd;

void sockrestart_begin_listen(struct fd *sock);
void sockrestart_end_listen(struct fd *sock);
void sockrestart_begin_listen_wait(struct fd *sock);
void sockrestart_end_listen_wait(struct fd *sock);
bool sockrestart_should_restart_listen_wait(void);
void sockrestart_on_suspend(void);
void sockrestart_on_resume(void);

struct fd_sockrestart {
    struct list listen;
};

struct task_sockrestart {
    int count;
    bool punt;
    struct list listen;
};

#endif
