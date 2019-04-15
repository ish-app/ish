#include <string.h>
#include <signal.h>
#include <pthread.h>
#include "fs/sockrestart.h"
#include "fs/fd.h"
#include "fs/sock.h"
#include "kernel/task.h"
#include "util/list.h"
extern const struct fd_ops socket_fdops;

static lock_t sockrestart_lock = LOCK_INITIALIZER;
static struct list listen_fds = LIST_INITIALIZER(listen_fds);

void sockrestart_begin_listen(struct fd *sock) {
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    list_add(&listen_fds, &sock->sockrestart.listen);
    unlock(&sockrestart_lock);
}

void sockrestart_end_listen(struct fd *sock) {
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    list_remove_safe(&sock->sockrestart.listen);
    unlock(&sockrestart_lock);
}

static struct list listen_tasks = LIST_INITIALIZER(listen_tasks);

void sockrestart_begin_listen_wait(struct fd *sock) {
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    if (current->sockrestart.count == 0)
        list_add(&listen_tasks, &current->sockrestart.listen);
    current->sockrestart.count++;
    unlock(&sockrestart_lock);
}

void sockrestart_end_listen_wait(struct fd *sock) {
    if (sock->ops != &socket_fdops)
        return;
    lock(&sockrestart_lock);
    current->sockrestart.count--;
    if (current->sockrestart.count == 0)
        list_remove(&current->sockrestart.listen);
    unlock(&sockrestart_lock);
}

bool sockrestart_should_restart_listen_wait() {
    lock(&sockrestart_lock);
    bool punt = current->sockrestart.punt;
    current->sockrestart.punt = false;
    unlock(&sockrestart_lock);
    return punt;
}

struct saved_socket {
    struct fd *sock;
    int type;
    int proto;
    union {
        char name[128];
        struct sockaddr name_addr;
    };
    socklen_t name_len;
    struct list saved;
};

static struct list saved_sockets = LIST_INITIALIZER(saved_sockets);

// these should only be called from the main thread, but it's easiest to just lock for the whole time

void sockrestart_on_suspend() {
    lock(&sockrestart_lock);
    assert(list_empty(&saved_sockets));
    struct fd *sock;
    list_for_each_entry(&listen_fds, sock, sockrestart.listen) {
        struct saved_socket *saved = malloc(sizeof(struct saved_socket));
        if (saved == NULL)
            continue; // better than a crash
        saved->sock = fd_retain(sock);
        saved->proto = sock->socket.protocol;
        unsigned size = sizeof(saved->type);
        getsockopt(sock->real_fd, SOL_SOCKET, SO_TYPE, &saved->type, &size);
        assert(size == sizeof(saved->type));
        saved->name_len = sizeof(saved->name);
        getsockname(sock->real_fd, (struct sockaddr *) &saved->name, &saved->name_len);
        list_add(&saved_sockets, &saved->saved);
    }
    unlock(&sockrestart_lock);
}

void sockrestart_on_resume() {
    lock(&sockrestart_lock);
    struct saved_socket *saved, *tmp;
    list_for_each_entry_safe(&saved_sockets, saved, tmp, saved) {
        list_remove(&saved->saved);
        int new_sock = socket(saved->name_addr.sa_family, saved->type, saved->proto);
        if (new_sock < 0) {
            printk("restarting socket(%d, %d, %d) failed: %s\n",
                    saved->name_addr.sa_family, saved->type, saved->proto, strerror(errno));
            goto thank_u_next;
        }
        if (bind(new_sock, (struct sockaddr *) &saved->name, saved->name_len) < 0) {
            printk("rebinding socket failed: %s\n", strerror(errno));
            goto thank_u_next;
        }
        dup2(new_sock, saved->sock->real_fd);

thank_u_next:
        fd_close(saved->sock);
    }
    struct task *task;
    list_for_each_entry(&listen_tasks, task, sockrestart.listen) {
        task->sockrestart.punt = true;
        pthread_kill(task->thread, SIGUSR1);
    }
    unlock(&sockrestart_lock);
}
