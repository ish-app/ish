#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void handler(int signal, siginfo_t *info, void *ucontext) {
    printf("caught signal %d code %d at %p\n", signal, info->si_code, info->si_addr);
}

int main() {
    struct sigaction act;
    act.sa_sigaction = handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &act, NULL);
    __asm__("ud2");
    printf("back in main\n");
}
