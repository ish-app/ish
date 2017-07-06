// Uses ptrace to overwrite the vdso of a running process.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/user.h>
#include <sys/ptrace.h>
#include "sys/elf.h"
#include "tools/ptutil.h"
#include "misc.h"

void transplant_vdso(int pid, const void *new_vdso, size_t new_vdso_size) {
    // get the vdso address and size from /proc/pid/maps
    char maps_file[32];
    sprintf(maps_file, "/proc/%d/maps", pid);
    FILE *maps = fopen(maps_file, "r");

    char line[256];
    dword_t start, end;
    while (fgets(line, sizeof(line), maps) != NULL) {
        char *map_type = NULL;
        sscanf(line, "%8x-%8x %*s %*s %*s %*s %ms\n", &start, &end, &map_type);
        if (strcmp(map_type, "[vdso]") == 0) {
            free(map_type);
            break;
        }
        free(map_type);
    }
    fclose(maps);

    // copy the new vdso in
    for (dword_t addr = start; addr < end; addr += sizeof(unsigned long)) {
        unsigned long new_vdso_data = 0;
        if (addr - start < new_vdso_size) {
            new_vdso_data = *(unsigned long *) ((char *) new_vdso + addr - start);
        }
        if (ptrace(PTRACE_POKEDATA, pid, addr, new_vdso_data) < 0) {
            perror("ptrace vdso poke"); exit(1);
        }
    }

    // find the entry point
    dword_t entry = *(dword_t *) ((char *) new_vdso + 24) + start;

    aux_write(pid, AX_SYSINFO, entry);
    aux_write(pid, AX_SYSINFO_EHDR, start);
}
