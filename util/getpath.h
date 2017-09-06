#ifndef GETPATH_H
#define GETPATH_H

// Gets the path of the file descriptor. The argument must be a buffer of size MAX_PATH.
int getpath(int fd, char *buf);

#endif
