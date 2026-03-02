#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fs/sqlutil.h"
#include "fs/fake-db.h"
#include "kernel/errno.h"
#include "util/list.h"
#include "debug.h"

// rebuild process in pseudocode:
//
// table = {}
// for each path, inode:
//     real_inode = stat(path).st_ino
//     if inode in table:
//         unlink(path)
//         link(table[inode], path)
//     else:
//         table[inode] = path
//     stat = db['stat ' + inode]
//     new_db['inode ' + path] = real_inode
//     new_db['stat ' + real_inode] = stat

// ad hoc hashtable
struct entry {
    ino_t inode;
    char *path;
    struct list chain;
};

int fakefs_rebuild(struct fakefs_db *fs, int root_fd) {
    sqlite3 *db = fs->db;
    int err;

    EXEC("begin");
    EXEC("create table paths_old (path blob primary key, inode integer)");
    EXEC("create table stats_old (inode integer primary key, stat blob)");
    EXEC("insert into paths_old select * from paths");
    EXEC("insert into stats_old select * from stats");
    EXEC("delete from paths");
    EXEC("delete from stats");
    sqlite3_stmt *get_paths = PREPARE("select path, inode from paths_old");
    sqlite3_stmt *read_stat = PREPARE("select stat from stats_old where inode = ?");
    sqlite3_stmt *write_path = PREPARE("insert into paths (path, inode) values (?, ?)");
    sqlite3_stmt *write_stat = PREPARE("replace into stats (inode, stat) values (?, ?)");

    struct list hashtable[2000];
#define HASH_SIZE (sizeof(hashtable)/sizeof(hashtable[0]))
    for (unsigned i = 0; i < HASH_SIZE; i++)
        list_init(&hashtable[i]);

    while (STEP(get_paths)) {
        const char *path = (const char *) sqlite3_column_text(get_paths, 0);
        ino_t inode = sqlite3_column_int64(get_paths, 1);

        // grab real inode
        struct stat stat;
        int err = fstatat(root_fd, fix_path(path), &stat, 0);
        if (err < 0)
            continue;
        ino_t real_inode = stat.st_ino;

        // restore hardlinks
        struct list *bucket = &hashtable[inode % HASH_SIZE];
        struct entry *entry;
        bool found = false;
        list_for_each_entry(bucket, entry, chain) {
            if (entry->inode == inode) {
                unlinkat(root_fd, fix_path(path), 0);
                linkat(root_fd, fix_path(entry->path), root_fd, fix_path(path), 0);
                found = true;
                break;
            }
        }
        if (!found) {
            entry = malloc(sizeof(struct entry));
            entry->inode = inode;
            entry->path = strdup(path);
            list_add(bucket, &entry->chain);
        }

        // extract the stat so we can copy it
        err = sqlite3_bind_int64(read_stat, 1, inode); CHECK_ERR();
        if (STEP(read_stat) == false) {
            RESET(read_stat);
            continue;
        }
        const void *stat_data = sqlite3_column_blob(read_stat, 0);
        size_t stat_data_size = sqlite3_column_bytes(read_stat, 0);

        // store all the information in the new database
        err = sqlite3_bind_int64(write_stat, 1, real_inode); CHECK_ERR();
        err = sqlite3_bind_blob(write_stat, 2, stat_data, stat_data_size, SQLITE_TRANSIENT); CHECK_ERR();
        STEP(write_stat);
        RESET(write_stat);
        err = sqlite3_bind_blob(write_path, 1, path, strlen(path), SQLITE_TRANSIENT); CHECK_ERR();
        err = sqlite3_bind_int64(write_path, 2, real_inode); CHECK_ERR();
        STEP(write_path);
        RESET(write_path);

        RESET(read_stat);
    }

    for (unsigned i = 0; i < HASH_SIZE; i++) {
        struct entry *entry, *tmp;
        list_for_each_entry_safe(&hashtable[i], entry, tmp, chain) {
            list_remove(&entry->chain);
            free(entry->path);
            free(entry);
        }
    }

    EXEC("drop table paths_old");
    EXEC("drop table stats_old");
    EXEC("commit");
    FINALIZE(get_paths);
    FINALIZE(read_stat);
    FINALIZE(write_path);
    FINALIZE(write_stat);
    return 0;
}
