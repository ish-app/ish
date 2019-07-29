#ifndef PATH_H
#define PATH_H

#define AT_PWD (struct fd *) -2

#define N_SYMLINK_FOLLOW 1
#define N_SYMLINK_NOFOLLOW 2
#define N_PARENT_DIR_WRITE 4

// Normalizes the path specified and writes the result into the out buffer.
//
// Normalization means:
//  - prepending the current or root directory
//  - converting multiple slashes into one
//  - resolving . and ..
//  - resolving symlinks, skipping the last path component if the follow_links
//    argument is true
// The result will always begin with a slash or be empty.
//
// If the normalized path plus the null terminator would be longer than
// MAX_PATH, _ENAMETOOLONG is returned. The out buffer is expected to be at
// least MAX_PATH in size.
//
// at is the file descriptor to use as a base to interpret relative paths. If
// at is AT_PWD, uses current->pwd (with appropriate locking).
int path_normalize(struct fd *at, const char *path, char *out, int flags);
bool path_is_normalized(const char *path);

#endif
