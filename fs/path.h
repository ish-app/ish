#ifndef PATH_H
#define PATH_H

#include <stddef.h>
#include <stdbool.h>

// A path is each path component as a null-terminated string, plus a final null
// terminating the whole thing.
typedef char *path_t;

// Parse or stringify a path, modifies the string in place.
// Parse may need path to have one free byte after the null terminator.
void path_parse(path_t path);
void path_stringify(path_t path);

size_t path_length(path_t path);
char *path_dup(path_t path);

bool path_has_prefix(path_t path, path_t prefix);

#define for_path(path, part) \
    for (const char *part = path; *part != '\0'; part += strlen(part) + 1)

#endif
