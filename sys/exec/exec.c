#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "emu/memory.h"
#include "emu/process.h"
#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/exec/elf.h"

#define ERRNO_FAIL(label) { \
    err = err_map(errno); \
    goto label; \
}

int sys_execve(const char *file, char *const argv[], char *const envp[]) {
    int err = 0;
    // argv and envp are ignored for the time being
    int f = open(file, O_RDONLY);
    if (f < 0) {
        perror("ohfuck");
        return err_map(errno);
    }

    struct elf_header header;
    // must be a valid x86_32 elf file
    int res = read(f, &header, sizeof(header));
    if (res < 0) ERRNO_FAIL(out);
    err = _ENOEXEC;
    if (res != sizeof(header)
            || memcmp(&header.magic, ELF_MAGIC, sizeof(header.magic)) != 0
            || header.bitness != ELF_32BIT
            || header.endian != ELF_LITTLEENDIAN
            || header.elfversion1 != 1
            || header.machine != ELF_X86)
        goto out;

    // read the program header
    uint16_t ph_count = header.phent_count;
    if (lseek(f, header.prghead_off, SEEK_SET) < 0) ERRNO_FAIL(out);
    size_t ph_size = sizeof(struct prg_header) * ph_count;
    struct prg_header *ph = malloc(ph_size);
    res = read(f, ph, ph_size);
    if (res < 0) ERRNO_FAIL(out_free_ph);
    if (res != ph_size) goto out_free_ph;

    // TODO allocate new PT and abort task if further failure
    // TODO from this point on, if any error occurs the process will have to be
    // killed before it even starts. yeah, yeah, wasted potential. c'mon, it's
    // just a process.

    // map dat shit!
    for (uint16_t i = 0; i < ph_count; i++) {
        struct prg_header phent = ph[i];
        switch (phent.type) {
            case PT_LOAD:
                TRACE_(
                        "offset:       %x\n"
                        "virt addr:    %x\n"
                        "phys addr:    %x\n"
                        "size in file: %x\n"
                        "size in mem:  %x\n"
                        "flags:        %x\n"
                        "alignment:    %x\n",
                        phent.offset, phent.vaddr, phent.paddr, phent.filesize,
                        phent.memsize, phent.flags, phent.alignment);
                pages_t pages = PAGES_FROM_BYTES(phent.filesize);
                if ((err = pt_map_file(current->cpu.pt, PAGE_ADDR(phent.vaddr),
                                pages, f, phent.offset, 0)) < 0) {
                    goto beyond_hope;
                }
                break;

            // TODO case PT_INTERP
            // did you know that most binary files have a shebang-equivalent that points to /lib/ld.so?
        }
    }

    // allocate stack
    // TODO P_GROWSDOWN flag
    if ((err = pt_map_nothing(current->cpu.pt, 0xffffe, 1, P_WRITABLE)) < 0) {
        goto beyond_hope;
    }
    // give one page to grow down. I need motivation to implement page fault handling
    current->cpu.esp = 0xfffff000;
    current->cpu.eip = header.entry_point;

    pt_dump(current->cpu.pt);

    err = 0;
out_free_ph:
    free(ph);
out:
    close(f);
    return err;

beyond_hope:
    // TODO call sys_exit
    goto out_free_ph;
}

int _sys_execve(addr_t filename, addr_t argv, addr_t envp) {
    // TODO translate rest of arguments
    char buf[255];
    user_get_string(filename, buf, sizeof(buf));
    return sys_execve(buf, NULL, NULL);
}
