#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "kernel/calls.h"
#include "kernel/errno.h"
#include "fs/fd.h"
#include "kernel/elf.h"
#include "kernel/vdso.h"

static inline dword_t align_stack(dword_t sp);
static inline ssize_t user_strlen(dword_t p);
static inline int user_memset(addr_t start, byte_t val, dword_t len);
static inline dword_t copy_string(dword_t sp, const char *string);
static inline dword_t copy_strings(dword_t sp, char *const strings[]);
static unsigned count_args(char *const args[]);

static int read_header(struct fd *fd, struct elf_header *header) {
    int err;
    if (fd->ops->lseek(fd, 0, SEEK_SET))
        return _EIO;
    if ((err = fd->ops->read(fd, header, sizeof(*header))) != sizeof(*header)) {
        if (err != 0)
            return _EIO;
        return _ENOEXEC;
    }
    if (memcmp(&header->magic, ELF_MAGIC, sizeof(header->magic)) != 0
            || (header->type != ELF_EXECUTABLE && header->type != ELF_DYNAMIC)
            || header->bitness != ELF_32BIT
            || header->endian != ELF_LITTLEENDIAN
            || header->elfversion1 != 1
            || header->machine != ELF_X86)
        return _ENOEXEC;
    return 0;
}

static int read_prg_headers(struct fd *fd, struct elf_header header, struct prg_header **ph_out) {
    ssize_t ph_size = sizeof(struct prg_header) * header.phent_count;
    struct prg_header *ph = malloc(ph_size);
    if (ph == NULL)
        return _ENOMEM;

    if (fd->ops->lseek(fd, header.prghead_off, SEEK_SET) < 0) {
        free(ph);
        return _EIO;
    }
    if (fd->ops->read(fd, ph, ph_size) != ph_size) {
        free(ph);
        if (errno != 0)
            return _EIO;
        return _ENOEXEC;
    }

    *ph_out = ph;
    return 0;
}

static int load_entry(struct prg_header ph, addr_t bias, struct fd *fd) {
    int err;

    addr_t addr = ph.vaddr + bias;
    addr_t offset = ph.offset;
    addr_t memsize = ph.memsize;
    addr_t filesize = ph.filesize;

    int flags = P_READ;
    if (ph.flags & PH_W) flags |= P_WRITE;

    if ((err = fd->ops->mmap(fd, curmem, PAGE(addr),
                    PAGE_ROUND_UP(filesize + PGOFFSET(addr)),
                    offset - PGOFFSET(addr), flags, MMAP_PRIVATE)) < 0)
        return err;

    if (memsize > filesize) {
        // put zeroes between addr + filesize and addr + memsize, call that bss
        dword_t bss_size = memsize - filesize;

        // first zero the tail from the end of the file mapping to the end
        // of the load entry or the end of the page, whichever comes first
        addr_t file_end = addr + filesize;
        dword_t tail_size = PAGE_SIZE - PGOFFSET(file_end);
        if (tail_size == PAGE_SIZE)
            // if you can calculate tail_size better and not have to do this please let me know
            tail_size = 0;

        if (tail_size != 0)
            user_memset(file_end, 0, tail_size);
        if (tail_size > bss_size)
            tail_size = bss_size;

        // then map the pages from after the file mapping up to and including the end of bss
        if (bss_size - tail_size != 0)
            if ((err = pt_map_nothing(curmem, PAGE_ROUND_UP(addr + filesize),
                    PAGE_ROUND_UP(bss_size - tail_size), flags)) < 0)
                return err;
    }
    return 0;
}

static int elf_exec(struct fd *fd, const char *file, char *const argv[], char *const envp[]) {
    int err = 0;

    // read the headers
    struct elf_header header;
    if ((err = read_header(fd, &header)) < 0)
        return err;
    struct prg_header *ph;
    if ((err = read_prg_headers(fd, header, &ph)) < 0)
        return err;

    // look for an interpreter
    char *interp_name = NULL;
    struct fd *interp_fd = NULL;
    struct elf_header interp_header;
    struct prg_header *interp_ph = NULL;
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
        if (fd->ops->lseek(fd, ph[i].offset, SEEK_SET) < 0)
            goto out_free_interp;
        if (fd->ops->read(fd, interp_name, ph[i].filesize) != ph[i].filesize)
            goto out_free_interp;

        // open interpreter and read headers
        interp_fd = generic_open(interp_name, O_RDONLY, 0);
        if (IS_ERR(interp_fd)) {
            err = PTR_ERR(interp_fd);
            goto out_free_interp;
        }
        if ((err = read_header(interp_fd, &interp_header)) < 0) {
            if (err == _ENOEXEC) err = _ELIBBAD;
            goto out_free_interp;
        }
        if ((err = read_prg_headers(interp_fd, interp_header, &interp_ph)) < 0) {
            if (err == _ENOEXEC) err = _ELIBBAD;
            goto out_free_interp;
        }
    }

    // free the process's memory.
    // from this point on, if any error occurs the process will have to be
    // killed before it even starts. please don't be too sad about it, it's
    // just a process.
    mem_release(curmem);
    current->cpu.mem = mem_new();
    write_wrlock(&curmem->lock);

    addr_t load_addr; // used for AX_PHDR
    bool load_addr_set = false;
    addr_t bias = 0; // offset for loading shared libraries as executables

    // map dat shit!
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type != PT_LOAD)
            continue;

        if (!load_addr_set && header.type == ELF_DYNAMIC)
            bias = 0x56555000; // I've no idea why

        if ((err = load_entry(ph[i], bias, fd)) < 0)
            goto beyond_hope;

        // load_addr is used to get a value for AX_PHDR et al
        if (!load_addr_set) {
            load_addr = bias + ph[i].vaddr - ph[i].offset;
            load_addr_set = true;
        }

        // we have to know where the brk starts
        addr_t brk = bias + ph[i].vaddr + ph[i].memsize;
        if (brk > current->start_brk)
            current->start_brk = current->brk = BYTES_ROUND_UP(brk);
    }

    addr_t entry = bias + header.entry_point;
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
        interp_addr = pt_find_hole(curmem, interp_size) << PAGE_BITS;
        // now back to map dat shit! interpreter edition
        for (int i = interp_header.phent_count - 1; i >= 0; i--) {
            if (interp_ph[i].type != PT_LOAD)
                continue;
            if ((err = load_entry(interp_ph[i], interp_addr, interp_fd)) < 0)
                goto beyond_hope;
        }
        entry = interp_addr + interp_header.entry_point;
    }

    // map vdso
    err = _ENOMEM;
    pages_t vdso_pages = sizeof(vdso_data) >> PAGE_BITS;
    page_t vdso_page = pt_find_hole(curmem, vdso_pages);
    if (vdso_page == BAD_PAGE)
        goto beyond_hope;
    if ((err = pt_map(curmem, vdso_page, vdso_pages, (void *) vdso_data, 0)) < 0)
        goto beyond_hope;
    current->vdso = vdso_page << PAGE_BITS;
    addr_t vdso_entry = current->vdso + ((struct elf_header *) vdso_data)->entry_point;

    // map 3 empty "vvar" pages to satisfy ptraceomatic
    page_t vvar_page = pt_find_hole(curmem, 3);
    if (vvar_page == BAD_PAGE)
        goto beyond_hope;
    if ((err = pt_map_nothing(curmem, vvar_page, 3, 0)) < 0)
        goto beyond_hope;

    // STACK TIME!

    // allocate 1 page of stack at 0xffffd, and let it grow down
    if ((err = pt_map_nothing(curmem, 0xffffd, 1, P_WRITE | P_GROWSDOWN)) < 0)
        goto beyond_hope;
    // that was the last memory mapping
    write_wrunlock(&curmem->lock);
    dword_t sp = 0xffffe000;
    // on 32-bit linux, there's 4 empty bytes at the very bottom of the stack.
    // on 64-bit linux, there's 8. make ptraceomatic happy. (a major theme in this file)
    sp -= sizeof(void *);

    err = _EFAULT;
    // first, copy stuff pointed to by argv/envp/auxv
    // filename, argc, argv
    addr_t file_addr = sp = copy_string(sp, file);
    if (sp == 0)
        goto beyond_hope;
    addr_t envp_addr = sp = copy_strings(sp, envp);
    if (sp == 0)
        goto beyond_hope;
    addr_t argv_addr = sp = copy_strings(sp, argv);
    if (sp == 0)
        goto beyond_hope;
    sp = align_stack(sp);

    addr_t platform_addr = sp = copy_string(sp, "i686");
    if (sp == 0)
        goto beyond_hope;
    // 16 random bytes so no system call is needed to seed a userspace RNG
    char random[16] = {};
    int dev_random = open("/dev/urandom", O_RDONLY);
    if (dev_random < 0 ||
            read(dev_random, random, sizeof(random)) != sizeof(random))
        abort(); // if this fails, something is very badly wrong indeed
    close(dev_random);
    addr_t random_addr = sp -= sizeof(random);
    if (user_put(sp, random))
        goto beyond_hope;

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
        {AX_ENTRY, bias + header.entry_point},
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
    dword_t argc = count_args(argv);
    dword_t envc = count_args(envp);
    sp -= ((argc + 1) + (envc + 1) + 1) * sizeof(dword_t);
    sp -= sizeof(aux);
    sp &=~ 0xf;

    // now copy down, start using p so sp is preserved
    addr_t p = sp;

    // argc
    if (user_put(p, argc))
        return _EFAULT;
    p += sizeof(dword_t);

    // argv
    while (argc-- > 0) {
        if (user_put(p, argv_addr))
            return _EFAULT;
        argv_addr += user_strlen(argv_addr) + 1;
        p += sizeof(dword_t); // null terminator
    }
    p += sizeof(dword_t); // null terminator

    // envp
    while (envc-- > 0) {
        if (user_put(p, envp_addr))
            return _EFAULT;
        envp_addr += user_strlen(envp_addr) + 1;
        p += sizeof(dword_t);
    }
    p += sizeof(dword_t); // null terminator

    // copy auxv
    if (user_put(p, aux))
        goto beyond_hope;
    p += sizeof(aux);

    current->cpu.esp = sp;
    current->cpu.eip = entry;
    current->cpu.fcw = 0x37f;
    collapse_flags(&current->cpu);
    current->vfork_done = true;
    notify(&current->vfork_cond);

    err = 0;
out_free_interp:
    if (interp_name != NULL)
        free(interp_name);
    if (interp_fd != NULL)
        interp_fd->ops->close(interp_fd);
    if (interp_ph != NULL)
        free(interp_ph);
out_free_ph:
    free(ph);
    return err;

beyond_hope:
    // TODO force sigsegv
    write_wrunlock(&curmem->lock);
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
    if (user_write_string(sp, string))
        return 0;
    return sp;
}

static inline dword_t copy_strings(addr_t sp, char *const strings[]) {
    for (unsigned i = count_args(strings); i > 0; i--) {
        sp = copy_string(sp, strings[i - 1]);
        if (sp == 0)
            return 0;
    }
    return sp;
}

static inline ssize_t user_strlen(addr_t p) {
    size_t i = 0;
    char c;
    do {
        if (user_get(p + i, c))
            return -1;
        i++;
    } while (c != '\0');
    return i - 1;
}

static inline int user_memset(addr_t start, byte_t val, dword_t len) {
    while (len--)
        if (user_put(start++, val))
            return 1;
    return 0;
}

static int shebang_exec(struct fd *fd, const char *file, char *const argv[], char *const envp[]) {
    // read the first 128 bytes to get the shebang line out of
    if (fd->ops->lseek(fd, 0, SEEK_SET))
        return _EIO;
    char header[128];
    int size = fd->ops->read(fd, header, sizeof(header) - 1);
    if (size < 0)
        return _EIO;
    header[size] = '\0';

    // only look at the first line
    char *newline = strchr(header, '\n');
    if (newline == NULL)
        return _ENOEXEC;
    *newline = '\0';

    // format: #![spaces]interpreter[spaces]argument[spaces]
    char *p = header;
    if (p[0] != '#' || p[1] != '!')
        return _ENOEXEC;
    p += 2;
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return _ENOEXEC;

    char *interpreter = p;
    while (*p != ' ' && *p != '\0')
        p++;
    if (*p != '\0') {
        *p++ = '\0';
        while (*p == ' ')
            p++;
    }

    char *argument = p;
    // strip trailing whitespace
    p = strchr(p, '\0') - 1;
    while (*p == ' ')
        *p-- = '\0';
    if (*argument == '\0')
        argument = NULL;

    int args_extra = 2;
    if (argument)
        args_extra++;
    char *real_argv[count_args(argv) + args_extra];
    real_argv[0] = interpreter;
    if (argument)
        real_argv[1] = argument;
    real_argv[args_extra - 1] = (char *) file; // maybe you'll have better luck getting rid of this cast
    memcpy(real_argv + args_extra, argv + 1, (count_args(argv)) * sizeof(argv[0]));
    return sys_execve(interpreter, real_argv, envp);
}

int sys_execve(const char *file, char *const argv[], char *const envp[]) {
    struct fd *fd = generic_open(file, O_RDONLY, 0);
    if (IS_ERR(fd))
        return PTR_ERR(fd);

    int err = elf_exec(fd, file, argv, envp);
    if (err != _ENOEXEC)
        goto found;
    err = shebang_exec(fd, file, argv, envp);
    if (err != _ENOEXEC)
        goto found;

found:
    fd_close(fd);
    for (fd_t f = 0; f < current->files->size; f++)
        if (f_is_cloexec(f))
            f_close(f);
    return 0;
}

#define MAX_ARGS 256 // for now
dword_t _sys_execve(addr_t filename_addr, addr_t argv_addr, addr_t envp_addr) {
    // TODO this code is shit, fix it
    char filename[MAX_PATH];
    if (user_read_string(filename_addr, filename, sizeof(filename)))
        return _EFAULT;
    char *argv[MAX_ARGS];
    int i;
    addr_t arg;
    STRACE("execve(\"%s\", {", filename);
    for (i = 0; ; i++) {
        if (user_get(argv_addr + i * 4, arg))
            return _EFAULT;
        if (arg == 0)
            break;
        if (i > MAX_ARGS)
            return _E2BIG;
        argv[i] = malloc(MAX_PATH);
        if (user_read_string(arg, argv[i], MAX_PATH))
            return _EFAULT;
        STRACE("\"%s\", ", argv[i]);
    }
    argv[i] = NULL;
    char *envp[MAX_ARGS];
    STRACE("}, {");
    for (i = 0; ; i++) {
        if (user_get(envp_addr + i * 4, arg))
            return _EFAULT;
        if (arg == 0)
            break;
        if (i > MAX_ARGS)
            return _E2BIG;
        envp[i] = malloc(MAX_PATH);
        if (user_read_string(arg, envp[i], MAX_PATH))
            return _EFAULT;
        STRACE("\"%s\", ", envp[i]);
    }
    envp[i] = NULL;
    STRACE("})");
    int res = sys_execve(filename, argv, envp);
    for (i = 0; argv[i] != NULL; i++)
        free(argv[i]);
    for (i = 0; envp[i] != NULL; i++)
        free(envp[i]);
    return res;
}
