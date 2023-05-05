#include <string.h>
#include <sys/stat.h>
#include "emu/memory.h"
#include "kernel/calls.h"
#include "fs/proc.h"
#include "fs/fd.h"
#include "fs/tty.h"
#include "kernel/fs.h"
#include "kernel/vdso.h"
#include "util/sync.h"

static void proc_pid_getname(struct proc_entry *entry, char *buf) {
    sprintf(buf, "%d", entry->pid);
}

static struct task *proc_get_task(struct proc_entry *entry) {
    lock(&pids_lock);
    struct task *task = pid_get_task(entry->pid);
    if (task == NULL)
        unlock(&pids_lock);
    return task;
}
static void proc_put_task(struct task *UNUSED(task)) {
    unlock(&pids_lock);
}

static int proc_pid_stat_show(struct proc_entry *entry, struct proc_data *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    lock(&task->general_lock);
    lock(&task->group->lock);
    lock(&task->sighand->lock);

    proc_printf(buf, "%d ", task->pid);
    proc_printf(buf, "(%.16s) ", task->comm);
    proc_printf(buf, "%c ",
            task->zombie ? 'Z' :
            task->group->stopped ? 'T' :
            'R'); // I have no visibility into sleep state at the moment
    proc_printf(buf, "%d ", task->parent ? task->parent->pid : 0);
    proc_printf(buf, "%d ", task->group->pgid);
    proc_printf(buf, "%d ", task->group->sid);
    struct tty *tty = task->group->tty;
    proc_printf(buf, "%d ", tty ? dev_make(tty->driver->major, tty->num) : 0);
    proc_printf(buf, "%d ", tty ? tty->fg_group : 0);
    proc_printf(buf, "%u ", 0); // flags

    // page faults (no data available)
    proc_printf(buf, "%lu ", 0l); // minor faults
    proc_printf(buf, "%lu ", 0l); // children minor faults
    proc_printf(buf, "%lu ", 0l); // major faults
    proc_printf(buf, "%lu ", 0l); // children major faults

    // values that would be returned from getrusage
    // finding these for a given process isn't too easy
    proc_printf(buf, "%lu ", 0l); // user time
    proc_printf(buf, "%lu ", 0l); // system time
    proc_printf(buf, "%ld ", 0l); // children user time
    proc_printf(buf, "%ld ", 0l); // children system time

    proc_printf(buf, "%ld ", 20l); // priority (not adjustable)
    proc_printf(buf, "%ld ", 0l); // nice (also not adjustable)
    proc_printf(buf, "%ld ", list_size(&task->group->threads));
    proc_printf(buf, "%ld ", 0l); // itimer value (deprecated, always 0)
    proc_printf(buf, "%lld ", 0ll); // jiffies on process start

    proc_printf(buf, "%lu ", 0l); // vsize
    proc_printf(buf, "%ld ", 0l); // rss
    proc_printf(buf, "%lu ", 0l); // rss limit

    // bunch of shit that can only be accessed by a debugger
    proc_printf(buf, "%lu ", 0l); // startcode
    proc_printf(buf, "%lu ", 0l); // endcode
    proc_printf(buf, "%lu ", task->mm ? task->mm->stack_start : 0);
    proc_printf(buf, "%lu ", 0l); // kstkesp
    proc_printf(buf, "%lu ", 0l); // kstkeip

    proc_printf(buf, "%lu ", (unsigned long) task->pending & 0xffffffff);
    proc_printf(buf, "%lu ", (unsigned long) task->blocked & 0xffffffff);
    uint32_t ignored = 0;
    uint32_t caught = 0;
    for (int i = 0; i < 32; i++) {
        if (task->sighand->action[i].handler == SIG_IGN_)
            ignored |= 1l << i;
        else if (task->sighand->action[i].handler != SIG_DFL_)
            caught |= 1l << i;
    }
    proc_printf(buf, "%lu ", (unsigned long) ignored);
    proc_printf(buf, "%lu ", (unsigned long) caught);

    proc_printf(buf, "%lu ", 0l); // wchan (wtf)
    proc_printf(buf, "%lu ", 0l); // nswap
    proc_printf(buf, "%lu ", 0l); // cnswap
    proc_printf(buf, "%d ", task->exit_signal);
    proc_printf(buf, "%d", 0); // processor
    // that's enough for now
    proc_printf(buf, "\n");

    unlock(&task->sighand->lock);
    unlock(&task->group->lock);
    unlock(&task->general_lock);
    proc_put_task(task);
    return 0;
}

static int proc_pid_statm_show(struct proc_entry *entry, struct proc_data *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;

    proc_printf(buf, "%lu ", 0); // total vm size
    proc_printf(buf, "%lu ", 0); // vm resident size
    proc_printf(buf, "%lu ", 0); // resident shared
    proc_printf(buf, "%lu ", 0); // text
    proc_printf(buf, "%lu ", 0); // lib (always 0 since linux 2.6)
    proc_printf(buf, "%lu ", 0); // data + stack
    proc_printf(buf, "%lu ", 0); // dirty (always 0 since linux 2.6)
    proc_printf(buf, "\n");

    proc_put_task(task);
    return 0;
}

static int proc_pid_auxv_show(struct proc_entry *entry, struct proc_data *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    int err = 0;
    lock(&task->general_lock);
    if (task->mm == NULL)
        goto out_free_task;

    size_t size = task->mm->auxv_end - task->mm->auxv_start;
    char *data = malloc(size);
    if (data == NULL) {
        err = _ENOMEM;
        goto out_free_task;
    }
    if (user_read_task(task, task->mm->auxv_start, data, size) == 0)
        proc_buf_append(buf, data, size);
    free(data);

out_free_task:
    unlock(&task->general_lock);
    proc_put_task(task);
    return err;
}

static int proc_pid_cmdline_show(struct proc_entry *entry, struct proc_data *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    int err = 0;
    lock(&task->general_lock);
    if (task->mm == NULL)
        goto out_free_task;

    size_t size = task->mm->argv_end - task->mm->argv_start;
    char *data = malloc(size);
    if (data == NULL) {
        err = _ENOMEM;
        goto out_free_task;
    }
    if (user_read_task(task, task->mm->argv_start, data, size) == 0)
        proc_buf_append(buf, data, size);
    free(data);

out_free_task:
    unlock(&task->general_lock);
    proc_put_task(task);
    return err;
}

void proc_maps_dump(struct task *task, struct proc_data *buf) {
    struct mem *mem = task->mem;
    if (mem == NULL)
        return;

    read_wrlock(&mem->lock);
    page_t page = 0;
    while (page < MEM_PAGES) {
        // find a region
        while (page < MEM_PAGES && mem_pt(mem, page) == NULL) {
            mem_next_page(mem, &page);
        }
        if (page >= MEM_PAGES)
            break;
        page_t start = page;
        struct pt_entry *start_pt = mem_pt(mem, start);
        struct data *data = start_pt->data;

        // find the end of said region
        while (page < MEM_PAGES) {
            struct pt_entry *pt = mem_pt(mem, page);
            if (pt == NULL)
                break;
            if ((pt->flags & P_RWX) != (start_pt->flags & P_RWX))
                break;
            // region continues if data is the same or both are anonymous
            if (!(pt->data == data || (pt->flags & P_ANONYMOUS && start_pt->flags & P_ANONYMOUS)))
                break;
            mem_next_page(mem, &page);
        }
        page_t end = page;

        // output info
        char path[MAX_PATH] = "";
        if (start_pt->flags & P_GROWSDOWN) {
            strcpy(path, "[stack]");
        } else if (data->name != NULL) {
            strcpy(path, data->name);
        } else if (data->fd != NULL) {
            generic_getpath(start_pt->data->fd, path);
        }
        proc_printf(buf, "%08x-%08x %c%c%c%c %08lx 00:00 %-10d %s\n",
                start << PAGE_BITS, end << PAGE_BITS,
                start_pt->flags & P_READ ? 'r' : '-',
                start_pt->flags & P_WRITE ? 'w' : '-',
                start_pt->flags & P_EXEC ? 'x' : '-',
                start_pt->flags & P_SHARED ? '-' : 'p',
                (unsigned long) data->file_offset, // offset
                0, // inode
                path);
    }
    read_wrunlock(&mem->lock);
}

static int proc_pid_maps_show(struct proc_entry *entry, struct proc_data *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    proc_maps_dump(task, buf);
    proc_put_task(task);
    return 0;
}

static ssize_t proc_pid_mem_pread(struct proc_entry *entry, struct proc_data *buf, off_t offset) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    int result = user_read_task(task, (addr_t)offset, buf->data, buf->size);
    proc_put_task(task);
    return result ? -1 : buf->size;
}

static ssize_t proc_pid_mem_pwrite(struct proc_entry *entry, struct proc_data *buf, off_t offset) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    int result = user_write_task_ptrace(task, (addr_t)offset, buf->data, buf->size);
    proc_put_task(task);
    return result ? -1 : buf->size;
}


static struct proc_dir_entry proc_pid_fd;

static bool proc_pid_fd_readdir(struct proc_entry *entry, unsigned long *index, struct proc_entry *next_entry) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    lock(&task->files->lock);
    while (*index < task->files->size && task->files->files[*index] == NULL)
        (*index)++;
    fd_t f = (*index)++;
    bool any_left = (unsigned) f < task->files->size;
    unlock(&task->files->lock);
    proc_put_task(task);
    *next_entry = (struct proc_entry) {&proc_pid_fd, .pid = entry->pid, .fd = f};
    return any_left;
}

static void proc_pid_fd_getname(struct proc_entry *entry, char *buf) {
    sprintf(buf, "%d", entry->fd);
}

static int proc_pid_fd_readlink(struct proc_entry *entry, char *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    lock(&task->files->lock);
    struct fd *fd = fdtable_get(task->files, entry->fd);
    int err = generic_getpath(fd, buf);
    unlock(&task->files->lock);
    proc_put_task(task);
    return err;
}

static int proc_pid_exe_readlink(struct proc_entry *entry, char *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    lock(&task->general_lock);
    int err = generic_getpath(task->mm->exefile, buf);
    unlock(&task->general_lock);
    proc_put_task(task);
    return err;
}

static void proc_pid_task_getname(struct proc_entry *entry, char *buf) {
    sprintf(buf, "%d", entry->pid);
}

static int proc_pid_task_readlink(struct proc_entry *entry, char *buf) {
    sprintf(buf, "/proc/%d", entry->pid);
    return 0;
}

static struct proc_dir_entry proc_pid_task;

static bool proc_pid_task_readdir(struct proc_entry *entry, unsigned long *index, struct proc_entry *next_entry) {
    // TODO: Expose all threads
    *next_entry = (struct proc_entry) {&proc_pid_task, .pid = entry->pid};
    return !(*index)++;
}


struct proc_children proc_pid_children = PROC_CHILDREN({
    {"auxv", .show = proc_pid_auxv_show},
    {"cmdline", .show = proc_pid_cmdline_show},
    {"exe", S_IFLNK, .readlink = proc_pid_exe_readlink},
    {"fd", S_IFDIR, .readdir = proc_pid_fd_readdir},
    {"maps", .show = proc_pid_maps_show},
    {"mem", .pread = proc_pid_mem_pread, .pwrite = proc_pid_mem_pwrite},
    {"stat", .show = proc_pid_stat_show},
    {"statm", .show = proc_pid_statm_show},
    {"task", S_IFDIR, .readdir = proc_pid_task_readdir},
});

struct proc_dir_entry proc_pid = {NULL, S_IFDIR,
    .children = &proc_pid_children, .getname = proc_pid_getname};

static struct proc_dir_entry proc_pid_fd = {NULL, S_IFLNK,
    .getname = proc_pid_fd_getname, .readlink = proc_pid_fd_readlink};

static struct proc_dir_entry proc_pid_task = {NULL, S_IFLNK,
    .getname = proc_pid_task_getname, .readlink = proc_pid_task_readlink};
