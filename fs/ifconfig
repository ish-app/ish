#include <string.h>
#include <sys/stat.h>
#include "kernel/calls.h"
#include "kernel/fs.h"
#include "fs/path.h"
#include "fs/real.h"

#define MAX_FILESYSTEMS 10
static const struct fs_ops *filesystems[MAX_FILESYSTEMS] = {
    &realfs,
    &procfs,
    &devptsfs,
    &tmpfs,
};

void fs_register(const struct fs_ops *fs) {
    for (unsigned i = 0; i < MAX_FILESYSTEMS; i++) {
        if (filesystems[i] == NULL) {
            filesystems[i] = fs;
            return;
        }
    }
    assert(!"reached filesystem limit");
}

struct mount *mount_find(char *path) {
    assert(path_is_normalized(path));
    lock(&mounts_lock);
    struct mount *mount = NULL;
    assert(!list_empty(&mounts)); // this would mean there's no root FS mounted
    list_for_each_entry(&mounts, mount, mounts) {
        size_t n = strlen(mount->point);
        if (strncmp(path, mount->point, n) == 0 && (path[n] == '/' || path[n] == '\0'))
            break;
    }
    mount->refcount++;
    unlock(&mounts_lock);
    return mount;
}

void mount_retain(struct mount *mount) {
    lock(&mounts_lock);
    mount->refcount++;
    unlock(&mounts_lock);
}

void mount_release(struct mount *mount) {
    lock(&mounts_lock);
    mount->refcount--;
    unlock(&mounts_lock);
}

int do_mount(const struct fs_ops *fs, const char *source, const char *point, const char *info, int flags) {
    struct mount *new_mount = malloc(sizeof(struct mount));
    if (new_mount == NULL)
        return _ENOMEM;
    new_mount->point = strdup(point);
    new_mount->source = strdup(source);
    new_mount->info = strdup(info);
    new_mount->flags = flags;
    new_mount->fs = fs;
    new_mount->data = NULL;
    new_mount->refcount = 0;
    if (fs->mount) {
        int err = fs->mount(new_mount);
        if (err < 0) {
            free((void *) new_mount->point);
            free((void *) new_mount->source);
            free(new_mount);
            return err;
        }
    }

    // the list must stay in descending order of mount point length
    struct mount *mount;
    list_for_each_entry(&mounts, mount, mounts) {
        if (strlen(mount->point) <= strlen(new_mount->point))
            break;
    }
    list_add_before(&mount->mounts, &new_mount->mounts);
    return 0;
}

int mount_remove(struct mount *mount) {
    if (mount->refcount != 0)
        return _EBUSY;

    if (mount->fs->umount)
        mount->fs->umount(mount);
    list_remove(&mount->mounts);
    free((void *) mount->info);
    free((void *) mount->source);
    free((void *) mount->point);
    free(mount);
    return 0;
}

int do_umount(const char *point) {
    struct mount *mount;
    bool found = false;
    list_for_each_entry(&mounts, mount, mounts) {
        if (strcmp(point, mount->point) == 0) {
            found = true;
            break;
        }
    }
    if (!found)
        return _EINVAL;
    return mount_remove(mount);
}

// FIXME: this is shit
bool mount_param_flag(const char *info, const char *flag) {
    while (*info != '\0') {
        if (strncmp(info, flag, strlen(flag)) == 0)
            return true;
        info += strcspn(info, ",");
    }
    return false;
}

#define MS_SUPPORTED (MS_READONLY_|MS_NOSUID_|MS_NODEV_|MS_NOEXEC_|MS_SILENT_)
#define MS_FLAGS (MS_READONLY_|MS_NOSUID_|MS_NODEV_|MS_NOEXEC_)

dword_t sys_mount(addr_t source_addr, addr_t point_addr, addr_t type_addr, dword_t flags, addr_t data_addr) {
    char source[MAX_PATH];
    if (user_read_string(source_addr, source, sizeof(source)))
        return _EFAULT;
    char point_raw[MAX_PATH];
    if (user_read_string(point_addr, point_raw, sizeof(point_raw)))
        return _EFAULT;
    char data[MAX_PATH];
    if (data_addr != 0) {
        if (user_read_string(data_addr, data, sizeof(data)))
            return _EFAULT;
    }
    char type[100];
    if (user_read_string(type_addr, type, sizeof(type)))
        return _EFAULT;
    STRACE("mount(\"%s\", \"%s\", \"%s\", %#x, \"%s\")", source, point_raw, type, flags, data_addr != 0 ? data : NULL);

    if (flags & ~MS_SUPPORTED) {
        FIXME("missing mount flags %#x", flags & ~MS_SUPPORTED);
        return _EINVAL;
    }

    const struct fs_ops *fs = NULL;
    for (size_t i = 0; i < sizeof(filesystems)/sizeof(filesystems[0]); i++) {
        if (filesystems[i] && (strcmp(filesystems[i]->name, type) == 0)) {
            fs = filesystems[i];
            break;
        }
    }
    if (fs == NULL)
        return _EINVAL;

    struct statbuf stat;
    int err = generic_statat(AT_PWD, point_raw, &stat, true);
    if (err < 0)
        return err;
    if (!S_ISDIR(stat.mode))
        return _ENOTDIR;

    char point[MAX_PATH];
    err = path_normalize(AT_PWD, point_raw, point, N_SYMLINK_FOLLOW);
    if (err < 0)
        return err;

    lock(&mounts_lock);
    err = do_mount(fs, source, point, data, flags & MS_FLAGS);
    unlock(&mounts_lock);
    return err;
}

#define UMOUNT_NOFOLLOW_ 8

dword_t sys_umount2(addr_t target_addr, dword_t flags) {
    char target_raw[MAX_PATH];
    if (user_read_string(target_addr, target_raw, sizeof(target_raw)))
        return _EFAULT;
    char target[MAX_PATH];
    int err = path_normalize(AT_PWD, target_raw, target,
            flags & UMOUNT_NOFOLLOW_ ? N_SYMLINK_NOFOLLOW : N_SYMLINK_FOLLOW);
    if (err < 0)
        return err;

    lock(&mounts_lock);
    err = do_umount(target);
    unlock(&mounts_lock);
    return err;
}

struct list mounts = {&mounts, &mounts};
lock_t mounts_lock = LOCK_INITIALIZER;
