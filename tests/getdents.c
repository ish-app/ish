#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

int main() {
    int fd = open(".", O_RDONLY | O_DIRECTORY);
    char buf[100];
    int count = syscall(SYS_getdents64, fd, buf, sizeof(buf));
    write(STDOUT_FILENO, buf, count);
}
