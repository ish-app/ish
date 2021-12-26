#include "fs/proc.h"
#include <sys/stat.h>

const char *proc_ish_version = "";

static int proc_ish_show_version(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    proc_printf(buf, "%s\n", proc_ish_version);
    return 0;
}

struct proc_children proc_ish_children = PROC_CHILDREN({
    {"version", .show = proc_ish_show_version},
});

