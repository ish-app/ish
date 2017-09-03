#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void handler(int signal) {
    printf("caught signal %d\n", signal);
}

int main() {
    signal(SIGILL, handler);
    __asm__("ud2");
    printf("back in main\n");
}
