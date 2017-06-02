#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>
#if __APPLE__
#define getrandom(buf, buflen, flags) getentropy(buf, buflen)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "emu/memory.h"
#include "emu/process.h"
#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/exec/elf.h"
#include "libvdso.so.h"

#define ERRNO_FAIL(label) { \
    err = err_map(errno); \
    goto label; \
}

static inline dword_t align_stack(dword_t sp);
static inline size_t strlen_user(dword_t p);
static inline dword_t copy_data(dword_t sp, const char *data, size_t count);
static inline dword_t copy_string(dword_t sp, const char *string);
static inline dword_t copy_strings(dword_t sp, char *const strings[]);
unsigned count_args(char *const args[]);

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

    addr_t load_addr; // used for AX_PHDR
    bool load_addr_set = false;
    addr_t bss = 0; // end of data/start of bss
    addr_t brk = 0; // end of bss/start of heap
    int bss_flags;

    // map dat shit!
    for (uint16_t i = 0; i < ph_count; i++) {
        struct prg_header phent = ph[i];
        switch (phent.type) {
            case PT_LOAD:
                TRACE(
                        "offset:       %x\n"
                        "virt addr:    %x\n"
                        "phys addr:    %x\n"
                        "size in file: %x\n"
                        "size in mem:  %x\n"
                        "flags:        %x\n"
                        "alignment:    %x\n",
                        phent.offset, phent.vaddr, phent.paddr, phent.filesize,
                        phent.memsize, phent.flags, phent.alignment);
                int flags = 0;
                if (phent.flags & PH_W) flags |= P_WRITABLE;
                addr_t addr = phent.vaddr;
                addr_t size = phent.filesize + OFFSET(phent.vaddr);
                addr_t off = phent.offset - OFFSET(phent.vaddr);
                printf("new size:     %x\n", size);
                printf("new offset:   %x\n", off);
                if ((err = pt_map_file(&curmem,
                                PAGE(addr), PAGE_ROUND_UP(size), f, off, flags)) < 0) {
                    goto beyond_hope;
                }
                // load_addr is used to get a value for AX_PHDR et al
                if (!load_addr_set) {
                    load_addr = phent.vaddr - phent.offset;
                    load_addr_set = true;
                }

                if (phent.vaddr + phent.filesize > bss) {
                    bss = phent.vaddr + phent.filesize;
                }
                if (phent.vaddr + phent.memsize > brk) {
                    bss_flags = flags;
                    brk = phent.vaddr + phent.memsize;
                }
                break;

            // TODO case PT_INTERP
            // did you know that most binary files have a shebang-equivalent that points to /lib/ld.so?
        }
    }

    // map the bss (which ends at brk)
    brk = PAGE_ROUND_UP(brk);
    bss = PAGE_ROUND_UP(bss);
    if (brk > bss) {
        if ((err = pt_map_nothing(&curmem, bss, brk - bss, bss_flags)) < 0) {
            goto beyond_hope;
        }
    }
    current->start_brk = current->brk = brk << PAGE_BITS;

    // map vdso
    addr_t vdso_addr = 0xf7ffc000;
    if ((err = pt_map(&curmem, PAGE(vdso_addr), 1, (void *) vdso_data, 0)) < 0) {
        goto beyond_hope;
    }
    addr_t vdso_entry = vdso_addr + ((struct elf_header *) vdso_data)->entry_point;

    // allocate stack
    if ((err = pt_map_nothing(&curmem, 0xffffd, 1, P_WRITABLE | P_GROWSDOWN)) < 0) {
        goto beyond_hope;
    }
    // give about a page to grow down. I need motivation to implement page fault handling
    dword_t sp = 0xffffe000;
    // on 32-bit linux, there's 4 empty bytes at the very bottom of the stack.
    // on 64-bit linux, there's 8. make ptraceomatic happy.
    sp -= sizeof(void *);

    // filename, argc, argv
    addr_t file_addr = sp = copy_string(sp, file);
    addr_t envp_addr = sp = copy_strings(sp, envp);
    addr_t argv_addr = sp = copy_strings(sp, argv);
    sp = align_stack(sp);

    // stuff pointed to by elf aux
    addr_t platform_addr = sp = copy_string(sp, "i686");
    // 16 random bytes so no system call is needed to seed a userspace RNG
    char random[16] = {};
    while (getrandom(random, sizeof(random), 0) < 0) {
        if (errno != EAGAIN) {
            ERRNO_FAIL(beyond_hope);
        }
    }
    addr_t random_addr = sp = copy_data(sp, random, sizeof(random));
    sp &=~ 0x7;

    // elf aux
    struct aux_ent aux[] = {
        {AX_SYSINFO, vdso_entry},
        {AX_SYSINFO_EHDR, vdso_addr},
        {AX_HWCAP, 0x00000000}, // suck that
        {AX_PAGESZ, PAGE_SIZE},
        {AX_CLKTCK, 0x64},
        {AX_PHDR, load_addr + header.prghead_off},
        {AX_PHENT, sizeof(struct prg_header)},
        {AX_PHNUM, header.phent_count},
        {AX_BASE, 0},
        {AX_FLAGS, 0},
        {AX_ENTRY, header.entry_point},
        {AX_UID, 0},
        {AX_EUID, 0},
        {AX_GID, 0},
        {AX_EGID, 0},
        {AX_SECURE, 0},
        {AX_RANDOM, random_addr},
        {AX_HWCAP2, 0}, // suck that too
        {AX_EXECFN, file_addr},
        {AX_PLATFORM, platform_addr},
        {0, 0}
    };
    sp = copy_data(sp, (const char *) aux, sizeof(aux));

    // envp
    size_t envc = count_args(envp);
    sp -= sizeof(dword_t); // null terminator
    sp -= envc * sizeof(dword_t);
    dword_t p = sp;
    while (envc-- > 0) {
        user_put(p, envp_addr);
        envp_addr += strlen_user(envp_addr) + 1;
        p += sizeof(dword_t);
    }

    // argv
    size_t argc = count_args(argv);
    sp -= sizeof(dword_t); // null terminator
    sp -= argc * sizeof(dword_t);
    p = sp;
    while (argc-- > 0) {
        user_put(p, argv_addr);
        argv_addr += strlen_user(argv_addr) + 1;
        p += sizeof(dword_t);
    }

    // argc
    sp -= sizeof(dword_t); user_put(sp, count_args(argv));

    current->cpu.esp = sp;
    current->cpu.eip = header.entry_point;
    /* pt_dump(current->cpu.pt); */

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

unsigned count_args(char *const args[]) {
    unsigned i;
    for (i = 0; args[i] != NULL; i++)
        ;
    return i;
}

static inline dword_t align_stack(dword_t sp) {
    return sp &~ 0xf;
}

static inline dword_t copy_string(dword_t sp, const char *string) {
    sp -= strlen(string) + 1;
    user_put_string(sp, string);
    return sp;
}

static inline dword_t copy_strings(dword_t sp, char *const strings[]) {
    for (unsigned i = 0; strings[i] != NULL; i++) {
        sp = copy_string(sp, strings[i]);
    }
    return sp;
}

static inline dword_t copy_data(dword_t sp, const char *data, size_t count) {
    sp -= count;
    user_put_count(sp, data, count);
    return sp;
}

static inline size_t strlen_user(dword_t p) {
    size_t len = 0;
    while (user_get8(p++) != 0) len++;
    return len;
}

int _sys_execve(addr_t filename, addr_t argv, addr_t envp) {
    // TODO translate rest of arguments
    char buf[255];
    user_get_string(filename, buf, sizeof(buf));
    return sys_execve(buf, NULL, NULL);
}
