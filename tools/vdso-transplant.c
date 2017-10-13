// Uses ptrace to overwrite the vdso of a running process.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/user.h>
#include <sys/ptrace.h>
#include "kernel/elf.h"
#include "tools/ptutil.h"
#include "misc.h"

static addr_t aux_addr(int pid, unsigned type) {
    struct user_regs_struct regs;
    trycall(ptrace(PTRACE_GETREGS, pid, NULL, &regs), "ptrace get sp for aux");
    dword_t sp = (dword_t) regs.rsp;
    // skip argc
    sp += 4;
    // skip argv
    while (pt_read(pid, sp) != 0)
        sp += 4;
    sp += 4;
    // skip envp
    while (pt_read(pid, sp) != 0)
        sp += 4;
    sp += 4;
    // dig through auxv
    dword_t aux_type;
    while ((aux_type = pt_read(pid, sp)) != 0) {
        sp += 4;
        if (aux_type == type) {
            return sp;
        }
        sp += 4;
    }
    return 0;
}

static void aux_write(int pid, int type, dword_t value) {
    return pt_write(pid, aux_addr(pid, type), value);
}

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
        if (map_type) {
            if (strcmp(map_type, "[vdso]") == 0) {
                free(map_type);
                break;
            }
            free(map_type);
        }
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
