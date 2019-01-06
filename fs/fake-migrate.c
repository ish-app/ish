#include "kernel/fs.h"
#include "debug.h"
#include "kernel/errno.h"

// The value of the user_version pragma is used to decide what needs migrating.

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static struct migration {
    const char *sql;
    void (*migrate)(struct mount *mount);
} migrations[] = {
    // version 0 to version 1
    {
        "create index inode_to_path on paths (inode, path);"
    },
};

int fakefs_migrate(struct mount *mount) {
    int err;
#define CHECK_ERR() \
    if (err != SQLITE_OK && err != SQLITE_ROW && err != SQLITE_DONE) \
        die("sqlite error while migrating: %s\n", sqlite3_errmsg(mount->db));
#define EXEC(sql) \
    err = sqlite3_exec(mount->db, sql, NULL, NULL, NULL); \
    CHECK_ERR();
#define PREPARE(sql) ({ \
    sqlite3_stmt *stmt; \
    err = sqlite3_prepare_v2(mount->db, sql, -1, &stmt, NULL); \
    CHECK_ERR(); \
    stmt; \
})
#define STEP(stmt) ({ \
    err = sqlite3_step(stmt); \
    CHECK_ERR(); \
    err == SQLITE_ROW; \
})
#define RESET(stmt) \
    err = sqlite3_reset(stmt); \
    CHECK_ERR()
#define FINALIZE(stmt) \
    err = sqlite3_finalize(stmt); \
    CHECK_ERR()

    sqlite3_stmt *user_version = PREPARE("pragma user_version");
    STEP(user_version);
    int version = sqlite3_column_int(user_version, 0);
    FINALIZE(user_version);

    EXEC("begin");
    int versions = sizeof(migrations)/sizeof(migrations[0]);
    while (version < versions) {
        struct migration m = migrations[version];
        if (m.sql != NULL)
            EXEC(m.sql);
        if (m.migrate != NULL)
            m.migrate(mount);
        version++;
    }
    // for some reason placeholders aren't allowed in pragmas
    char *pragma_user_version = sqlite3_mprintf("pragma user_version = %d", version);
    EXEC(pragma_user_version);
    sqlite3_free(pragma_user_version);
    EXEC("commit");

    return 0;
}
