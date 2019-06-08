#include <stdio.h>

int main(int argc, const char *argv[]) {
    FILE *f = stdin;
    if (argc > 1)
        f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "cat: ");
        perror(argv[1]);
        return 1;
    }

    int c;
    while ((c = fgetc(f)) != EOF)
        putchar(c);
}
