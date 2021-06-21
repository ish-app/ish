#include <string.h>
#include <sys/stat.h>
#include "kernel/errno.h"
#include "debug.h"
#include "misc.h"
#include "fs/fake-db.h"

static void db_check_error(struct fakefs_db *fs) {
    int errcode = sqlite3_errcode(fs->db);
    switch (errcode) {
        case SQLITE_OK:
        case SQLITE_ROW:
        case SQLITE_DONE:
            break;

        default:
            die("sqlite error: %s", sqlite3_errmsg(fs->db));
    }
}

static sqlite3_stmt *db_prepare(struct fakefs_db *fs, const char *stmt) {
    sqlite3_stmt *statement;
    sqlite3_prepare_v2(fs->db, stmt, strlen(stmt) + 1, &statement, NULL);
    db_check_error(fs);
    return statement;
}

bool db_exec(struct fakefs_db *fs, sqlite3_stmt *stmt) {
    int err = sqlite3_step(stmt);
    db_check_error(fs);
    return err == SQLITE_ROW;
}
void db_reset(struct fakefs_db *fs, sqlite3_stmt *stmt) {
    sqlite3_reset(stmt);
    db_check_error(fs);
}
void db_exec_reset(struct fakefs_db *fs, sqlite3_stmt *stmt) {
    db_exec(fs, stmt);
    db_reset(fs, stmt);
}

void db_begin(struct fakefs_db *fs) {
    sqlite3_mutex_enter(fs->lock);
    db_exec_reset(fs, fs->stmt.begin);
}
void db_commit(struct fakefs_db *fs) {
    db_exec_reset(fs, fs->stmt.commit);
    sqlite3_mutex_leave(fs->lock);
}
void db_rollback(struct fakefs_db *fs) {
    db_exec_reset(fs, fs->stmt.rollback);
    sqlite3_mutex_leave(fs->lock);
}

static void bind_path(sqlite3_stmt *stmt, int i, const char *path) {
    sqlite3_bind_blob(stmt, i, path, strlen(path), SQLITE_TRANSIENT);
}

inode_t path_get_inode(struct fakefs_db *fs, const char *path) {
    // select inode from paths where path = ?
    bind_path(fs->stmt.path_get_inode, 1, path);
    inode_t inode = 0;
    if (db_exec(fs, fs->stmt.path_get_inode))
        inode = sqlite3_column_int64(fs->stmt.path_get_inode, 0);
    db_reset(fs, fs->stmt.path_get_inode);
    return inode;
}
bool path_read_stat(struct fakefs_db *fs, const char *path, struct ish_stat *stat, inode_t *inode) {
    // select inode, stat from stats natural join paths where path = ?
    bind_path(fs->stmt.path_read_stat, 1, path);
    bool exists = db_exec(fs, fs->stmt.path_read_stat);
    if (exists) {
        if (inode)
            *inode = sqlite3_column_int64(fs->stmt.path_read_stat, 0);
        if (stat)
            *stat = *(struct ish_stat *) sqlite3_column_blob(fs->stmt.path_read_stat, 1);
    }
    db_reset(fs, fs->stmt.path_read_stat);
    return exists;
}
inode_t path_create(struct fakefs_db *fs, const char *path, struct ish_stat *stat) {
    // insert into stats (stat) values (?)
    sqlite3_bind_blob(fs->stmt.path_create_stat, 1, stat, sizeof(*stat), SQLITE_TRANSIENT);
    db_exec_reset(fs, fs->stmt.path_create_stat);
    inode_t inode = sqlite3_last_insert_rowid(fs->db);
    // insert or replace into paths values (?, last_insert_rowid())
    bind_path(fs->stmt.path_create_path, 1, path);
    db_exec_reset(fs, fs->stmt.path_create_path);
    return inode;
}

void inode_read_stat(struct fakefs_db *fs, inode_t inode, struct ish_stat *stat) {
    // select stat from stats where inode = ?
    sqlite3_bind_int64(fs->stmt.inode_read_stat, 1, inode);
    if (!db_exec(fs, fs->stmt.inode_read_stat))
        die("inode_read_stat(%llu): missing inode", (unsigned long long) inode);
    *stat = *(struct ish_stat *) sqlite3_column_blob(fs->stmt.inode_read_stat, 0);
    db_reset(fs, fs->stmt.inode_read_stat);
}
void inode_write_stat(struct fakefs_db *fs, inode_t inode, struct ish_stat *stat) {
    // update stats set stat = ? where inode = ?
    sqlite3_bind_blob(fs->stmt.inode_write_stat, 1, stat, sizeof(*stat), SQLITE_TRANSIENT);
    sqlite3_bind_int64(fs->stmt.inode_write_stat, 2, inode);
    db_exec_reset(fs, fs->stmt.inode_write_stat);
}

void path_link(struct fakefs_db *fs, const char *src, const char *dst) {
    inode_t inode = path_get_inode(fs, src);
    if (inode == 0)
        die("fakefs link(%s, %s): nonexistent src path", src, dst);
    // insert or replace into paths (path, inode) values (?, ?)
    bind_path(fs->stmt.path_link, 1, dst);
    sqlite3_bind_int64(fs->stmt.path_link, 2, inode);
    db_exec_reset(fs, fs->stmt.path_link);
}
inode_t path_unlink(struct fakefs_db *fs, const char *path) {
    inode_t inode = path_get_inode(fs, path);
    if (inode == 0)
        die("path_unlink(%s): nonexistent path", path);
    // delete from paths where path = ?
    bind_path(fs->stmt.path_unlink, 1, path);
    db_exec_reset(fs, fs->stmt.path_unlink);
    return inode;
}
void path_rename(struct fakefs_db *fs, const char *src, const char *dst) {
    // update or replace paths set path = change_prefix(path, ? [len(src)], ? [dst])
    //  where (path >= ? [src plus /] and path < [src plus 0]) or path = ? [src]
    // arguments:
    // 1. length of src
    // 2. dst
    // 3. src plus /
    // 4. src plus 0
    // 5. src
    size_t src_len = strlen(src);
    sqlite3_bind_int64(fs->stmt.path_rename, 1, src_len);
    bind_path(fs->stmt.path_rename, 2, dst);
    char src_extra[src_len + 1];
    memcpy(src_extra, src, src_len);
    src_extra[src_len] = '/';
    sqlite3_bind_blob(fs->stmt.path_rename, 3, src_extra, src_len + 1, SQLITE_TRANSIENT);
    src_extra[src_len] = '0';
    sqlite3_bind_blob(fs->stmt.path_rename, 4, src_extra, src_len + 1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(fs->stmt.path_rename, 5, src_extra, src_len, SQLITE_TRANSIENT);
    db_exec_reset(fs, fs->stmt.path_rename);
}

#if DEBUG_sql
static int trace_callback(unsigned UNUSED(why), void *UNUSED(fuck), void *stmt, void *_sql) {
    char *sql = _sql;
    printk("%d sql trace: %s %s\n", current ? current->pid : -1, sqlite3_expanded_sql(stmt), sql[0] == '-' ? sql : "");
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

extern int fakefs_rebuild(struct fakefs_db *fs, int root_fd);
extern int fakefs_migrate(struct fakefs_db *fs, int root_fd);

int fake_db_init(struct fakefs_db *fs, const char *db_path, int root_fd) {
    int err = sqlite3_open_v2(db_path, &fs->db, SQLITE_OPEN_READWRITE, NULL);
    if (err != SQLITE_OK) {
        printk("error opening database: %s\n", sqlite3_errmsg(fs->db));
        sqlite3_close(fs->db);
        return _EINVAL;
    }
    sqlite3_busy_timeout(fs->db, 1000);
    sqlite3_create_function(fs->db, "change_prefix", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC, NULL, sqlite_func_change_prefix, NULL, NULL);
    db_check_error(fs);

    // let's do WAL mode
    sqlite3_stmt *statement = db_prepare(fs, "pragma journal_mode=wal");
    db_check_error(fs);
    sqlite3_step(statement);
    db_check_error(fs);
    sqlite3_finalize(statement);

    statement = db_prepare(fs, "pragma foreign_keys=true");
    db_check_error(fs);
    sqlite3_step(statement);
    db_check_error(fs);
    sqlite3_finalize(statement);

#if DEBUG_sql
    sqlite3_trace_v2(mount->db, SQLITE_TRACE_STMT, trace_callback, NULL);
#endif

    err = fakefs_migrate(fs, root_fd);
    if (err < 0)
        return err;

    // after the filesystem is compressed, transmitted, and uncompressed, the
    // inode numbers will be different. to detect this, the inode of the
    // database file is stored inside the database and compared with the actual
    // database file inode, and if they're different we rebuild the database.
    struct stat statbuf;
    if (stat(db_path, &statbuf) < 0) ERRNO_DIE("stat database");
    ino_t db_inode = statbuf.st_ino;
    statement = db_prepare(fs, "select db_inode from meta");
    if (sqlite3_step(statement) == SQLITE_ROW) {
        if ((uint64_t) sqlite3_column_int64(statement, 0) != db_inode) {
            sqlite3_finalize(statement);
            statement = NULL;
            int err = fakefs_rebuild(fs, root_fd);
            if (err < 0) {
                return err;
            }
        }
    }
    if (statement != NULL)
        sqlite3_finalize(statement);

    // save current inode
    statement = db_prepare(fs, "update meta set db_inode = ?");
    sqlite3_bind_int64(statement, 1, (int64_t) db_inode);
    db_check_error(fs);
    sqlite3_step(statement);
    db_check_error(fs);
    sqlite3_finalize(statement);

    // delete orphaned stats
    statement = db_prepare(fs, "delete from stats where not exists (select 1 from paths where inode = stats.inode)");
    db_check_error(fs);
    sqlite3_step(statement);
    db_check_error(fs);
    sqlite3_finalize(statement);

    fs->lock = sqlite3_mutex_alloc(SQLITE_MUTEX_FAST);
    fs->stmt.begin = db_prepare(fs, "begin");
    fs->stmt.commit = db_prepare(fs, "commit");
    fs->stmt.rollback = db_prepare(fs, "rollback");
    fs->stmt.path_get_inode = db_prepare(fs, "select inode from paths where path = ?");
    fs->stmt.path_read_stat = db_prepare(fs, "select inode, stat from stats natural join paths where path = ?");
    fs->stmt.path_create_stat = db_prepare(fs, "insert into stats (stat) values (?)");
    fs->stmt.path_create_path = db_prepare(fs, "insert or replace into paths values (?, last_insert_rowid())");
    fs->stmt.inode_read_stat = db_prepare(fs, "select stat from stats where inode = ?");
    fs->stmt.inode_write_stat = db_prepare(fs, "update stats set stat = ? where inode = ?");
    fs->stmt.path_link = db_prepare(fs, "insert or replace into paths (path, inode) values (?, ?)");
    fs->stmt.path_unlink = db_prepare(fs, "delete from paths where path = ?");
    fs->stmt.path_rename = db_prepare(fs, "update or replace paths set path = change_prefix(path, ?, ?) "
            "where (path >= ? and path < ?) or path = ?");
    fs->stmt.path_from_inode = db_prepare(fs, "select path from paths where inode = ?");
    fs->stmt.try_cleanup_inode = db_prepare(fs, "delete from stats where inode = ? and not exists (select 1 from paths where inode = stats.inode)");
    return 0;
}

int fake_db_deinit(struct fakefs_db *fs) {
    if (fs->db) {
        sqlite3_finalize(fs->stmt.begin);
        sqlite3_finalize(fs->stmt.commit);
        sqlite3_finalize(fs->stmt.rollback);
        sqlite3_finalize(fs->stmt.path_get_inode);
        sqlite3_finalize(fs->stmt.path_read_stat);
        sqlite3_finalize(fs->stmt.path_create_stat);
        sqlite3_finalize(fs->stmt.path_create_path);
        sqlite3_finalize(fs->stmt.inode_read_stat);
        sqlite3_finalize(fs->stmt.inode_write_stat);
        sqlite3_finalize(fs->stmt.path_link);
        sqlite3_finalize(fs->stmt.path_unlink);
        sqlite3_finalize(fs->stmt.path_rename);
        sqlite3_finalize(fs->stmt.path_from_inode);
        sqlite3_finalize(fs->stmt.try_cleanup_inode);
        return sqlite3_close(fs->db);
    }
    return SQLITE_OK;
}
