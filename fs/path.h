#ifndef PATH_H
#define PATH_H

#include <stddef.h>
#include <stdbool.h>

// A path is each path component as a null-terminated string, plus a final null
// terminating the whole thing.

// Parse or stringify a path, modifies the string in place.
// Parse may need path to have one free byte after the null terminator.
void path_parse(char *path);
void path_stringify(char *path);

size_t path_length(const char *path);
char *path_dup(const char *path);

bool path_has_prefix(const char *path, const char *prefix);

#define for_path(path, part) \
    for (const char *part = path; *part != '\0'; part += strlen(part) + 1)

#endif
