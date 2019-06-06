#include <stdio.h>
#include <stdlib.h>

unsigned long fib(unsigned long n) {
    if (n <= 1)
        return 1;
    return fib(n - 1) + fib(n - 2);
}

int main(int argc, const char *argv[]) {
    if (argc != 2) {
        printf("argc is %d\n", argc);
        printf("please specify a number to fibbonaci\n");
        return 1;
    }
    unsigned long n = strtoul(argv[1], NULL, 10);
    printf("%lu\n", fib(n));
    return 0;
}

