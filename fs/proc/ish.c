#include "fs/proc.h"
#include "fs/proc/ish.h"
#include "kernel/errno.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *proc_ish_version = "";
struct all_user_default_keys user_default_keys;
void (*user_default_keys_requisition)(void);
void (*user_default_keys_relinquish)(void);
bool (*get_user_default)(char *name, char **buffer, size_t *size);
bool (*set_user_default)(char *name, char *buffer, size_t size);
bool (*remove_user_default)(char *name);

static void proc_ish_defaults_initialize(struct proc_entry *UNUSED(entry)) {
    user_default_keys_requisition();
}

static void proc_ish_defaults_deinitialize(struct proc_entry *UNUSED(entry)) {
    user_default_keys_relinquish();
}

static void proc_ish__defaults_getname(struct proc_entry *entry, char *buf) {
    strcpy(buf, user_default_keys.entries[entry->index].underlying_name);
}

static void proc_ish_defaults_getname(struct proc_entry *entry, char *buf) {
    strcpy(buf, user_default_keys.entries[entry->index].name);
}

static int proc_ish_defaults_readlink(struct proc_entry *entry, char *buf) {
    sprintf(buf, "../.defaults/%s", user_default_keys.entries[entry->index].underlying_name);
    return 0;
}

static int proc_ish__defaults_show(struct proc_entry *entry, struct proc_data *data) {
    size_t size;
    char *buffer;
    if (get_user_default(user_default_keys.entries[entry->index].underlying_name, &buffer, &size)) {
        proc_buf_append(data, buffer, size);
        free(buffer);
        return 0;
    } else {
        return _EIO;
    }
}

static int proc_ish__defaults_update(struct proc_entry *entry, struct proc_data *data) {
    if (set_user_default(user_default_keys.entries[entry->index].underlying_name, data->data, data->size)) {
        return 0;
    } else {
        return _EIO;
    }
}

static int proc_ish_defaults_unlink(struct proc_entry *entry) {
    return remove_user_default(user_default_keys.entries[entry->index].underlying_name) ? 0 : _EIO;
}

struct proc_dir_entry proc_ish__defaults_fd = { NULL,
    .ref = proc_ish_defaults_initialize,
    .unref = proc_ish_defaults_deinitialize,
    .getname = proc_ish__defaults_getname,
    .show = proc_ish__defaults_show,
    .update = proc_ish__defaults_update,
    .unlink = proc_ish_defaults_unlink
};
struct proc_dir_entry proc_ish_defaults_fd = { NULL, S_IFLNK,
    .ref = proc_ish_defaults_initialize,
    .unref = proc_ish_defaults_deinitialize,
    .getname = proc_ish_defaults_getname,
    .readlink = proc_ish_defaults_readlink,
    .unlink = proc_ish_defaults_unlink
};

static bool proc_ish__defaults_readdir(struct proc_entry *UNUSED(entry), unsigned long *index, struct proc_entry *next_entry) {
    *next_entry = (struct proc_entry){&proc_ish__defaults_fd, .index = *index, .fd = *index};
    ++(*index);
    return *index < user_default_keys.count;
}

static bool proc_ish_defaults_readdir(struct proc_entry *UNUSED(entry), unsigned long *index, struct proc_entry *next_entry) {
    while (*index < user_default_keys.count && !user_default_keys.entries[*index].name) {
        ++(*index);
    }
    *next_entry = (struct proc_entry){&proc_ish_defaults_fd, .index = *index, .fd = *index};
    ++(*index);
    return *index < user_default_keys.count;
}

static int proc_ish_show_version(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    proc_printf(buf, "%s\n", proc_ish_version);
    return 0;
}

struct proc_children proc_ish_children = PROC_CHILDREN({
    {".defaults", S_IFDIR,
        .ref = proc_ish_defaults_initialize,
        .unref = proc_ish_defaults_deinitialize,
        .readdir = proc_ish__defaults_readdir,
    },
    {"defaults", S_IFDIR,
        .ref = proc_ish_defaults_initialize,
        .unref = proc_ish_defaults_deinitialize,
        .readdir = proc_ish_defaults_readdir,
    },
    {"version", .show = proc_ish_show_version},
});

