// Dumps the VDSO to a file called libvdso.so
#include <stdio.h>
#include <sys/auxv.h>

int main() {
    void *vdso = (void *) getauxval(AT_SYSINFO_EHDR);
    FILE *f = fopen("libvdso.so", "w");
    if (f == NULL) {
        perror("fopen libvdso.so");
        return 1;
    }
    fwrite(vdso, 0x2000, 1, f);
}
