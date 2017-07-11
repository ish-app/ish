#include <string.h>

#include "sys/process.h"

// for now just collapses slashes, will eventually do something with . and ..
// TODO move to fs/pathname.c or something
void pathname_normalize(char *pathname) {
    char *s = pathname, *d = pathname;
    while (*s != '\0') {
        // copy up to a slash
        while (*s != '/' && *s != '\0')
            *d++ = *s++;
        // collapse slashes
        while (*s == '/')
            s++;
        // but make sure there's no slash at the end
        if (*s != '\0')
            *d++ = '/';
    }
    *d = '\0'; // finally
}

// turns the given path into an absolute path
// result is malloced and needs to be freed
char *pathname_expand(const char *pathname) {
    // this is not the most efficient way to do it, i'm aware, but it works and it's rare
    size_t full_path_len = strlen(pathname);
    if (pathname[0] != '/')
        full_path_len += strlen(current->pwd) + 1; // plus one for slash
    char *full_path = malloc(full_path_len + 1); full_path[0] = '\0';
    if (pathname[0] != '/') {
        strcat(full_path, current->pwd);
        strcat(full_path, "/");
    }
    strcat(full_path, pathname);
    pathname_normalize(full_path);
    return full_path;
}

