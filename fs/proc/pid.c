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

static int proc_pid_status_show(struct proc_entry *entry, struct proc_data *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    lock(&task->general_lock);
    lock(&task->group->lock);
    lock(&task->sighand->lock);

    proc_printf(buf, "Name: %.16s\n", task->comm);
    int umask = task->fs->umask & 0777;
    proc_printf(buf, "Umask:    00%o\n", umask);
    char proc_state = (task->zombie ? 'Z' :
                       task->group->stopped ? 'T' :
                       task->io_block && task->pid != current->pid ? 'S' :
                       'R');
    char *long_proc_state = malloc(15);
    switch(proc_state) {
        case 'Z' :
            strcpy(long_proc_state, "Z  (Zombie)");
            break;
        case 'T' :
            strcpy(long_proc_state, "T  (Stopped)");
            break;
        case 'S' :
            strcpy(long_proc_state, "S  (Sleep)");
            break;
        case 'R' :
            strcpy(long_proc_state, "R  (Running)");
            break;
    }
    proc_printf(buf, "State: %s\n", long_proc_state);
    proc_printf(buf, "Tgid: %d\n", task->tgid);
    proc_printf(buf, "Pid: %d\n", task->pid);
    proc_printf(buf, "PPid:  %d\n", task->parent ? task->parent->pid : 0);
    proc_printf(buf, "TracePid: %d\n", 0);
    proc_printf(buf, "Uid: %d   %d  %d  %d\n", task->uid, task->uid, task->uid, task->uid);
    proc_printf(buf, "Gid: %d   %d  %d  %d\n", task->gid, task->gid, task->gid, task->gid);
    proc_printf(buf, "FDSize: %d\n", 0);
    proc_printf(buf, "Groups: %d\n", 0);
    proc_printf(buf, "NStgid: %d\n", 0);
    proc_printf(buf, "NSpid: %d\n", 0);
    proc_printf(buf, "NSpgid: %d\n", 0);
    proc_printf(buf, "NSsid: %d\n", 0);
    proc_printf(buf, "VmPeak: %d\n", 0);
    proc_printf(buf, "VmSize: %d\n", 0);
    proc_printf(buf, "VmLck: %d\n", 0);
    proc_printf(buf, "VmPin: %d\n", 0);
    proc_printf(buf, "VmHWM: %d\n", 0);
    proc_printf(buf, "VmRSS: %d\n", 0);
    proc_printf(buf, "RssAnon: %d\n", 0);
    proc_printf(buf, "RssFile: %d\n", 0);
    proc_printf(buf, "RssShmem: %d\n", 0);
    proc_printf(buf, "VmData: %d\n", 0);
    proc_printf(buf, "VmStk: %d\n", 0);
    proc_printf(buf, "VmExe: %d\n", 0);
    proc_printf(buf, "VmLib: %d\n", 0);
    proc_printf(buf, "VmPTE: %d\n", 0);
    proc_printf(buf, "VmSwap: %d\n", 0);
    proc_printf(buf, "THP_enabled: %d\n", 0);
    proc_printf(buf, "Threads: %d\n", 0);
    proc_printf(buf, "SigQ: %d\n", 0);
    proc_printf(buf, "SigPnd: 0000000000000000\n");
    proc_printf(buf, "ShdPnd: 0000000000000000\n");
    proc_printf(buf, "SigBlk: 0000000000000000\n");
    proc_printf(buf, "SigIgn: 0000000000000000\n");
    proc_printf(buf, "CapInh: 0000000000000000\n");
    proc_printf(buf, "CapPrm: 0000000000000000\n");
    proc_printf(buf, "CapEff: 0000000000000000\n");
    proc_printf(buf, "CapBnd: 0000000000000000\n");
    proc_printf(buf, "CapAmb: 0000000000000000\n");
    proc_printf(buf, "NoNewPrivs:   %d\n",0);
    proc_printf(buf, "Speculation_Store_Bypass):   %d\n",0);
    proc_printf(buf, "Cpus_allowed:   %s\n", "ffffffff");
    proc_printf(buf, "Cpus_allowed_list:   %s\n", "0-31");
    proc_printf(buf, "voluntary_ctxt_switches:   %s\n", "0");
    proc_printf(buf, "nonvoluntary_ctxt_switches:   %s\n", "0");

    unlock(&task->sighand->lock);
    unlock(&task->group->lock);
    unlock(&task->general_lock);
    proc_put_task(task);

    return 0;
}

static int proc_pid_stat_show(struct proc_entry *entry, struct proc_data *buf) {
    struct task *task = proc_get_task(entry);
    if (task == NULL)
        return _ESRCH;
    lock(&task->general_lock);
    lock(&task->group->lock);
    if(task->sighand != NULL) {//mke
        lock(&task->sighand->lock);
    } else {
        printk("%d task->sighand=NULL : ", current->pid);
    }

    // program reads this using read-like syscall, so we are in blocking area,
    // which means its io_block is set to true. When a proc reads an
    // information about itself, but it shouldn't be marked as blocked.
    char proc_state = (task->zombie ? 'Z' :
                       task->group->stopped ? 'T' :
                       task->io_block && task->pid != current->pid ? 'S' :
                       'R');
    
    proc_printf(buf, "%d ", task->pid);
    proc_printf(buf, "(%.16s) ", task->comm);
    proc_printf(buf, "%c ", proc_state);
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
    proc_printf(buf, "%d", task->exit_signal);
    // that's enough for now
    proc_printf(buf, "\n");

    if(task->sighand != NULL) //mke
        unlock(&task->sighand->lock);
    unlock(&task->group->lock);
    unlock(&task->general_lock);
    proc_put_task(task);
    return 0;
}

static int proc_pid_statm_show(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    proc_printf(buf, "%lu ", 0l); // size
    proc_printf(buf, "%lu ", 0l); // resident
    proc_printf(buf, "%lu ", 0l); // shared
    proc_printf(buf, "%lu ", 0l); // text
    proc_printf(buf, "%lu ", 00); // lib (unused since Linux 2.6)
    proc_printf(buf, "%lu ", 0l); // data
    proc_printf(buf, "%lu\n", 00); // dt (unused since Linux 2.6)
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
        proc_buf_write(buf, data, size);
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
        proc_buf_write(buf, data, size);
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

struct proc_dir_entry proc_pid_entries[] = {
    {"auxv", .show = proc_pid_auxv_show},
    {"cmdline", .show = proc_pid_cmdline_show},
    {"exe", S_IFLNK, .readlink = proc_pid_exe_readlink},
    {"fd", S_IFDIR, .readdir = proc_pid_fd_readdir},
    {"maps", .show = proc_pid_maps_show},
    {"stat", .show = proc_pid_stat_show},
    {"statm", .show = proc_pid_statm_show},
    {"status", .show = proc_pid_status_show},
};

struct proc_dir_entry proc_pid = {NULL, S_IFDIR,
    .children = proc_pid_entries, .children_sizeof = sizeof(proc_pid_entries),
    .getname = proc_pid_getname};

static struct proc_dir_entry proc_pid_fd = {NULL, S_IFLNK,
    .getname = proc_pid_fd_getname, .readlink = proc_pid_fd_readlink};
