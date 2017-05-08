// compile with cc -m32 -nostdlib
void _start() {
    char hello[] = "Hello, world!\n";
    long result;
    __asm__ volatile("int $0x80"
            : "=a" (result)
            : "a" ((long) 4), "b" ((long) 1), "c" ((long) hello), "d" ((long) sizeof(hello) - 1));
    __asm__ volatile("int $0x80"
            : "=a" (result)
            : "a" ((long) 1));
}
