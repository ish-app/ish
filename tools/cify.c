// Reads a file and outputs a C file with a symbol containing all the data in the file.
#include <stdio.h>
#include <ctype.h>

int main(int argc, const char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s symbol input output.c output.h", argv[0]);
        return 1;
    }

    FILE *input = fopen(argv[2], "r");
    FILE *output = fopen(argv[3], "w");
    fprintf(output, "const char %s[] = \"", argv[1]);

    int ch;
    int size = 0;
    while ((ch = getc(input)) != EOF) {
        if (isprint(ch) && ch != '\\' && ch != '"') {
            putc(ch, output);
        } else switch (ch) {
            case '"': fputs("\\\"", output); break;
            case '\\': fputs("\\\\", output); break;
            case '\n': fputs("\\n", output); break;
            case '\r': fputs("\\r", output); break;
            default: fprintf(output, "\\%03o", ch); break;
        }
        size++;
    }

    int padding = 4096 - size % 4096;
    for (int i = 0; i < padding; i++) {
        fputs("\0", output);
    }

    fprintf(output, "\";\n");

    fclose(input);
    fclose(output);
    output = fopen(argv[4], "w");
    fprintf(output, "extern const char %s[%d];\n", argv[1], size + padding);
}
