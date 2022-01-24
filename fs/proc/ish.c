#include "fs/proc.h"
#include "fs/proc/ish.h"
#include "kernel/errno.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *proc_ish_version = "";
char **(*get_all_defaults_keys)(void);
char *(*get_friendly_name)(const char *name);
char *(*get_underlying_name)(const char *name);
bool (*get_user_default)(const char *name, char **buffer, size_t *size);
bool (*set_user_default)(const char *name, char *buffer, size_t size);
bool (*remove_user_default)(const char *name);

static void proc_ish_defaults_getname(struct proc_entry *entry, char *buf) {
    strcpy(buf, entry->name);
}

static int proc_ish_defaults_readlink(struct proc_entry *entry, char *buf) {
    char *name = get_underlying_name(entry->name);
    sprintf(buf, "../.defaults/%s", name);
    free(name);
    return 0;
}

static int proc_ish_underlying_defaults_show(struct proc_entry *entry, struct proc_data *data) {
    size_t size;
    char *buffer;
    if (!get_user_default(entry->name, &buffer, &size))
        return _EIO;
    proc_buf_append(data, buffer, size);
    free(buffer);
    return 0;
}

static int proc_ish_underlying_defaults_update(struct proc_entry *entry, struct proc_data *data) {
    if (!set_user_default(entry->name, data->data, data->size))
        return _EIO;
    return 0;
}

static int proc_ish_underlying_defaults_unlink(struct proc_entry *entry) {
    return remove_user_default(entry->name) ? 0 : _EIO;
}

static int proc_ish_defaults_unlink(struct proc_entry *entry) {
    char *name = get_underlying_name(entry->name);
    int err = remove_user_default(name) ? 0 : _EIO;
    free(name);
    return err;
}

struct proc_dir_entry proc_ish_underlying_defaults_fd = { NULL,
    .getname = proc_ish_defaults_getname,
    .show = proc_ish_underlying_defaults_show,
    .update = proc_ish_underlying_defaults_update,
    .unlink = proc_ish_underlying_defaults_unlink,
};
struct proc_dir_entry proc_ish_defaults_fd = { NULL, S_IFLNK,
    .getname = proc_ish_defaults_getname,
    .readlink = proc_ish_defaults_readlink,
    .unlink = proc_ish_defaults_unlink,
};

static void get_child_names(struct proc_entry *entry, unsigned long index) {
    if (index == 0 || entry->child_names == NULL) {
        if (entry->child_names != NULL)
            free_string_array(entry->child_names);
        entry->child_names = get_all_defaults_keys();
    }
}

static bool proc_ish_underlying_defaults_readdir(struct proc_entry *entry, unsigned long *index, struct proc_entry *next_entry) {
    get_child_names(entry, *index);
    if (entry->child_names[*index] == NULL)
        return false;
    next_entry->meta = &proc_ish_underlying_defaults_fd;
    next_entry->name = strdup(entry->child_names[*index]);
    (*index)++;
    return true;
}

static bool proc_ish_defaults_readdir(struct proc_entry *entry, unsigned long *index, struct proc_entry *next_entry) {
    get_child_names(entry, *index);
    char *friendly_name;
    do {
        const char *name = entry->child_names[*index];
        if (name == NULL)
            return false;
        friendly_name = get_friendly_name(name);
        (*index)++;
    } while (friendly_name == NULL);
    next_entry->meta = &proc_ish_defaults_fd;
    next_entry->name = friendly_name;
    return true;
}

static int proc_ish_show_version(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    proc_printf(buf, "%s\n", proc_ish_version);
    return 0;
}

struct proc_children proc_ish_children = PROC_CHILDREN({
    {".defaults", S_IFDIR, .readdir = proc_ish_underlying_defaults_readdir},
    {"defaults", S_IFDIR, .readdir = proc_ish_defaults_readdir},
    {"version", .show = proc_ish_show_version},
});
