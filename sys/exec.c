#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "sys/calls.h"
#include "sys/errno.h"
#include "sys/elf.h"
#include "libvdso.so.h"

#define ERRNO_FAIL(label) { \
    err = err_map(errno); \
    goto label; \
}

static inline dword_t align_stack(dword_t sp);
static inline size_t user_strlen(dword_t p);
static inline void user_memset(addr_t start, dword_t len, byte_t val);
static inline dword_t copy_string(dword_t sp, const char *string);
static inline dword_t copy_strings(dword_t sp, char *const strings[]);
static unsigned count_args(char *const args[]);

static int read_header(int f, struct elf_header *header) {
    if (read(f, header, sizeof(*header)) != sizeof(*header)) {
        if (errno != 0)
            return _EIO;
        return _ENOEXEC;
    }
    if (memcmp(&header->magic, ELF_MAGIC, sizeof(header->magic)) != 0
            || header->bitness != ELF_32BIT
            || header->endian != ELF_LITTLEENDIAN
            || header->elfversion1 != 1
            || header->machine != ELF_X86)
        return _ENOEXEC;
    return 0;
}

static int read_prg_headers(int f, struct elf_header header, struct prg_header **ph_out) {
    ssize_t ph_size = sizeof(struct prg_header) * header.phent_count;
    struct prg_header *ph = malloc(ph_size);
    if (ph == NULL)
        return _ENOMEM;

    if (lseek(f, header.prghead_off, SEEK_SET) < 0) {
        free(ph);
        return _EIO;
    }
    if (read(f, ph, ph_size) != ph_size) {
        free(ph);
        if (errno != 0)
            return _EIO;
        return _ENOEXEC;
    }

    *ph_out = ph;
    return 0;
}

static int load_entry(struct prg_header ph, addr_t bias, int f) {
    int err;

    addr_t addr = ph.vaddr + bias;
    addr_t offset = ph.offset;
    addr_t memsize = ph.memsize;
    addr_t filesize = ph.filesize;

    int flags = 0;
    if (ph.flags & PH_W) flags |= P_WRITE;

    if ((err = pt_map_file(&curmem, PAGE(addr),
                    PAGE_ROUND_UP(filesize + OFFSET(addr)), f,
                    offset - OFFSET(addr), flags)) < 0)
        return err;

    if (memsize > filesize) {
        // put zeroes between addr + filesize and addr + memsize, call that bss
        dword_t bss_size = memsize - filesize;

        // first zero the tail from the end of the file mapping to the end
        // of the load entry or the end of the page, whichever comes first
        addr_t file_end = addr + filesize;
        dword_t tail_size = PAGE_SIZE - OFFSET(file_end);
        if (tail_size == PAGE_SIZE)
            // if you can calculate tail_size better and not have to do this please let me know
            tail_size = 0;

        if (tail_size != 0)
            user_memset(file_end, tail_size, 0);
        if (tail_size > bss_size)
            tail_size = bss_size;

        // then map the pages from after the file mapping up to and including the end of bss
        if (bss_size - tail_size != 0)
            if ((err = pt_map_nothing(&curmem, PAGE_ROUND_UP(addr + filesize),
                    PAGE_ROUND_UP(bss_size - tail_size), flags)) < 0)
                return err;
    }
    return 0;
}
int sys_execve(const char *file, char *const argv[], char *const envp[]) {
    int err = 0;

    // open the file and read the headers
    int f;
    if ((f = open(file, O_RDONLY)) < 0)
        return err_map(errno);
    struct elf_header header;
    if ((err = read_header(f, &header)) < 0)
        goto out_free_f;
    struct prg_header *ph;
    if ((err = read_prg_headers(f, header, &ph)) < 0)
        goto out_free_f;

    // look for an interpreter
    char *interp_name = NULL;
    int interp_f = -1;
    struct elf_header interp_header;
    struct prg_header *interp_ph;
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type != PT_INTERP)
            continue;
        if (interp_name) {
            // can't have two interpreters
            err = _EINVAL;
            goto out_free_interp;
        }

        interp_name = malloc(ph[i].filesize);
        err = _ENOMEM;
        if (interp_name == NULL)
            goto out_free_ph;

        // read the interpreter name out of the file
        err = _EIO;
        if (lseek(f, ph[i].offset, SEEK_SET) < 0)
            goto out_free_interp;
        if (read(f, interp_name, ph[i].filesize) != ph[i].filesize)
            goto out_free_interp;

        // open interpreter and read headers
        if ((interp_f = open(interp_name, O_RDONLY)) < 0) {
            err = err_map(errno);
            goto out_free_interp;
        }
        if ((err = read_header(interp_f, &interp_header)) < 0) {
            if (err == _ENOEXEC) err = _ELIBBAD;
            goto out_free_interp;
        }
        if ((err = read_prg_headers(interp_f, interp_header, &interp_ph)) < 0) {
            if (err == _ENOEXEC) err = _ELIBBAD;
            goto out_free_interp;
        }
    }

    // free the process's memory.
    // from this point on, if any error occurs the process will have to be
    // killed before it even starts. please don't be too sad about it, it's
    // just a process.
    pt_unmap(&curmem, 0, PT_SIZE, PT_FORCE);

    addr_t load_addr; // used for AX_PHDR
    bool load_addr_set = false;

    // map dat shit!
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type != PT_LOAD)
            continue;

        if ((err = load_entry(ph[i], 0, f)) < 0)
            goto beyond_hope;

        // load_addr is used to get a value for AX_PHDR et al
        if (!load_addr_set) {
            load_addr = ph[i].vaddr - ph[i].offset;
            load_addr_set = true;
        }

        // we have to know where the brk starts
        if (ph[i].vaddr + ph[i].memsize > current->start_brk)
            current->start_brk = current->brk = BYTES_ROUND_UP(ph[i].vaddr + ph[i].memsize);
    }

    addr_t entry = header.entry_point;
    addr_t interp_addr = 0;

    if (interp_name) {
        // map dat shit! interpreter edition
        // but first, a brief intermission to find out just how big the interpreter is
        struct prg_header *interp_first = NULL, *interp_last = NULL;
        for (int i = 0; i < interp_header.phent_count; i++) {
            if (interp_ph[i].type == PT_LOAD) {
                if (interp_first == NULL)
                    interp_first = &interp_ph[i];
                interp_last = &interp_ph[i];
            }
        }
        pages_t interp_size = 0;
        if (interp_first != NULL) {
            pages_t a = PAGE_ROUND_UP(interp_last->vaddr + interp_last->memsize);
            pages_t b = PAGE(interp_first->vaddr);
            interp_size = a - b;
        }
        interp_addr = pt_find_hole(&curmem, interp_size) << PAGE_BITS;
        // now back to map dat shit! interpreter edition
        for (int i = interp_header.phent_count; i >= 0; i--) {
            if (interp_ph[i].type != PT_LOAD)
                continue;
            if ((err = load_entry(interp_ph[i], interp_addr, interp_f)) < 0)
                goto beyond_hope;
        }
        entry = interp_addr + interp_header.entry_point;
    }

    // map vdso
    err = _ENOMEM;
    pages_t vdso_pages = sizeof(vdso_data) >> PAGE_BITS;
    page_t vdso_page = pt_find_hole(&curmem, vdso_pages);
    if (vdso_page == BAD_PAGE)
        goto beyond_hope;
    if ((err = pt_map(&curmem, vdso_page, vdso_pages, (void *) vdso_data, 0)) < 0)
        goto beyond_hope;
    current->vdso = vdso_page << PAGE_BITS;
    addr_t vdso_entry = current->vdso + ((struct elf_header *) vdso_data)->entry_point;

    // map 2 empty "vvar" pages to satisfy ptraceomatic
    page_t vvar_page = pt_find_hole(&curmem, 2);
    if (vvar_page == BAD_PAGE)
        goto beyond_hope;
    if ((err = pt_map_nothing(&curmem, vvar_page, 2, 0)) < 0)
        goto beyond_hope;

    // STACK TIME!

    // allocate 1 page of stack at 0xffffd, and let it grow down
    if ((err = pt_map_nothing(&curmem, 0xffffd, 1, P_WRITE | P_GROWSDOWN)) < 0) {
        goto beyond_hope;
    }
    dword_t sp = 0xffffe000;
    // on 32-bit linux, there's 4 empty bytes at the very bottom of the stack.
    // on 64-bit linux, there's 8. make ptraceomatic happy. (a major theme in this file)
    sp -= sizeof(void *);

    // first, copy stuff pointed to by argv/envp/auxv
    // filename, argc, argv
    addr_t file_addr = sp = copy_string(sp, file);
    addr_t envp_addr = sp = copy_strings(sp, envp);
    addr_t argv_addr = sp = copy_strings(sp, argv);
    sp = align_stack(sp);

    addr_t platform_addr = sp = copy_string(sp, "i686");
    // 16 random bytes so no system call is needed to seed a userspace RNG
    char random[16] = {};
    if (getentropy(random, sizeof(random)) < 0)
        abort(); // if this fails, something is very badly wrong indeed
    addr_t random_addr = sp -= sizeof(random);
    user_put_count(sp, random, sizeof(random));

    // the way linux aligns the stack at this point is kinda funky
    // calculate how much space is needed for argv, envp, and auxv, subtract
    // that from sp, then align, then copy argv/envp/auxv from that down

    // declare elf aux now so we can know how big it is
    struct aux_ent aux[] = {
        {AX_SYSINFO, vdso_entry},
        {AX_SYSINFO_EHDR, current->vdso},
        {AX_HWCAP, 0x00000000}, // suck that
        {AX_PAGESZ, PAGE_SIZE},
        {AX_CLKTCK, 0x64},
        {AX_PHDR, load_addr + header.prghead_off},
        {AX_PHENT, sizeof(struct prg_header)},
        {AX_PHNUM, header.phent_count},
        {AX_BASE, interp_addr},
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
    size_t argc = count_args(argv);
    size_t envc = count_args(envp);
    sp -= ((argc + 1) + (envc + 1) + 1) * sizeof(dword_t);
    sp -= sizeof(aux);
    sp &=~ 0xf;

    // now copy down, start using p so sp is preserved
    addr_t p = sp;

    // argc
    user_put(p, argc); p += sizeof(dword_t);

    // argv
    while (argc-- > 0) {
        user_put(p, argv_addr);
        argv_addr += user_strlen(argv_addr) + 1;
        p += sizeof(dword_t); // null terminator
    }
    p += sizeof(dword_t); // null terminator

    // envp
    while (envc-- > 0) {
        user_put(p, envp_addr);
        envp_addr += user_strlen(envp_addr) + 1;
        p += sizeof(dword_t);
    }
    p += sizeof(dword_t); // null terminator

    // copy auxv
    user_put_count(p, (const char *) aux, sizeof(aux));
    p += sizeof(aux);

    current->cpu.esp = sp;
    current->cpu.eip = entry;

    err = 0;
out_free_interp:
    if (interp_name != NULL)
        free(interp_name);
    if (interp_f != -1)
        close(interp_f);
out_free_ph:
    free(ph);
out_free_f:
    close(f);
    return err;

beyond_hope:
    // TODO force sigsegv
    goto out_free_interp;
}

static unsigned count_args(char *const args[]) {
    unsigned i;
    for (i = 0; args[i] != NULL; i++)
        ;
    return i;
}

static inline dword_t align_stack(addr_t sp) {
    return sp &~ 0xf;
}

static inline dword_t copy_string(addr_t sp, const char *string) {
    sp -= strlen(string) + 1;
    user_put_string(sp, string);
    return sp;
}

static inline dword_t copy_strings(addr_t sp, char *const strings[]) {
    for (unsigned i = count_args(strings); i > 0; i--) {
        sp = copy_string(sp, strings[i - 1]);
    }
    return sp;
}

static inline size_t user_strlen(addr_t p) {
    size_t len = 0;
    while (user_get8(p++) != 0) len++;
    return len;
}

static inline void user_memset(addr_t start, dword_t len, byte_t val) {
    while (len--) {
        user_put8(start++, val);
    }
}

#define MAX_ARGS 256 // for now
dword_t _sys_execve(addr_t filename_addr, addr_t argv_addr, addr_t envp_addr) {
    // TODO this code is shit, fix it
    char filename[MAX_PATH];
    user_get_string(filename_addr, filename, sizeof(filename));
    char *argv[MAX_ARGS];
    int i;
    for (i = 0; user_get(argv_addr + i * 4) != 0; i++) {
        if (i > MAX_ARGS)
            return _E2BIG;
        argv[i] = malloc(MAX_PATH);
        user_get_string(user_get(argv_addr + i * 4), argv[i], MAX_PATH);
    }
    argv[i] = NULL;
    char *envp[MAX_ARGS];
    for (i = 0; user_get(envp_addr + i * 4) != 0; i++) {
        if (i >= MAX_ARGS)
            return _E2BIG;
        envp[i] = malloc(MAX_PATH);
        user_get_string(user_get(envp_addr + i * 4), envp[i], MAX_PATH);
    }
    envp[i] = NULL;
    int res = sys_execve(filename, argv, envp);
    for (i = 0; argv[i] != NULL; i++)
        free(argv[i]);
    for (i = 0; envp[i] != NULL; i++)
        free(envp[i]);
    return res;
}
