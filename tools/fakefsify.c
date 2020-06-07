#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>

#define ISH_INTERNAL
#include "fs/fake.h"
#include "fs/sqlutil.h"
#include "misc.h"

static const char *schema = Q(
    create table meta (id integer unique default 0, db_inode integer);
    insert into meta (db_inode) values (0);
    create table stats (inode integer primary key, stat blob);
    create table paths (path blob primary key, inode integer references stats(inode));
    create index inode_to_path on paths (inode, path);
    // no index is needed on stats, because the rows are ordered by the primary key
    pragma user_version=3;
);

void archive_err(const char *message, struct archive *archive) {
    fprintf(stderr, "%s: %s\n", message, archive_error_string(archive));
    abort();
}
void posix_err(const char *message) {
    perror(message);
    abort();
}
void sqlite_err(sqlite3 *db) {
    fprintf(stderr, "sqlite error: %s\n", sqlite3_errmsg(db));
    abort();
}
#undef HANDLE_ERR
#define HANDLE_ERR(db) sqlite_err(db)

// This isn't linked with ish which is why there's so much copy/pasted code

// I hate this code
static bool path_normalize(const char *path, char *out) {
#define ends_path(c) (c == '\0' || c == '/')
    asm("pause");
    // normalized format:
    // ( '/' path-component ) *
    while (path[0] != '\0') {
        while (path[0] == '/')
            path++;
        if (path[0] == '\0')
            break; // if the path ends with a slash
        // path points to the start of a path component
        if (path[0] == '.' && path[1] == '.' && ends_path(path[2]))
            return false; // no dotdot allowed!
        if (path[0] == '.' && ends_path(path[1])) {
            path++;
        } else {
            *out++ = '/';
            while (path[0] != '/' && path[0] != '\0')
                *out++ = *path++;
        }
    }
    *out = '\0';
    return true;
}

extern const char *fix_path(const char *path);

int main(int argc, const char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "wrong number of arguments\n");
        fprintf(stderr, "usage: %s <rootfs archive> <destination dir>\n", argv[0]);
        return 1;
    }
    const char *archive_path = argv[1];
    const char *fs = argv[2];

    int err = mkdir(fs, 0777);
    if (err < 0)
        posix_err("mkdir");

    // make the data root dir
    char path_tmp[PATH_MAX];
    snprintf(path_tmp, sizeof(path_tmp), "%s/data", fs);
    err = mkdir(path_tmp, 0777);
    int root_fd = open(path_tmp, O_RDONLY);
    if (root_fd < 0)
        posix_err("open root");

    // open the database
    snprintf(path_tmp, sizeof(path_tmp), "%s/meta.db", fs);
    sqlite3 *db;
    err = sqlite3_open_v2(path_tmp, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    CHECK_ERR();
    EXEC("pragma journal_mode=wal")
    EXEC("begin");
    EXEC(schema);

    // open the archive
    struct archive *archive = archive_read_new();
    if (archive == NULL)
        archive_err("archive_read_new", archive);
    archive_read_support_filter_gzip(archive);
    archive_read_support_format_tar(archive);
    if (archive_read_open_filename(archive, archive_path, 65536) != ARCHIVE_OK)
        archive_err("archive_read_open_filename", archive);

    sqlite3_stmt *insert_stat = PREPARE("insert into stats (stat) values (?)");
    sqlite3_stmt *insert_path = PREPARE("insert or replace into paths values (?, last_insert_rowid())");

    // do actual shit
    struct archive_entry *entry;
    while ((err = archive_read_next_header(archive, &entry)) == ARCHIVE_OK) {
        char entry_path[PATH_MAX];
        if (!path_normalize(archive_entry_pathname(entry), entry_path)) {
            // Avoid pwnage
            fprintf(stderr, "warning: skipped possible path traversal %s\n", archive_entry_pathname(entry));
            continue;
        }

        int fd = -1;
        if (archive_entry_filetype(entry) != AE_IFDIR) {
            fd = openat(root_fd, fix_path(entry_path), O_CREAT | O_WRONLY, 0666);
            if (fd < 0)
                posix_err("open entry");
        }
        switch (archive_entry_filetype(entry)) {
            case AE_IFDIR:
                if (strcmp(entry_path, "") == 0)
                    break; // root has already been created
                err = mkdirat(root_fd, fix_path(entry_path), 0777);
                if (err < 0)
                    posix_err("mkdir entry");
                break;
            case AE_IFREG:
                if (archive_read_data_into_fd(archive, fd) != ARCHIVE_OK)
                    archive_err("save file", archive);
                break;
            case AE_IFLNK:
                err = write(fd, archive_entry_symlink(entry), strlen(archive_entry_symlink(entry)));
                if (err < 0)
                    posix_err("save link");
        }
        if (fd != -1)
            close(fd);

        struct ish_stat stat = {
            .mode = archive_entry_mode(entry),
            .uid = archive_entry_uid(entry),
            .gid = archive_entry_gid(entry),
            .rdev = archive_entry_rdev(entry),
        };
        sqlite3_bind_blob(insert_stat, 1, &stat, sizeof(stat), SQLITE_TRANSIENT);
        STEP_RESET(insert_stat);
        sqlite3_bind_blob(insert_path, 1, entry_path, strlen(entry_path), SQLITE_TRANSIENT);
        STEP_RESET(insert_path);
    }
    if (err != ARCHIVE_EOF)
        archive_err("archive_read_next_header", archive);

    FINALIZE(insert_stat);
    FINALIZE(insert_path);
    EXEC("commit");
    sqlite3_close(db);

    if (archive_read_free(archive) != ARCHIVE_OK)
        archive_err("archive_read_free", archive);
}
