#ifndef FS_FAKEFSIFY_H
#define FS_FAKEFSIFY_H

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

bool fakefsify(const char *archive_path, const char *fs, struct fakefsify_error *err_out);

#endif
