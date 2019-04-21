#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sqlite3.h>

#include "debug.h"
#include "kernel/errno.h"
#include "kernel/task.h"
#include "fs/fd.h"
#include "fs/dev.h"
#include "fs/inode.h"
#define ISH_INTERNAL
#include "fs/fake.h"

// TODO document database

static void db_check_error(struct mount *mount) {
    int errcode = sqlite3_errcode(mount->db);
    switch (errcode) {
        case SQLITE_OK:
        case SQLITE_ROW:
        case SQLITE_DONE:
            break;

        default:
            die("sqlite error: %s", sqlite3_errmsg(mount->db));
    }
}

static sqlite3_stmt *db_prepare(struct mount *mount, const char *stmt) {
    sqlite3_stmt *statement;
    sqlite3_prepare_v2(mount->db, stmt, strlen(stmt) + 1, &statement, NULL);
    db_check_error(mount);
    return statement;
}

static bool db_exec(struct mount *mount, sqlite3_stmt *stmt) {
    int err = sqlite3_step(stmt);
    db_check_error(mount);
    return err == SQLITE_ROW;
}
static void db_reset(struct mount *mount, sqlite3_stmt *stmt) {
    sqlite3_reset(stmt);
    db_check_error(mount);
}
static void db_exec_reset(struct mount *mount, sqlite3_stmt *stmt) {
    db_exec(mount, stmt);
    db_reset(mount, stmt);
}

void db_begin(struct mount *mount) {
    lock(&mount->lock);
    db_exec_reset(mount, mount->stmt.begin);
}
void db_commit(struct mount *mount) {
    db_exec_reset(mount, mount->stmt.commit);
    unlock(&mount->lock);
}
void db_rollback(struct mount *mount) {
    db_exec_reset(mount, mount->stmt.rollback);
    unlock(&mount->lock);
}

static void bind_path(sqlite3_stmt *stmt, int i, const char *path) {
    sqlite3_bind_blob(stmt, i, path, strlen(path), SQLITE_TRANSIENT);
}

static void try_cleanup_inode(struct mount *mount, ino_t inode) {
    sqlite3_bind_int64(mount->stmt.try_cleanup_inode, 1, inode);
    db_exec_reset(mount, mount->stmt.try_cleanup_inode);
}

ino_t path_get_inode(struct mount *mount, const char *path) {
    // select inode from paths where path = ?
    bind_path(mount->stmt.path_get_inode, 1, path);
    ino_t inode = 0;
    if (db_exec(mount, mount->stmt.path_get_inode))
        inode = sqlite3_column_int64(mount->stmt.path_get_inode, 0);
    db_reset(mount, mount->stmt.path_get_inode);
    return inode;
}
bool path_read_stat(struct mount *mount, const char *path, struct ish_stat *stat, ino_t *inode) {
    // select inode, stat from stats natural join paths where path = ?
    bind_path(mount->stmt.path_read_stat, 1, path);
    bool exists = db_exec(mount, mount->stmt.path_read_stat);
    if (exists) {
        if (inode)
            *inode = sqlite3_column_int64(mount->stmt.path_read_stat, 0);
        if (stat)
            *stat = *(struct ish_stat *) sqlite3_column_blob(mount->stmt.path_read_stat, 1);
    }
    db_reset(mount, mount->stmt.path_read_stat);
    return exists;
}
void path_create(struct mount *mount, const char *path, struct ish_stat *stat) {
    // insert into stats (stat) values (?)
    sqlite3_bind_blob(mount->stmt.path_create_stat, 1, stat, sizeof(*stat), SQLITE_TRANSIENT);
    db_exec_reset(mount, mount->stmt.path_create_stat);
    // insert or replace into paths values (?, last_insert_rowid())
    bind_path(mount->stmt.path_create_path, 1, path);
    db_exec_reset(mount, mount->stmt.path_create_path);
}

static void inode_read_stat(struct mount *mount, ino_t inode, struct ish_stat *stat) {
    // select stat from stats where inode = ?
    sqlite3_bind_int64(mount->stmt.inode_read_stat, 1, inode);
    if (!db_exec(mount, mount->stmt.inode_read_stat))
        die("inode_read_stat(%llu): missing inode", (unsigned long long) inode);
    *stat = *(struct ish_stat *) sqlite3_column_blob(mount->stmt.inode_read_stat, 0);
    db_reset(mount, mount->stmt.inode_read_stat);
}
static void inode_write_stat(struct mount *mount, ino_t inode, struct ish_stat *stat) {
    // update stats set stat = ? where inode = ?
    sqlite3_bind_blob(mount->stmt.inode_write_stat, 1, stat, sizeof(*stat), SQLITE_TRANSIENT);
    sqlite3_bind_int64(mount->stmt.inode_write_stat, 2, inode);
    db_exec_reset(mount, mount->stmt.inode_write_stat);
}

static void path_link(struct mount *mount, const char *src, const char *dst) {
    ino_t inode = path_get_inode(mount, src);
    if (inode == 0)
        die("fakefs link(%s, %s): nonexistent src path", src, dst);
    // insert or replace into paths (path, inode) values (?, ?)
    bind_path(mount->stmt.path_link, 1, dst);
    sqlite3_bind_int64(mount->stmt.path_link, 2, inode);
    db_exec_reset(mount, mount->stmt.path_link);
}
static void path_unlink(struct mount *mount, const char *path) {
    ino_t inode = path_get_inode(mount, path);
    if (inode == 0)
        die("path_unlink(%s): nonexistent path", path);
    // delete from paths where path = ?
    bind_path(mount->stmt.path_unlink, 1, path);
    db_exec_reset(mount, mount->stmt.path_unlink);
    if (inode_is_orphaned(mount, inode))
        try_cleanup_inode(mount, inode);
}
static void path_rename(struct mount *mount, const char *src, const char *dst) {
    // update or replace paths set path = change_prefix(path, ? [len(src)], ? [dst])
    //  where (path >= ? [src plus /] and path < [src plus 0]) or path = ? [src]
    // arguments:
    // 1. length of src
    // 2. dst
    // 3. src plus /
    // 4. src plus 0
    // 5. src
    size_t src_len = strlen(src);
    sqlite3_bind_int64(mount->stmt.path_rename, 1, src_len);
    bind_path(mount->stmt.path_rename, 2, dst);
    char src_extra[src_len + 1];
    memcpy(src_extra, src, src_len);
    src_extra[src_len] = '/';
    sqlite3_bind_blob(mount->stmt.path_rename, 3, src_extra, src_len + 1, SQLITE_TRANSIENT);
    src_extra[src_len] = '0';
    sqlite3_bind_blob(mount->stmt.path_rename, 4, src_extra, src_len + 1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(mount->stmt.path_rename, 5, src_extra, src_len, SQLITE_TRANSIENT);
    db_exec_reset(mount, mount->stmt.path_rename);
}

// this exists only to override readdir to fix the returned inode numbers
static struct fd_ops fakefs_fdops;

static struct fd *fakefs_open(struct mount *mount, const char *path, int flags, int mode) {
    struct fd *fd = realfs.open(mount, path, flags, 0666);
    if (IS_ERR(fd))
        return fd;
    db_begin(mount);
    fd->fake_inode = path_get_inode(mount, path);
    if (flags & O_CREAT_) {
        struct ish_stat ishstat;
        ishstat.mode = mode | S_IFREG;
        ishstat.uid = current->euid;
        ishstat.gid = current->egid;
        ishstat.rdev = 0;
        if (fd->fake_inode == 0) {
            path_create(mount, path, &ishstat);
            fd->fake_inode = path_get_inode(mount, path);
        }
    }
    db_commit(mount);
    if (fd->fake_inode == 0) {
        // metadata for this file is missing
        // TODO unlink the real file
        fd_close(fd);
        return ERR_PTR(_ENOENT);
    }
    fd->ops = &fakefs_fdops;
    return fd;
}

// WARNING: giant hack, just for file providerws
struct fd *fakefs_open_inode(struct mount *mount, ino_t inode) {
    db_begin(mount);
    sqlite3_stmt *stmt = mount->stmt.path_from_inode;
    sqlite3_bind_int64(stmt, 1, inode);
step:
    if (!db_exec(mount, stmt)) {
        db_reset(mount, stmt);
        db_rollback(mount);
        return ERR_PTR(_ENOENT);
    }
    const char *path = (const char *) sqlite3_column_text(stmt, 0);
    struct fd *fd = realfs.open(mount, path, O_RDWR_, 0);
    if (PTR_ERR(fd) == _EISDIR)
        fd = realfs.open(mount, path, O_RDONLY_, 0);
    if (PTR_ERR(fd) == _ENOENT)
        goto step;
    db_reset(mount, stmt);
    db_commit(mount);
    fd->fake_inode = inode;
    fd->ops = &fakefs_fdops;
    return fd;
}

static int fakefs_link(struct mount *mount, const char *src, const char *dst) {
    db_begin(mount);
    int err = realfs.link(mount, src, dst);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    path_link(mount, src, dst);
    db_commit(mount);
    return 0;
}

static int fakefs_unlink(struct mount *mount, const char *path) {
    db_begin(mount);
    int err = realfs.unlink(mount, path);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    path_unlink(mount, path);
    db_commit(mount);
    return 0;
}

static int fakefs_rmdir(struct mount *mount, const char *path) {
    db_begin(mount);
    int err = realfs.rmdir(mount, path);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    path_unlink(mount, path);
    db_commit(mount);
    return 0;
}

static int fakefs_rename(struct mount *mount, const char *src, const char *dst) {
    db_begin(mount);
    path_rename(mount, src, dst);
    int err = realfs.rename(mount, src, dst);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    db_commit(mount);
    return 0;
}

static int fakefs_symlink(struct mount *mount, const char *target, const char *link) {
    db_begin(mount);
    // create a file containing the target
    int fd = openat(mount->root_fd, fix_path(link), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        db_rollback(mount);
        return errno_map();
    }
    ssize_t res = write(fd, target, strlen(target));
    close(fd);
    if (res < 0) {
        int saved_errno = errno;
        unlinkat(mount->root_fd, fix_path(link), 0);
        db_rollback(mount);
        errno = saved_errno;
        return errno_map();
    }

    // customize the stat info so it looks like a link
    struct ish_stat ishstat;
    ishstat.mode = S_IFLNK | 0777; // symlinks always have full permissions
    ishstat.uid = current->euid;
    ishstat.gid = current->egid;
    ishstat.rdev = 0;
    path_create(mount, link, &ishstat);
    db_commit(mount);
    return 0;
}

static int fakefs_mknod(struct mount *mount, const char *path, mode_t_ mode, dev_t_ dev) {
    mode_t_ real_mode = 0666;
    if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISSOCK(mode))
        real_mode |= S_IFREG;
    else
        real_mode |= mode & S_IFMT;
    db_begin(mount);
    int err = realfs.mknod(mount, path, real_mode, 0);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    struct ish_stat stat;
    stat.mode = mode;
    stat.uid = current->euid;
    stat.gid = current->egid;
    stat.rdev = 0;
    if (S_ISBLK(mode) || S_ISCHR(mode))
        stat.rdev = dev;
    path_create(mount, path, &stat);
    db_commit(mount);
    return err;
}

static int fakefs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat, bool follow_links) {
    db_begin(mount);
    struct ish_stat ishstat;
    ino_t inode;
    if (!path_read_stat(mount, path, &ishstat, &inode)) {
        db_rollback(mount);
        return _ENOENT;
    }
    int err = realfs.stat(mount, path, fake_stat, follow_links);
    db_commit(mount);
    if (err < 0)
        return err;
    fake_stat->inode = inode;
    fake_stat->mode = ishstat.mode;
    fake_stat->uid = ishstat.uid;
    fake_stat->gid = ishstat.gid;
    fake_stat->rdev = ishstat.rdev;
    return 0;
}

static int fakefs_fstat(struct fd *fd, struct statbuf *fake_stat) {
    int err = realfs.fstat(fd, fake_stat);
    if (err < 0)
        return err;
    db_begin(fd->mount);
    struct ish_stat ishstat;
    inode_read_stat(fd->mount, fd->fake_inode, &ishstat);
    db_commit(fd->mount);
    fake_stat->inode = fd->fake_inode;
    fake_stat->mode = ishstat.mode;
    fake_stat->uid = ishstat.uid;
    fake_stat->gid = ishstat.gid;
    fake_stat->rdev = ishstat.rdev;
    return 0;
}

static void fake_stat_setattr(struct ish_stat *ishstat, struct attr attr) {
    switch (attr.type) {
        case attr_uid:
            ishstat->uid = attr.uid;
            break;
        case attr_gid:
            ishstat->gid = attr.gid;
            break;
        case attr_mode:
            ishstat->mode = (ishstat->mode & S_IFMT) | (attr.mode & ~S_IFMT);
            break;
    }
}

static int fakefs_setattr(struct mount *mount, const char *path, struct attr attr) {
    if (attr.type == attr_size)
        return realfs.setattr(mount, path, attr);
    db_begin(mount);
    struct ish_stat ishstat;
    ino_t inode;
    if (!path_read_stat(mount, path, &ishstat, &inode)) {
        db_rollback(mount);
        return _ENOENT;
    }
    fake_stat_setattr(&ishstat, attr);
    inode_write_stat(mount, inode, &ishstat);
    db_commit(mount);
    return 0;
}

static int fakefs_fsetattr(struct fd *fd, struct attr attr) {
    if (attr.type == attr_size)
        return realfs.fsetattr(fd, attr);
    db_begin(fd->mount);
    struct ish_stat ishstat;
    inode_read_stat(fd->mount, fd->fake_inode, &ishstat);
    fake_stat_setattr(&ishstat, attr);
    inode_write_stat(fd->mount, fd->fake_inode, &ishstat);
    db_commit(fd->mount);
    return 0;
}

static int fakefs_mkdir(struct mount *mount, const char *path, mode_t_ mode) {
    db_begin(mount);
    int err = realfs.mkdir(mount, path, 0777);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    struct ish_stat ishstat;
    ishstat.mode = mode | S_IFDIR;
    ishstat.uid = current->euid;
    ishstat.gid = current->egid;
    ishstat.rdev = 0;
    path_create(mount, path, &ishstat);
    db_commit(mount);
    return 0;
}

static ssize_t file_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    // broken symlinks can't be included in an iOS app or else Xcode craps out
    int fd = openat(mount->root_fd, fix_path(path), O_RDONLY);
    if (fd < 0)
        return errno_map();
    int err = read(fd, buf, bufsize);
    close(fd);
    if (err < 0)
        return errno_map();
    return err;
}

static ssize_t fakefs_readlink(struct mount *mount, const char *path, char *buf, size_t bufsize) {
    db_begin(mount);
    struct ish_stat ishstat;
    if (!path_read_stat(mount, path, &ishstat, NULL)) {
        db_rollback(mount);
        return _ENOENT;
    }
    if (!S_ISLNK(ishstat.mode)) {
        db_rollback(mount);
        return _EINVAL;
    }

    ssize_t err = realfs.readlink(mount, path, buf, bufsize);
    if (err == _EINVAL)
        err = file_readlink(mount, path, buf, bufsize);
    db_commit(mount);
    return err;
}

static int fakefs_readdir(struct fd *fd, struct dir_entry *entry) {
    assert(fd->ops == &fakefs_fdops);
    int res;
retry:
    res = realfs_fdops.readdir(fd, entry);
    if (res <= 0)
        return res;

    // this is annoying
    char entry_path[MAX_PATH + 1];
    realfs_getpath(fd, entry_path);
    if (strcmp(entry->name, "..") == 0) {
        if (strcmp(entry_path, "") != 0) {
            *strrchr(entry_path, '/') = '\0';
        }
    } else if (strcmp(entry->name, ".") != 0) {
        // god I don't know what to do if this would overflow
        strcat(entry_path, "/");
        strcat(entry_path, entry->name);
    }

    db_begin(fd->mount);
    entry->inode = path_get_inode(fd->mount, entry_path);
    db_commit(fd->mount);
    // it's quite possible that due to some mishap there's no metadata for this file
    // so just skip this entry, instead of crashing the program, so there's hope for recovery
    if (entry->inode == 0)
        goto retry;
    return res;
}

static struct fd_ops fakefs_fdops;
static void __attribute__((constructor)) init_fake_fdops() {
    fakefs_fdops = realfs_fdops;
    fakefs_fdops.readdir = fakefs_readdir;
}

int fakefs_rebuild(struct mount *mount);
int fakefs_migrate(struct mount *mount);

#if DEBUG_sql
static int trace_callback(unsigned UNUSED(why), void *UNUSED(fuck), void *stmt, void *_sql) {
    char *sql = _sql;
    printk("sql trace: %s %s\n", sqlite3_expanded_sql(stmt), sql[0] == '-' ? sql : "");
    return 0;
}
#endif

static void sqlite_func_change_prefix(sqlite3_context *context, int argc, sqlite3_value **args) {
    assert(argc == 3);
    const void *in_blob = sqlite3_value_blob(args[0]);
    size_t in_size = sqlite3_value_bytes(args[0]);
    size_t start = sqlite3_value_int64(args[1]);
    const void *replacement = sqlite3_value_blob(args[2]);
    size_t replacement_size = sqlite3_value_bytes(args[2]);
    size_t out_size = in_size - start + replacement_size;
    char *out_blob = sqlite3_malloc(out_size);
    memcpy(out_blob, replacement, replacement_size);
    memcpy(out_blob + replacement_size, in_blob + start, in_size - start);
    sqlite3_result_blob(context, out_blob, out_size, sqlite3_free);
}

static int fakefs_mount(struct mount *mount) {
    char db_path[PATH_MAX];
    strcpy(db_path, mount->source);
    char *basename = strrchr(db_path, '/') + 1;
    assert(strcmp(basename, "data") == 0);
    strcpy(basename, "meta.db");

    // check if it is in fact a sqlite database
    char buf[16] = {};
    int dbf = open(db_path, O_RDONLY);
    if (dbf < 0)
        return errno_map();
    read(dbf, buf, sizeof(buf));
    close(dbf);
    if (strncmp(buf, "SQLite format 3", 15) != 0)
        return _EINVAL;

    int err = sqlite3_open_v2(db_path, &mount->db, SQLITE_OPEN_READWRITE, NULL);
    if (err != SQLITE_OK) {
        printk("error opening database: %s\n", sqlite3_errmsg(mount->db));
        sqlite3_close(mount->db);
        return _EINVAL;
    }
    sqlite3_busy_timeout(mount->db, 1000);
    sqlite3_create_function(mount->db, "change_prefix", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC, NULL, sqlite_func_change_prefix, NULL, NULL);
    db_check_error(mount);

    // let's do WAL mode
    sqlite3_stmt *statement = db_prepare(mount, "pragma journal_mode=wal");
    db_check_error(mount);
    sqlite3_step(statement);
    db_check_error(mount);
    sqlite3_finalize(statement);

    statement = db_prepare(mount, "pragma foreign_keys=true");
    db_check_error(mount);
    sqlite3_step(statement);
    db_check_error(mount);
    sqlite3_finalize(statement);

#if DEBUG_sql
    sqlite3_trace_v2(mount->db, SQLITE_TRACE_STMT, trace_callback, NULL);
#endif

    // do this now so fakefs_rebuild can use mount->root_fd
    err = realfs.mount(mount);
    if (err < 0)
        return err;

    err = fakefs_migrate(mount);
    if (err < 0)
        return err;

    // after the filesystem is compressed, transmitted, and uncompressed, the
    // inode numbers will be different. to detect this, the inode of the
    // database file is stored inside the database and compared with the actual
    // database file inode, and if they're different we rebuild the database.
    struct stat statbuf;
    if (stat(db_path, &statbuf) < 0) ERRNO_DIE("stat database");
    ino_t db_inode = statbuf.st_ino;
    statement = db_prepare(mount, "select db_inode from meta");
    if (sqlite3_step(statement) == SQLITE_ROW) {
        if ((uint64_t) sqlite3_column_int64(statement, 0) != db_inode) {
            sqlite3_finalize(statement);
            statement = NULL;
            int err = fakefs_rebuild(mount);
            if (err < 0) {
                close(mount->root_fd);
                return err;
            }
        }
    }
    if (statement != NULL)
        sqlite3_finalize(statement);

    // save current inode
    statement = db_prepare(mount, "update meta set db_inode = ?");
    sqlite3_bind_int64(statement, 1, (int64_t) db_inode);
    db_check_error(mount);
    sqlite3_step(statement);
    db_check_error(mount);
    sqlite3_finalize(statement);

    // delete orphaned stats
    statement = db_prepare(mount, "delete from stats where not exists (select 1 from paths where inode = stats.inode)");
    db_check_error(mount);
    sqlite3_step(statement);
    db_check_error(mount);
    sqlite3_finalize(statement);

    lock_init(&mount->lock);
    mount->stmt.begin = db_prepare(mount, "begin");
    mount->stmt.commit = db_prepare(mount, "commit");
    mount->stmt.rollback = db_prepare(mount, "rollback");
    mount->stmt.path_get_inode = db_prepare(mount, "select inode from paths where path = ?");
    mount->stmt.path_read_stat = db_prepare(mount, "select inode, stat from stats natural join paths where path = ?");
    mount->stmt.path_create_stat = db_prepare(mount, "insert into stats (stat) values (?)");
    mount->stmt.path_create_path = db_prepare(mount, "insert or replace into paths values (?, last_insert_rowid())");
    mount->stmt.inode_read_stat = db_prepare(mount, "select stat from stats where inode = ?");
    mount->stmt.inode_write_stat = db_prepare(mount, "update stats set stat = ? where inode = ?");
    mount->stmt.path_link = db_prepare(mount, "insert or replace into paths (path, inode) values (?, ?)");
    mount->stmt.path_unlink = db_prepare(mount, "delete from paths where path = ?");
    mount->stmt.path_rename = db_prepare(mount, "update or replace paths set path = change_prefix(path, ?, ?) "
            "where (path >= ? and path < ?) or path = ?");
    mount->stmt.path_from_inode = db_prepare(mount, "select path from paths where inode = ?");
    mount->stmt.try_cleanup_inode = db_prepare(mount, "delete from stats where inode = ? and not exists (select 1 from paths where inode = stats.inode)");

    return 0;
}

static int fakefs_umount(struct mount *mount) {
    if (mount->db)
        sqlite3_close(mount->db);
    /* return realfs.umount(mount); */
    return 0;
}

static void fakefs_inode_orphaned(struct mount *mount, struct inode_data *inode) {
    db_begin(mount);
    try_cleanup_inode(mount, inode->number);
    db_commit(mount);
}

const struct fs_ops fakefs = {
    .magic = 0x66616b65,
    .mount = fakefs_mount,
    .umount = fakefs_umount,
    .statfs = realfs_statfs,
    .open = fakefs_open,
    .readlink = fakefs_readlink,
    .link = fakefs_link,
    .unlink = fakefs_unlink,
    .rename = fakefs_rename,
    .symlink = fakefs_symlink,
    .mknod = fakefs_mknod,

    .close = realfs_close,
    .stat = fakefs_stat,
    .fstat = fakefs_fstat,
    .flock = realfs_flock,
    .setattr = fakefs_setattr,
    .fsetattr = fakefs_fsetattr,
    .getpath = realfs_getpath,
    .utime = realfs_utime,

    .mkdir = fakefs_mkdir,
    .rmdir = fakefs_rmdir,

    .inode_orphaned = fakefs_inode_orphaned,
};
