#include <string.h>

#include "fs/path.h"
#include "misc.h"

void path_parse(char *path) {
    // this function is for absolute paths
    assert(*path == '/');

    char *d = path; // destination
    const char *s = path + 1; // source

    while (*s != '\0') {
        // copy one component
        while (*s != '/' && *s != '\0')
            *d++ = *s++;
        // turn any number of slashes into a null
        while (*s == '/') s++;
        *d++ = '\0';
    }
    *d++ = '\0'; // final null terminator
}

void path_stringify(char *path) {
    // just add a slash at the beginning and change all the nulls to slashes. ezpz
    char *d = path + path_length(path) - 1;
    const char *s = d - 1;
    while (path < d) {
        char c = *s--;
        if (c == '\0') c = '/';
        *d-- = c;
    }
    *d = '/';
}

size_t path_length(const char *path) {
    size_t len = 0;
    while (path[len] != '\0' && path[len+1] != '\0')
        len++;
    len++;
    return len;
}

char *path_dup(const char *path) {
    size_t path_len = path_length(path);
    char *new_path = malloc(path_len + 1);
    strncpy(new_path, path, path_len + 1);
    return new_path;
}

bool path_has_prefix(const char *path, const char *prefix) {
    const char *path_part, *prefix_part;
    for (path_part = path, prefix_part = prefix;
            *path_part != '\0' && *prefix_part != '\0';
            path_part += strlen(path_part) + 1, prefix_part += strlen(prefix_part) + 1) {
        if (strcmp(path_part, prefix_part) != 0) {
            return false;
        }
    }
    return *prefix_part == '\0';
}
