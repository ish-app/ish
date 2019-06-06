#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <signal.h>

static char code[] = {
    0xb8, 0x01, 0x00, 0x00, 0x00, // movl $1, %eax
    0xc3, // ret
};

static sigjmp_buf env;
static int catching = 0;
static void handle_segfault(int sig) {
    if (catching) {
        catching = 0;
        siglongjmp(env, 1);
    }
}

static void test(char *code, const char *name) {
    printf("%-6s before: %d expected 1\n", name, ((int (*)()) code)());
    code[1] = 2;
    printf("%-6s after:  %d expected 2\n", name, ((int (*)()) code)());
}

int main() {
    signal(SIGSEGV, handle_segfault);
    void *code_copy = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    memcpy(code_copy, code, sizeof(code));

    test(code, "static");
    test(code_copy, "mmap");
    munmap(code_copy, 0x1000);
    printf("call nonexistent: ");
    catching = 1;
    if (!sigsetjmp(env, 1))
        printf("%d expected segfault\n", ((int (*)()) code_copy)());
    else
        printf("segfault\n");
    return 0;
}
