#include <unistd.h>
#include <fcntl.h>

#include "sys/fs.h"
#include "util/getpath.h"

#if defined(__linux__)
int getpath(int fd, char *buf) {
    char procfd[20];
    sprintf(procfd, "/proc/self/fd/%d", fd);
    ssize_t size = readlink(procfd, buf, MAX_PATH - 1);
    if (size < 0)
        return size;
    buf[MAX_PATH] = '\0';
    return 0;
}
#elif defined(__APPLE__)
int getpath(int fd, char *buf) {
    return fcntl(fd, F_GETPATH, buf);
}
#else
#error "getpath implementation missing"
#endif
