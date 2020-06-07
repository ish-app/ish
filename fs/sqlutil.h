#ifndef FS_SQLUTIL_H
#define FS_SQLUTIL_H
#include <sqlite3.h>

// Some nice sqlite macros for anything outside of fs/fake.c

#define Q(...) #__VA_ARGS__

#define HANDLE_ERR(db) \
    die("sqlite error while rebuilding: %s\n", sqlite3_errmsg(db))

#define CHECK_ERR() \
    if (err != SQLITE_OK && err != SQLITE_ROW && err != SQLITE_DONE) \
        HANDLE_ERR(db)
#define EXEC(sql) \
    err = sqlite3_exec(db, sql, NULL, NULL, NULL); \
    CHECK_ERR();
#define PREPARE(sql) ({ \
    sqlite3_stmt *stmt; \
    err = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL); \
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
#define STEP_RESET(stmt) \
    STEP(stmt); \
    RESET(stmt)
#define FINALIZE(stmt) \
    err = sqlite3_finalize(stmt); \
    CHECK_ERR()

#endif
