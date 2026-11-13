// Self-modifying code, as a JIT compiler produces it. Every line must end in OK.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#define CHECK(name, cond) printf("%-44s %s\n", name, (cond) ? "OK" : "FAIL")
typedef long (*fn_t)(long);
// mov $imm32, %eax ; add %rdi, %rax ; ret
static void emit(unsigned char *p, uint32_t imm) { unsigned char code[] = {0xb8, 0, 0, 0, 0, 0x48, 0x01, 0xf8, 0xc3}; memcpy(code + 1, &imm, 4); memcpy(p, code, sizeof code); }
static unsigned char *page;
static void *patcher(void *arg) { emit(page + 512, (uint32_t) (uintptr_t) arg); return NULL; }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    page = mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    emit(page, 100);
    fn_t f = (fn_t) page;
    CHECK("generated code runs", f(5) == 105);
    int ok = 1;
    for (uint32_t i = 0; i < 20000; i++) { emit(page, i); if (f(1) != (long) i + 1) { ok = 0; break; } }
    CHECK("rewritten in place 20000 times", ok);
    // patch only the immediate, a single byte at a time
    ok = 1;
    for (int i = 0; i < 5000; i++) { page[1] = (unsigned char) i; page[2] = (unsigned char) (i >> 8); if (f(0) != (i & 0xffff)) { ok = 0; break; } }
    CHECK("single bytes patched", ok);
    // data and code on the same page, data written between calls
    volatile long *data = (volatile long *) (page + 2048);
    ok = 1;
    for (long i = 0; i < 20000; i++) { *data = i * 3; if (f(*data) != i * 3 + 4999) { ok = 0; break; } }
    CHECK("data writes next to code", ok);
    // a second function on the same page, patched from another thread
    emit(page + 512, 7);
    fn_t g = (fn_t) (page + 512);
    ok = g(1) == 8;
    for (uintptr_t i = 0; i < 300 && ok; i++) { pthread_t t; pthread_create(&t, NULL, patcher, (void *) (i + 1000)); pthread_join(t, NULL); if (g(0) != (long) i + 1000) ok = 0; }
    CHECK("patched by another thread", ok);
    // code that straddles a page boundary, then rewritten
    emit(page + 4096 - 4, 55);
    fn_t h = (fn_t) (page + 4096 - 4);
    ok = h(1) == 56;
    for (uint32_t i = 0; i < 3000 && ok; i++) { emit(page + 4096 - 4, i); if (h(2) != (long) i + 2) ok = 0; }
    CHECK("code across a page boundary", ok);
    // W^X style: write while not executable, flip, run
    unsigned char *wx = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ok = 1;
    for (uint32_t i = 0; i < 2000 && ok; i++) {
        mprotect(wx, 4096, PROT_READ | PROT_WRITE); emit(wx, i * 7); mprotect(wx, 4096, PROT_READ | PROT_EXEC);
        if (((fn_t) wx)(3) != (long) i * 7 + 3) ok = 0;
    }
    CHECK("write, mprotect, execute cycles", ok);
    // munmap and map something else at the same address
    munmap(wx, 4096);
    unsigned char *again = mmap(wx, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    emit(again, 31337);
    CHECK("remapped address runs the new code", ((fn_t) again)(0) == 31337);
    return 0;
}
