#ifndef FS_PATH_H
#define FS_PATH_H

#define MAX_PATH 4096

// Normalizes the path specified and writes the result into the out buffer.
//
// Normalization means:
//  - prepending the current or root directory
//  - converting multiple slashes into one
//  - resolving . and ..
//  - resolving symlinks, skipping the last path component if the follow_links
//    argument is true
// The result will always begin with a slash.
//
// If the normalized path plus the null terminator would be longer than
// MAX_PATH, _ENAMETOOLONG is returned. The out buffer is expected to be at
// least MAX_PATH in size.
int path_normalize(const char *path, char *out, bool follow_links);

#endif
