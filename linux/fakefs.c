/* Based on hostfs to a significant extent */
#include <asm/fcntl.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/statfs.h>
#include <user/fs.h>

#include <sqlite3.h>
#include "fs/fake-db.h"
#include "../fs/hostfs/hostfs.h" // just a quick way to get stat without typing too much

struct fakefs_super {
    struct fakefs_db db;
    int root_fd;
};

// free with __putname
static char *dentry_name(struct dentry *dentry) {
    char *name = __getname();
    if (name == NULL)
        return ERR_PTR(-ENOMEM);
    char *path = dentry_path_raw(dentry, name, PATH_MAX);
    if (IS_ERR(path))
        return path;
    BUG_ON(path[0] != '/');
    if (strcmp(path, "/") == 0)
        path[0] = '\0';
    memmove(name, path, strlen(path) + 1);
    return name;
}

/***** inode *****/

static int restat_inode(struct inode *ino);
static int read_inode(struct inode *ino);

#define INODE_FD(ino) (*((uintptr_t *) &(ino)->i_private))

static int open_fd_for_dentry(struct inode *dir, struct dentry *dentry) {
    int fd = host_openat(INODE_FD(dir), dentry->d_name.name, O_RDWR, 0);
    if (fd == -EISDIR)
        fd = host_openat(INODE_FD(dir), dentry->d_name.name, O_RDONLY, 0);
    return fd;
}

static struct dentry *fakefs_lookup(struct inode *ino, struct dentry *dentry, unsigned int flags) {
    struct fakefs_super *info = ino->i_sb->s_fs_info;
    struct inode *child = NULL;

    char *path = dentry_name(dentry);
    if (IS_ERR(path))
        return ERR_PTR(PTR_ERR(path));
    db_begin(&info->db);
    inode_t child_ino = path_get_inode(&info->db, path);
    __putname(path);
    if (child_ino == 0)
        goto out;

    child = ilookup(ino->i_sb, child_ino);
    if (child != NULL)
        goto out;

    int fd = open_fd_for_dentry(ino, dentry);
    if (fd < 0) {
        child = ERR_PTR(fd);
        goto out;
    }

    child = new_inode(ino->i_sb);
    if (child == NULL) {
        child = ERR_PTR(-ENOMEM);
        host_close(fd);
        goto out;
    }
    child->i_ino = child_ino;
    INODE_FD(child) = fd;
    int err = read_inode(child);
    if (err < 0) {
        iput(child);
        /* TODO: check whether iput manages to close the FD by calling evict_inode */
        child = ERR_PTR(err);
        goto out;
    }

out:
    if (IS_ERR(child)) {
        db_rollback(&info->db);
        printk("fakefs_lookup failed: %pe\n", child);
    } else {
        db_commit(&info->db);
    }
    return d_splice_alias(child, dentry);
}

static int __finish_make_node(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev, int fd) {
    struct fakefs_super *info = dir->i_sb->s_fs_info;
    struct inode *child = NULL;
    int err;

    if (fd == -1)
        fd = open_fd_for_dentry(dir, dentry);
    if (fd < 0) {
        err = fd;
        goto fail;
    }

    child = new_inode(dir->i_sb);
    err = -ENOMEM;
    if (child == NULL) {
        host_close(fd);
        goto fail;
    }
    inode_init_owner(mnt_userns, child, dir, mode);
    INODE_FD(child) = fd;

    char *path = dentry_name(dentry);
    if (IS_ERR(path)) {
        err = PTR_ERR(path);
        goto fail;
    }

    db_begin(&info->db);
    struct ish_stat ishstat = {
        .mode = mode,
        .uid = i_uid_read(child),
        .gid = i_gid_read(child),
        .rdev = rdev,
    };
    child->i_ino = path_create(&info->db, path, &ishstat);
    __putname(path);

    err = read_inode(child);
    if (err < 0) {
        db_rollback(&info->db);
        goto fail;
    }
    db_commit(&info->db);
    d_instantiate(dentry, child);
    return 0;

fail:
    if (child != NULL)
        iput(child);
    host_unlinkat(INODE_FD(dir), dentry->d_name.name);
    return err;
}

static int finish_make_node(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, int fd) {
    return __finish_make_node(mnt_userns, dir, dentry, mode, 0, fd);
}

static int fakefs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    int fd = host_openat(INODE_FD(dir), dentry->d_name.name, O_CREAT | O_RDWR | (excl ? O_EXCL : 0), 0666);
    if (fd < 0)
        return fd;
    return finish_make_node(mnt_userns, dir, dentry, mode, fd);
}

static int fakefs_rename(struct user_namespace *mnt_userns, struct inode *from_dir, struct dentry *from_dentry, struct inode *to_dir, struct dentry *to_dentry, unsigned flags) {
    if (flags != 0)
        return -EINVAL;
    struct fakefs_super *info = from_dir->i_sb->s_fs_info;

    char *from_path = dentry_name(from_dentry);
    if (IS_ERR(from_path))
        return PTR_ERR(from_path);
    char *to_path = dentry_name(to_dentry);
    if (IS_ERR(to_path)) {
        __putname(from_path);
        return PTR_ERR(to_path);
    }

    db_begin(&info->db);
    path_rename(&info->db, from_path, to_path);
    __putname(from_path);
    __putname(to_path);

    int err = host_renameat(INODE_FD(from_dir), from_dentry->d_name.name,
                            INODE_FD(to_dir), to_dentry->d_name.name);
    if (err < 0) {
        db_rollback(&info->db);
        return err;
    }
    db_commit(&info->db);

    return 0;
}

static int fakefs_link(struct dentry *from, struct inode *ino, struct dentry *to) {
    struct fakefs_super *info = ino->i_sb->s_fs_info;
    struct inode *inode;

    char *from_path = dentry_name(from);
    if (IS_ERR(from_path))
        return PTR_ERR(from_path);
    char *to_path = dentry_name(to);
    if (IS_ERR(to_path)) {
        __putname(from_path);
        return PTR_ERR(to_path);
    }

    db_begin(&info->db);
    path_link(&info->db, from_path, to_path);
    __putname(from_path);
    __putname(to_path);

    int err = host_linkat(INODE_FD(d_inode(from->d_parent)), from->d_name.name,
                          INODE_FD(d_inode(to->d_parent)), to->d_name.name);
    if (err < 0) {
        db_rollback(&info->db);
        return err;
    }
    db_commit(&info->db);

    inode = d_inode(from);
    ihold(inode);
    d_instantiate(to, inode);
    return 0;
}

static int unlink_common(struct inode *dir, struct dentry *dentry, int is_dir) {
    struct fakefs_super *info = dir->i_sb->s_fs_info;
    char *path = dentry_name(dentry);
    if (IS_ERR(path))
        return PTR_ERR(path);

    db_begin(&info->db);
    path_unlink(&info->db, path);
    __putname(path);

    int err = (is_dir ? host_rmdirat : host_unlinkat)(INODE_FD(dir), dentry->d_name.name);
    if (err < 0) {
        db_rollback(&info->db);
        return err;
    }
    db_commit(&info->db);
    return 0;
}

static int fakefs_unlink(struct inode *dir, struct dentry *dentry) {
    return unlink_common(dir, dentry, 0);
}

static int fakefs_rmdir(struct inode *dir, struct dentry *dentry) {
    return unlink_common(dir, dentry, 1);
}

static int fakefs_symlink(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, const char *target) {
    int fd = host_openat(INODE_FD(dir), dentry->d_name.name, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return fd;
    ssize_t res = host_write(fd, target, strlen(target));
    if (res < 0) {
        host_close(fd);
        host_unlinkat(INODE_FD(dir), dentry->d_name.name);
        return res;
    }
    return finish_make_node(mnt_userns, dir, dentry, S_IFLNK | 0777, fd);
}

static int fakefs_mkdir(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode) {
    int err = host_mkdirat(INODE_FD(dir), dentry->d_name.name, 0777);
    if (err < 0)
        return err;
    err = finish_make_node(mnt_userns, dir, dentry, S_IFDIR | mode, -1);
    if (err < 0)
        host_rmdirat(INODE_FD(dir), dentry->d_name.name);
    return err;
}

static int fakefs_mknod(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev) {
    int fd = host_openat(INODE_FD(dir), dentry->d_name.name, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return fd;
    return __finish_make_node(mnt_userns, dir, dentry, mode, dev, fd);
}

static int fakefs_setattr(struct user_namespace *mnt_userns, struct dentry *dentry, struct iattr *attr) {
    int err = setattr_prepare(mnt_userns, dentry, attr);
    if (err < 0)
        return err;
    struct inode *inode = d_inode(dentry);

    // attributes of ishstat
    struct fakefs_super *info = inode->i_sb->s_fs_info;
    if (attr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID)) {
        db_begin(&info->db);
        struct ish_stat stat;
        inode_read_stat(&info->db, inode->i_ino, &stat);
        if (attr->ia_valid & ATTR_MODE)
            stat.mode = attr->ia_mode;
        if (attr->ia_valid & ATTR_UID)
            stat.uid = from_kuid(mnt_userns, attr->ia_uid);
        if (attr->ia_valid & ATTR_GID)
            stat.gid = from_kgid(mnt_userns, attr->ia_gid);
        inode_write_stat(&info->db, inode->i_ino, &stat);
        db_commit(&info->db);
    }

    // size
    if (attr->ia_valid & ATTR_SIZE && attr->ia_size != i_size_read(inode)) {
        err = host_ftruncate(INODE_FD(inode), attr->ia_size);
        if (err < 0)
            return err;
        truncate_setsize(inode, attr->ia_size);
    }

    // time
    if (attr->ia_valid & (ATTR_ATIME | ATTR_ATIME_SET | ATTR_MTIME | ATTR_MTIME_SET)) {
        struct timespec64 atime = {.tv_nsec = UTIME_OMIT};
        if (attr->ia_valid & ATTR_ATIME_SET)
            atime = attr->ia_atime;
        else if (attr->ia_valid & ATTR_ATIME)
            atime.tv_nsec = UTIME_NOW;
        struct timespec64 mtime = {.tv_nsec = UTIME_OMIT};
        if (attr->ia_valid & ATTR_MTIME_SET)
            mtime = attr->ia_mtime;
        else if (attr->ia_valid & ATTR_MTIME)
            mtime.tv_nsec = UTIME_NOW;

        struct host_timespec times[2] = {
            {.tv_sec = atime.tv_sec, .tv_nsec = atime.tv_nsec},
            {.tv_sec = mtime.tv_sec, .tv_nsec = mtime.tv_nsec},
        };
        err = host_futimens(INODE_FD(inode), times);
        if (err < 0)
            return err;
        err = restat_inode(inode);
        if (err < 0)
            return err;
    }

    setattr_copy(mnt_userns, inode, attr);
    mark_inode_dirty(inode); // TODO: is this actually necessary?
    return 0;
}

static const struct inode_operations fakefs_iops = {
    .setattr = fakefs_setattr,
};

static const struct inode_operations fakefs_dir_iops = {
    .lookup = fakefs_lookup,
    .create = fakefs_create,
    .link = fakefs_link,
    .unlink = fakefs_unlink,
    .symlink = fakefs_symlink,
    .mkdir = fakefs_mkdir,
    .rmdir = fakefs_rmdir,
    .mknod = fakefs_mknod,
    .rename = fakefs_rename,
    .setattr = fakefs_setattr,
};

static const struct inode_operations fakefs_link_iops = {
    .get_link = page_get_link,
};

/***** file *****/

#define FILE_DIR(file) ((file)->private_data)

static int fakefs_file_release(struct inode *inode, struct file *file) {
    filemap_write_and_wait(inode->i_mapping);
    return 0;
}

static int fakefs_fsync(struct file *file, loff_t start, loff_t end,
                        int datasync) {
    int err = file_write_and_wait_range(file, start, end);
    if (err)
        return err;
    return host_fsync(INODE_FD(file->f_inode), datasync);
}

static int fakefs_iterate(struct file *file, struct dir_context *ctx) {
    struct fakefs_super *info = file->f_inode->i_sb->s_fs_info;

    if (FILE_DIR(file) == NULL) {
        int err = host_dup_opendir(INODE_FD(file->f_inode), &FILE_DIR(file));
        if (err < 0)
            return err;
    }
    void *dir = FILE_DIR(file);
    int res;
    if (ctx->pos == 0)
        res = host_rewinddir(dir);
    else
        res = host_seekdir(dir, ctx->pos - 1);
    if (res < 0)
        return res;

    char *dir_path = dentry_name(file->f_path.dentry);
    if (IS_ERR(dir_path))
        return PTR_ERR(dir_path);
    size_t dir_path_len = strlen(dir_path);


    struct host_dirent ent;
    for (;;) {
        res = host_readdir(dir, &ent);
        if (res <= 0)
            break;
        // Get the inode number by constructing the file path and looking it up in the database
        if (strcmp(ent.name, ".") == 0) {
            ent.ino = file->f_inode->i_ino;
        } else if (strcmp(ent.name, "..") == 0) {
            ent.ino = d_inode(file->f_path.dentry->d_parent)->i_ino;
        } else {
            db_begin(&info->db);
            if (dir_path_len + 1 + strlen(ent.name) + 1 > PATH_MAX)
                continue; // a
            dir_path[dir_path_len] = '/';
            strcpy(&dir_path[dir_path_len + 1], ent.name);
            ent.ino = path_get_inode(&info->db, dir_path);
            db_commit(&info->db);
        }
        if (!dir_emit(ctx, ent.name, strlen(ent.name), ent.ino, ent.type))
            break;
        ctx->pos = host_telldir(dir) + 1;
    }
    return res;
}

static int fakefs_dir_release(struct inode *ino, struct file *file) {
    if (FILE_DIR(file) != NULL)
        return host_closedir(FILE_DIR(file));
    return 0;
}

static const struct file_operations fakefs_file_fops = {
    .llseek = generic_file_llseek,
    .splice_read = generic_file_splice_read,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .mmap = generic_file_mmap,
    .release = fakefs_file_release,
    .fsync = fakefs_fsync,
};

static const struct file_operations fakefs_dir_fops = {
    .iterate = fakefs_iterate,
    .release = fakefs_dir_release,
};

/***** address space *****/

static int fakefs_readpage(struct file *file, struct page *page) {
    struct inode *inode = file ? file->f_inode : page->mapping->host;
    char *buffer = kmap(page);
    ssize_t res = host_pread(INODE_FD(inode), buffer, PAGE_SIZE, page_offset(page));
    if (res < 0) {
        ClearPageUptodate(page);
        SetPageError(page);
        goto out;
    }
    memset(buffer + res, 0, PAGE_SIZE - res);

    res = 0;
    ClearPageError(page);
    SetPageUptodate(page);

out:
    flush_dcache_page(page);
    kunmap(page);
    unlock_page(page);
    return res;
}

static int fakefs_writepage(struct page *page, struct writeback_control *wbc) {
    struct inode *inode = page->mapping->host;

    loff_t start = page_offset(page);
    int len = PAGE_SIZE;
    if (page->index > (inode->i_size >> PAGE_SHIFT))
        len = inode->i_size & (PAGE_SIZE - 1);

    void *buffer = kmap(page);
    ssize_t res = host_pwrite(INODE_FD(inode), buffer, len, start);
    kunmap(page);

    if (res != len) {
        ClearPageUptodate(page);
        goto out;
    }

    if (start + len > inode->i_size)
        inode->i_size = start + len;
    ClearPageError(page);
    res = 0;

out:
    unlock_page(page);
    return res;
}

static int fakefs_write_begin(struct file *file, struct address_space *mapping,
                              loff_t pos, unsigned len, unsigned flags,
                              struct page **pagep, void **fsdata) {
    pgoff_t index = pos >> PAGE_SHIFT;
    *pagep = grab_cache_page_write_begin(mapping, index, flags);
    if (!*pagep)
        return -ENOMEM;
    return 0;
}

/* copied from hostfs, I don't really know what it does or how it works */
static int fakefs_write_end(struct file *file, struct address_space *mapping,
                            loff_t pos, unsigned len, unsigned copied,
                            struct page *page, void *fsdata) {
    struct inode *inode = mapping->host;
    unsigned from = pos & (PAGE_SIZE - 1);

    void *buffer = kmap(page);
    ssize_t res = host_pwrite(INODE_FD(file->f_inode), buffer + from, copied, pos);
    kunmap(page);
    if (res < 0)
        goto out;

    if (!PageUptodate(page) && res == PAGE_SIZE)
        SetPageUptodate(page);

    pos += res;
    if (pos > inode->i_size)
        inode->i_size = pos;

out:
    unlock_page(page);
    put_page(page);
    return res;
}

static const struct address_space_operations fakefs_aops = {
    .readpage = fakefs_readpage,
    .writepage = fakefs_writepage,
    .set_page_dirty = __set_page_dirty_nobuffers,
    .write_begin = fakefs_write_begin,
    .write_end = fakefs_write_end,
};

static int restat_inode(struct inode *ino) {
    struct hostfs_stat host_stat;
    int err = stat_file(NULL, &host_stat, INODE_FD(ino));
    if (err < 0)
        return err;
    set_nlink(ino, host_stat.nlink);
    ino->i_size = host_stat.size;
    ino->i_blocks = host_stat.blocks;
    ino->i_atime.tv_sec = host_stat.atime.tv_sec;
    ino->i_atime.tv_nsec = host_stat.atime.tv_nsec;
    ino->i_ctime.tv_sec = host_stat.ctime.tv_sec;
    ino->i_ctime.tv_nsec = host_stat.ctime.tv_nsec;
    ino->i_mtime.tv_sec = host_stat.mtime.tv_sec;
    ino->i_mtime.tv_nsec = host_stat.mtime.tv_nsec;
    return 0;
}

static int read_inode(struct inode *ino) {
    struct fakefs_super *info = ino->i_sb->s_fs_info;
    struct ish_stat ishstat;
    inode_read_stat(&info->db, ino->i_ino, &ishstat);
    ino->i_mode = ishstat.mode;
    i_uid_write(ino, ishstat.uid);
    i_gid_write(ino, ishstat.gid);

    int err = restat_inode(ino);
    if (err < 0)
        return err;

    switch (ino->i_mode & S_IFMT) {
    case S_IFREG:
        ino->i_op = &fakefs_iops;
        ino->i_fop = &fakefs_file_fops;
        ino->i_mapping->a_ops = &fakefs_aops;
        break;
    case S_IFDIR:
        ino->i_op = &fakefs_dir_iops;
        ino->i_fop = &fakefs_dir_fops;
        break;
    case S_IFLNK:
        ino->i_op = &fakefs_link_iops;
        ino->i_mapping->a_ops = &fakefs_aops;
        inode_nohighmem(ino);
        break;
    case S_IFCHR:
    case S_IFBLK:
    case S_IFIFO:
    case S_IFSOCK:
        init_special_inode(ino, ino->i_mode & S_IFMT, ishstat.rdev);
        ino->i_op = &fakefs_iops;
        break;
    default:
        printk("read_inode: unexpected S_IFMT: %o\n", ino->i_mode & S_IFMT);
        return -EIO;
    }
    return 0;
}

static const struct dentry_operations fakefs_dops = {
    .d_delete = always_delete_dentry,
};

/***** superblock *****/

static void fakefs_evict_inode(struct inode *ino) {
    struct fakefs_super *info = ino->i_sb->s_fs_info;
    if (INODE_FD(ino) != info->root_fd && INODE_FD(ino) != -1)
        host_close(INODE_FD(ino));
    INODE_FD(ino) = -1;
    truncate_inode_pages_final(&ino->i_data);
    clear_inode(ino);
}

static int fakefs_statfs(struct dentry *dentry, struct kstatfs *kstat) {
    struct host_statfs stat;
    int err = host_fstatfs(INODE_FD(d_inode(dentry)), &stat);
    if (err < 0)
        return err;
    kstat->f_type = 0x66616b65;
    kstat->f_bsize = stat.bsize;
    kstat->f_frsize = stat.frsize;
    kstat->f_blocks = stat.blocks;
    kstat->f_bfree = stat.bfree;
    kstat->f_bavail = stat.bavail;
    kstat->f_files = stat.files;
    kstat->f_ffree = stat.ffree;
    kstat->f_fsid = u64_to_fsid(stat.fsid);
    kstat->f_namelen = stat.namemax;
    return 0;
}

static const struct super_operations fakefs_super_ops = {
    .drop_inode = generic_delete_inode,
    .evict_inode = fakefs_evict_inode,
    .statfs = fakefs_statfs,
};

static int fakefs_fill_super(struct super_block *sb, struct fs_context *fc) {
    struct fakefs_super *info = sb->s_fs_info;

    // https://lore.kernel.org/all/d9bcb237-39e1-29b1-9718-b720a7e7540b@collabora.com/T/
    int err = super_setup_bdi(sb);
    if (err < 0)
        return err;

    struct inode *root = new_inode(sb);
    if (root == NULL)
        return -ENOMEM;
    db_begin(&info->db);
    root->i_ino = path_get_inode(&info->db, "");
    if (root->i_ino == 0) {
        printk("fakefs: could not find root inode\n");
        db_rollback(&info->db);
        iput(root);
        return -EINVAL;
    }
    INODE_FD(root) = info->root_fd;
    err = read_inode(root);
    if (err < 0) {
        db_rollback(&info->db);
        iput(root);
        return err;
    }
    db_commit(&info->db);

    sb->s_op = &fakefs_super_ops;
    sb->s_d_op = &fakefs_dops;
    sb->s_root = d_make_root(root);
    if (sb->s_root == NULL) {
        iput(root);
        return -ENOMEM;
    }

    return 0;
}

/***** context/init *****/

struct fakefs_context {
    const char *path;
};

static int fakefs_fc_parse_param(struct fs_context *fc, struct fs_parameter *param) {
    struct fakefs_context *ctx = fc->fs_private;
    if (strcmp(param->key, "source") == 0) {
        ctx->path = kstrdup(param->string, GFP_KERNEL);
        if (ctx->path == NULL)
            return -ENOMEM;
    }
    return 0;
}

static void fakefs_fc_free(struct fs_context *fc) {
    struct fakefs_context *ctx = fc->fs_private;
    kfree(ctx->path);
    kfree(ctx);
}

static int fakefs_get_tree(struct fs_context *fc) {
    fc->s_fs_info = kzalloc(sizeof(struct fakefs_super), GFP_KERNEL);
    if (fc->s_fs_info == NULL)
        return -ENOMEM;
    struct fakefs_super *info = fc->s_fs_info;
    struct fakefs_context *ctx = fc->fs_private;

    char *path = kmalloc(strlen(ctx->path) + 10, GFP_KERNEL);
    strcpy(path, ctx->path);
    strcat(path, "/data");
    info->root_fd = host_open(path, O_RDONLY);
    if (info->root_fd < 0) {
        kfree(path);
        return info->root_fd;
    }

    strcpy(path, ctx->path);
    strcat(path, "/meta.db");
    int err = fake_db_init(&info->db, path, info->root_fd);
    if (err < 0) {
        kfree(path);
        return err;
    }
    kfree(path);

    err = vfs_get_super(fc, vfs_get_keyed_super, fakefs_fill_super);
    if (err < 0)
        return err;
    return 0;
}

static struct fs_context_operations fakefs_context_ops = {
    .parse_param = fakefs_fc_parse_param,
    .free = fakefs_fc_free,
    .get_tree = fakefs_get_tree,
};

static int fakefs_init_fs_context(struct fs_context *fc) {
    fc->ops = &fakefs_context_ops;
    fc->fs_private = kzalloc(sizeof(struct fakefs_context), GFP_KERNEL);
    if (fc->fs_private == NULL)
        return -ENOMEM;
    return 0;
}

static void fakefs_kill_sb(struct super_block *sb) {
    struct fakefs_super *info = sb->s_fs_info;
    fake_db_deinit(&info->db);
    host_close(info->root_fd);
    kill_anon_super(sb);
    kfree(info);
}

static struct file_system_type fakefs_type = {
    .name = "fakefs",
    .init_fs_context = fakefs_init_fs_context,
    .kill_sb = fakefs_kill_sb,
};

static int fakefs_init(void) {
    return register_filesystem(&fakefs_type);
}

fs_initcall(fakefs_init);
