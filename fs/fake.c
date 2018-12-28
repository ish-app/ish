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

// TODO document database

struct ish_stat {
    dword_t mode;
    dword_t uid;
    dword_t gid;
    dword_t rdev;
};

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

static void db_begin(struct mount *mount) {
    lock(&mount->lock);
    db_exec_reset(mount, mount->stmt.begin);
}
static void db_commit(struct mount *mount) {
    db_exec_reset(mount, mount->stmt.commit);
    unlock(&mount->lock);
}
static void db_rollback(struct mount *mount) {
    db_exec_reset(mount, mount->stmt.rollback);
    unlock(&mount->lock);
}

static ino_t inode_for_path(struct mount *mount, const char *path) {
    struct stat stat;
    if (fstatat(mount->root_fd, fix_path(path), &stat, AT_SYMLINK_NOFOLLOW) < 0)
        // interestingly, both linux and darwin reserve inode number 0. linux
        // uses it for an error return and darwin uses it to mark deleted
        // directory entries (and maybe also for error returns, I don't know).
        return 0;
    return stat.st_ino;
}

static ino_t write_path(struct mount *mount, const char *path) {
    ino_t inode = inode_for_path(mount, path);
    if (inode != 0) {
        sqlite3_bind_blob(mount->stmt.write_path, 1, path, strlen(path), SQLITE_TRANSIENT);
        db_check_error(mount);
        sqlite3_bind_int64(mount->stmt.write_path, 2, inode);
        db_check_error(mount);
        db_exec_reset(mount, mount->stmt.write_path);
    }
    return inode;
}

static void delete_path(struct mount *mount, const char *path) {
    sqlite3_bind_blob(mount->stmt.delete_path, 1, path, strlen(path), SQLITE_TRANSIENT);
    db_check_error(mount);
    db_exec_reset(mount, mount->stmt.delete_path);
}

static bool read_stat(struct mount *mount, const char *path, struct ish_stat *stat) {
    ino_t inode = inode_for_path(mount, path);
    if (inode == 0)
        return false;
    sqlite3_bind_int64(mount->stmt.read_stat, 1, inode);
    db_check_error(mount);
    bool has_result = db_exec(mount, mount->stmt.read_stat);
    if (!has_result) {
        db_reset(mount, mount->stmt.read_stat);
        return false;
    }
    const struct ish_stat *db_stat = sqlite3_column_blob(mount->stmt.read_stat, 0);
    if (stat != NULL)
        *stat = *db_stat;
    db_reset(mount, mount->stmt.read_stat);
    return true;
}

static void write_stat(struct mount *mount, const char *path, struct ish_stat *stat) {
    ino_t inode = write_path(mount, path);
    assert(inode != 0);
    sqlite3_bind_int64(mount->stmt.write_stat, 1, inode);
    db_check_error(mount);
    sqlite3_bind_blob(mount->stmt.write_stat, 2, stat, sizeof(*stat), SQLITE_TRANSIENT);
    db_check_error(mount);
    db_exec_reset(mount, mount->stmt.write_stat);
}

static void delete_inode_stat(struct mount *mount, ino_t inode) {
    sqlite3_bind_int64(mount->stmt.delete_stat, 1, inode);
    db_check_error(mount);
    db_exec_reset(mount, mount->stmt.delete_stat);
}

static struct fd *fakefs_open(struct mount *mount, const char *path, int flags, int mode) {
    struct fd *fd = realfs.open(mount, path, flags, 0666);
    if (IS_ERR(fd))
        return fd;
    if (flags & O_CREAT_) {
        db_begin(mount);
        if (!read_stat(mount, path, NULL)) {
            struct ish_stat ishstat;
            ishstat.mode = mode | S_IFREG;
            ishstat.uid = current->euid;
            ishstat.gid = current->egid;
            ishstat.rdev = 0;
            write_stat(mount, path, &ishstat);
        }
        db_commit(mount);
    }
    return fd;
}

static int fakefs_link(struct mount *mount, const char *src, const char *dst) {
    db_begin(mount);
    int err = realfs.link(mount, src, dst);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    write_path(mount, dst);
    db_commit(mount);
    return 0;
}

static int fakefs_unlink(struct mount *mount, const char *path) {
    // find out if this is the last link
    bool gone = false;
    int fd = openat(mount->root_fd, fix_path(path), O_RDONLY);
    struct stat stat;
    if (fd >= 0 && fstat(fd, &stat) >= 0 && stat.st_nlink == 1)
        gone = true;
    if (fd >= 0)
        close(fd);

    db_begin(mount);
    ino_t inode = inode_for_path(mount, path);
    int err = realfs.unlink(mount, path);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    delete_path(mount, path);
    if (gone)
        delete_inode_stat(mount, inode);
    db_commit(mount);
    return 0;
}

static int fakefs_rmdir(struct mount *mount, const char *path) {
    db_begin(mount);
    ino_t inode = inode_for_path(mount, path);
    int err = realfs.rmdir(mount, path);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    delete_path(mount, path);
    delete_inode_stat(mount, inode);
    db_commit(mount);
    return 0;
}

static int fakefs_rename(struct mount *mount, const char *src, const char *dst) {
    db_begin(mount);
    // get the inode of the dst path
    ino_t old_dst_inode = inode_for_path(mount, dst);

    int err = realfs.rename(mount, src, dst);
    if (err < 0) {
        db_rollback(mount);
        return err;
    }
    write_path(mount, dst);
    delete_path(mount, src);
    // if this rename clobbered a file at the dst path, the metadata for that
    // file needs to be deleted
    if (old_dst_inode != 0 && old_dst_inode != inode_for_path(mount, dst))
        delete_inode_stat(mount, old_dst_inode);
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
    write_stat(mount, link, &ishstat);
    db_commit(mount);
    return 0;
}

static int fakefs_mknod(struct mount *mount, const char *path, mode_t_ mode, dev_t_ dev) {
    mode_t_ real_mode = 0666;
    if (S_ISBLK(mode) || S_ISCHR(mode))
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
    write_stat(mount, path, &stat);
    db_commit(mount);
    return err;
}

static int fakefs_stat(struct mount *mount, const char *path, struct statbuf *fake_stat, bool follow_links) {
    db_begin(mount);
    struct ish_stat ishstat;
    if (!read_stat(mount, path, &ishstat)) {
        db_rollback(mount);
        return _ENOENT;
    }
    int err = realfs.stat(mount, path, fake_stat, follow_links);
    db_commit(mount);
    if (err < 0)
        return err;
    fake_stat->mode = ishstat.mode;
    fake_stat->uid = ishstat.uid;
    fake_stat->gid = ishstat.gid;
    fake_stat->rdev = ishstat.rdev;
    return 0;
}

static int fakefs_fstat(struct fd *fd, struct statbuf *fake_stat) {
    // this is truly sad, but there is no alternative
    char path[MAX_PATH];
    int err = fd->mount->fs->getpath(fd, path);
    if (err < 0)
        return err;
    return fakefs_stat(fd->mount, path, fake_stat, false);
}

static int fakefs_setattr(struct mount *mount, const char *path, struct attr attr) {
    db_begin(mount);
    struct ish_stat ishstat;
    if (!read_stat(mount, path, &ishstat)) {
        db_rollback(mount);
        return _ENOENT;
    }
    switch (attr.type) {
        case attr_uid:
            ishstat.uid = attr.uid;
            break;
        case attr_gid:
            ishstat.gid = attr.gid;
            break;
        case attr_mode:
            ishstat.mode = (ishstat.mode & S_IFMT) | (attr.mode & ~S_IFMT);
            break;
        case attr_size:
            db_commit(mount);
            return realfs_truncate(mount, path, attr.size);
    }
    write_stat(mount, path, &ishstat);
    db_commit(mount);
    return 0;
}

static int fakefs_fsetattr(struct fd *fd, struct attr attr) {
    char path[MAX_PATH];
    int err = fd->mount->fs->getpath(fd, path);
    if (err < 0)
        return err;
    return fakefs_setattr(fd->mount, path, attr);
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
    write_stat(mount, path, &ishstat);
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
    if (!read_stat(mount, path, &ishstat)) {
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

int fakefs_rebuild(struct mount *mount);
int fakefs_migrate(struct mount *mount);

#if DEBUG_sql
static int trace_callback(unsigned why, void *fuck, void *stmt, void *sql) {
    printk("sql trace: %s\n", sqlite3_expanded_sql(stmt));
    return 0;
}
#endif

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

    // let's do WAL mode
    sqlite3_stmt *statement = db_prepare(mount, "pragma journal_mode=wal");
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
        if (sqlite3_column_int64(statement, 0) != db_inode) {
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
    sqlite3_bind_int64(statement, 1, db_inode);
    db_check_error(mount);
    sqlite3_step(statement);
    db_check_error(mount);
    sqlite3_finalize(statement);

    lock_init(&mount->lock);
    mount->stmt.begin = db_prepare(mount, "begin");
    mount->stmt.commit = db_prepare(mount, "commit");
    mount->stmt.rollback = db_prepare(mount, "rollback");
    mount->stmt.read_stat = db_prepare(mount, "select stat from stats where inode = ?");
    mount->stmt.write_stat = db_prepare(mount, "replace into stats (inode, stat) values (?, ?)");
    mount->stmt.delete_stat = db_prepare(mount, "delete from stats where inode = ?");
    mount->stmt.write_path = db_prepare(mount, "replace into paths (path, inode) values (?, ?)");
    mount->stmt.delete_path = db_prepare(mount, "delete from paths where inode = ?");

    return 0;
}

static int fakefs_umount(struct mount *mount) {
    if (mount->db)
        sqlite3_close(mount->db);
    /* return realfs.umount(mount); */
    return 0;
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

    .stat = fakefs_stat,
    .fstat = fakefs_fstat,
    .flock = realfs_flock,
    .setattr = fakefs_setattr,
    .fsetattr = fakefs_fsetattr,
    .getpath = realfs_getpath,
    .utime = realfs_utime,

    .mkdir = fakefs_mkdir,
    .rmdir = fakefs_rmdir,
};
