// compile with cc -m32 -nostdlib
char hello[] = "Hello, world!\n";

void _start() {
    long result;
    __asm__ volatile("int $0x80"
            : "=a" (result)
            : "a" (4), "b" (1), "c" (hello), "d" (sizeof(hello) - 1));
    __asm__ volatile("int $0x80"
            : "=a" (result)
            : "a" (1), "b" (0));
}
