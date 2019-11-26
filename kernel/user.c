#include "kernel/calls.h"

static int __user_read_task(struct task *task, addr_t addr, void *buf, size_t count) {
    char *cbuf = (char *) buf;
    size_t i = 0;
    while (i < count) {
        char *ptr = mem_ptr(task->mem, addr + i, MEM_READ);
        if (ptr == NULL)
            return 1;
        cbuf[i++] = *ptr;
    }
    return 0;
}

static int __user_write_task(struct task *task, addr_t addr, const void *buf, size_t count) {
    const char *cbuf = (const char *) buf;
    size_t i = 0;
    while (i < count) {
        char *ptr = mem_ptr(task->mem, addr + i, MEM_WRITE);
        if (ptr == NULL)
            return 1;
        *ptr = cbuf[i++];
    }
    return 0;
}

int user_read_task(struct task *task, addr_t addr, void *buf, size_t count) {
    read_wrlock(&task->mem->lock);
    int res = __user_read_task(task, addr, buf, count);
    read_wrunlock(&task->mem->lock);
    return res;
}

int user_read(addr_t addr, void *buf, size_t count) {
    return user_read_task(current, addr, buf, count);
}

int user_write_task(struct task *task, addr_t addr, const void *buf, size_t count) {
    read_wrlock(&task->mem->lock);
    int res = __user_write_task(task, addr, buf, count);
    read_wrunlock(&task->mem->lock);
    return res;
}

int user_write(addr_t addr, const void *buf, size_t count) {
    return user_write_task(current, addr, buf, count);
}

int user_read_string(addr_t addr, char *buf, size_t max) {
    if (addr == 0)
        return 1;
    read_wrlock(&current->mem->lock);
    size_t i = 0;
    while (i < max) {
        if (__user_read_task(current, addr + i, &buf[i], sizeof(buf[i]))) {
            read_wrunlock(&current->mem->lock);
            return 1;
        }
        if (buf[i] == '\0')
            break;
        i++;
    }
    read_wrunlock(&current->mem->lock);
    return 0;
}

int user_write_string(addr_t addr, const char *buf) {
    if (addr == 0)
        return 1;
    read_wrlock(&current->mem->lock);
    size_t i = 0;
    do {
        if (__user_write_task(current, addr + i, &buf[i], sizeof(buf[i]))) {
            read_wrunlock(&current->mem->lock);
            return 1;
        }
        i++;
    } while (buf[i - 1] != '\0');
    read_wrunlock(&current->mem->lock);
    return 0;
}
