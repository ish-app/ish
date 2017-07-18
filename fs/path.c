#include <string.h>
#include "sys/calls.h"
#include "fs/path.h"

int path_normalize(const char *path, char *out) {
    // there's very likely an off-by-one error in the MAX_PATH logic that I
    // won't find for years because nobody creates files with 4k long names

    const char *p = path;
    char *o = out;
    int n = MAX_PATH;
    *o = '\0';

    // start with root or cwd, depending on whether it starts with a slash
    if (*p == '/') {
        strcpy(o, current->root);
        n -= strlen(current->root);
        o += strlen(current->root);
        // if it does start with a slash, make sure to skip all the slashes
        while (*p == '/')
            p++;
    } else {
        strcpy(o, current->pwd);
        n -= strlen(current->pwd);
        o += strlen(current->pwd);
    }

    while (*p != '\0') {
        if (n == 0)
            return _ENAMETOOLONG;

        // output a slash
        *o++ = '/';
        // copy up to a slash or null
        while (*p != '/' && *p != '\0' && --n > 0)
            *o++ = *p++;
        // eat any slashes
        while (*p == '/')
            p++;

        // TODO check if out points to a symlink at this point
    }

    *o = '\0';
    return 0;
}
