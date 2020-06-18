#ifndef FS_FAKEFSIFY_H
#define FS_FAKEFSIFY_H
#include <stdbool.h>

struct fakefsify_error {
    int line;
    enum {
        ERR_ARCHIVE,
        ERR_SQLITE,
        ERR_POSIX,
    } type;
    int code;
    char *message;
};

bool fakefs_import(const char *archive_path, const char *fs, struct fakefsify_error *err_out);
bool fakefs_export(const char *fs, const char *archive_path, struct fakefsify_error *err_out);

#endif
