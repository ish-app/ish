#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "kernel/calls.h"
#include "kernel/random.h"
#include "kernel/errno.h"
#include "fs/fd.h"
#include "kernel/elf.h"
#include "kernel/vdso.h"

static inline dword_t align_stack(dword_t sp);
static inline ssize_t user_strlen(dword_t p);
static inline int user_memset(addr_t start, byte_t val, dword_t len);
static inline dword_t copy_string(dword_t sp, const char *string);
static inline dword_t copy_strings(dword_t sp, const char *strings);
static size_t strings_size(const char *args);
static unsigned strings_count(const char *args);

static int read_header(struct fd *fd, struct elf_header *header) {
    int err;
    if (fd->ops->lseek(fd, 0, SEEK_SET))
        return _EIO;
    if ((err = fd->ops->read(fd, header, sizeof(*header))) != sizeof(*header)) {
        if (err < 0)
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

    if ((err = fd->ops->mmap(fd, current->mem, PAGE(addr),
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
            if ((err = pt_map_nothing(current->mem, PAGE_ROUND_UP(addr + filesize),
                    PAGE_ROUND_UP(bss_size - tail_size), flags)) < 0)
                return err;
    }
    return 0;
}

static addr_t find_hole_for_elf(struct elf_header *header, struct prg_header *ph) {
    struct prg_header *first = NULL, *last = NULL;
    for (int i = 0; i < header->phent_count; i++) {
        if (ph[i].type == PT_LOAD) {
            if (first == NULL)
                first = &ph[i];
            last = &ph[i];
        }
    }
    pages_t size = 0;
    if (first != NULL) {
        pages_t a = PAGE_ROUND_UP(last->vaddr + last->memsize);
        pages_t b = PAGE(first->vaddr);
        size = a - b;
    }
    return pt_find_hole(current->mem, size) << PAGE_BITS;
}

static int elf_exec(struct fd *fd, const char *file, const char *argv, const char *envp) {
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
    mm_release(current->mm);
    task_set_mm(current, mm_new());
    write_wrlock(&current->mem->lock);

    current->mm->exefile = fd_retain(fd);

    addr_t load_addr; // used for AX_PHDR
    bool load_addr_set = false;
    addr_t bias = 0; // offset for loading shared libraries as executables

    // map dat shit!
    for (unsigned i = 0; i < header.phent_count; i++) {
        if (ph[i].type != PT_LOAD)
            continue;

        if (!load_addr_set && header.type == ELF_DYNAMIC) {
            // see giant comment in linux/fs/binfmt_elf.c, around line 950
            if (interp_name)
                bias = 0x56555000; // I have no idea how this number was arrived at
            else
                bias = find_hole_for_elf(&header, ph);
        }

        if ((err = load_entry(ph[i], bias, fd)) < 0)
            goto beyond_hope;

        // load_addr is used to get a value for AX_PHDR et al
        if (!load_addr_set) {
            load_addr = bias + ph[i].vaddr - ph[i].offset;
            load_addr_set = true;
        }

        // we have to know where the brk starts
        addr_t brk = bias + ph[i].vaddr + ph[i].memsize;
        if (brk > current->mm->start_brk)
            current->mm->start_brk = current->mm->brk = BYTES_ROUND_UP(brk);
    }

    addr_t entry = bias + header.entry_point;
    addr_t interp_base = 0;

    if (interp_name) {
        // map dat shit! interpreter edition
        interp_base = find_hole_for_elf(&interp_header, interp_ph);
        for (int i = interp_header.phent_count - 1; i >= 0; i--) {
            if (interp_ph[i].type != PT_LOAD)
                continue;
            if ((err = load_entry(interp_ph[i], interp_base, interp_fd)) < 0)
                goto beyond_hope;
        }
        entry = interp_base + interp_header.entry_point;
    }

    // map vdso
    err = _ENOMEM;
    pages_t vdso_pages = sizeof(vdso_data) >> PAGE_BITS;
    // FIXME disgusting hack: musl's dynamic linker has a one-page hole, and
    // I'd rather not put the vdso in that hole. so find a two-page hole and
    // add one.
    page_t vdso_page = pt_find_hole(current->mem, vdso_pages + 1);
    if (vdso_page == BAD_PAGE)
        goto beyond_hope;
    vdso_page += 1;
    if ((err = pt_map(current->mem, vdso_page, vdso_pages, (void *) vdso_data, 0)) < 0)
        goto beyond_hope;
    current->mm->vdso = vdso_page << PAGE_BITS;
    addr_t vdso_entry = current->mm->vdso + ((struct elf_header *) vdso_data)->entry_point;

    // map 3 empty "vvar" pages to satisfy ptraceomatic
#define NUM_VVAR 3
    page_t vvar_page = pt_find_hole(current->mem, NUM_VVAR);
    if (vvar_page == BAD_PAGE)
        goto beyond_hope;
    if ((err = pt_map_nothing(current->mem, vvar_page, NUM_VVAR, 0)) < 0)
        goto beyond_hope;

    // STACK TIME!

    // allocate 1 page of stack at 0xffffd, and let it grow down
    if ((err = pt_map_nothing(current->mem, 0xffffd, 1, P_WRITE | P_GROWSDOWN)) < 0)
        goto beyond_hope;
    // that was the last memory mapping
    write_wrunlock(&current->mem->lock);
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
    current->mm->argv_end = sp;
    addr_t argv_addr = sp = copy_strings(sp, argv);
    if (sp == 0)
        goto beyond_hope;
    current->mm->argv_start = sp;
    sp = align_stack(sp);

    addr_t platform_addr = sp = copy_string(sp, "i686");
    if (sp == 0)
        goto beyond_hope;
    // 16 random bytes so no system call is needed to seed a userspace RNG
    char random[16] = {};
    get_random(random, sizeof(random)); // if this fails, eh, no one's really using it
    addr_t random_addr = sp -= sizeof(random);
    if (user_put(sp, random))
        goto beyond_hope;

    // the way linux aligns the stack at this point is kinda funky
    // calculate how much space is needed for argv, envp, and auxv, subtract
    // that from sp, then align, then copy argv/envp/auxv from that down

    // declare elf aux now so we can know how big it is
    struct aux_ent aux[] = {
        {AX_SYSINFO, vdso_entry},
        {AX_SYSINFO_EHDR, current->mm->vdso},
        {AX_HWCAP, 0x00000000}, // suck that
        {AX_PAGESZ, PAGE_SIZE},
        {AX_CLKTCK, 0x64},
        {AX_PHDR, load_addr + header.prghead_off},
        {AX_PHENT, sizeof(struct prg_header)},
        {AX_PHNUM, header.phent_count},
        {AX_BASE, interp_base},
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
    dword_t argc = strings_count(argv);
    dword_t envc = strings_count(envp);
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

    // This code was written when I discovered that the glibc entry point
    // interprets edx as the address of a function to call on exit, as
    // specified in the ABI. This register is normally set by the dynamic
    // linker, so everything works fine until you run a static executable.
    current->cpu.eax = 0;
    current->cpu.ebx = 0;
    current->cpu.ecx = 0;
    current->cpu.edx = 0;
    current->cpu.esi = 0;
    current->cpu.edi = 0;
    current->cpu.ebp = 0;
    collapse_flags(&current->cpu);
    current->cpu.eflags = 0;

    err = 0;
out_free_interp:
    if (interp_name != NULL)
        free(interp_name);
    if (interp_fd != NULL && !IS_ERR(interp_fd))
        fd_close(interp_fd);
    if (interp_ph != NULL)
        free(interp_ph);
out_free_ph:
    free(ph);
    return err;

beyond_hope:
    // TODO force sigsegv
    write_wrunlock(&current->mem->lock);
    goto out_free_interp;
}

// Returns the total memory needed to store the arguments, including all the terminating nulls
static size_t strings_size(const char *args) {
    const char *args_end = args;
    size_t arg_len;
    do {
        arg_len = strlen(args_end);
        args_end += arg_len + 1;
    } while (arg_len != 0);
    return args_end - args;
}

static unsigned strings_count(const char *args) {
    unsigned n = 0;
    while (*args != '\0') {
        args += strlen(args) + 1;
        n++;
    }
    return n;
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

static inline dword_t copy_strings(addr_t sp, const char *strings) {
    size_t size = strings_size(strings);
    sp -= size;
    if (user_write(sp, strings, size))
        return 0;
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

static int format_exec(struct fd *fd, const char *file, const char *argv, const char *envp) {
    int err = elf_exec(fd, file, argv, envp);
    if (err != _ENOEXEC)
        return err;
    // other formats would go here
    return _ENOEXEC;
}

static int shebang_exec(struct fd *fd, const char *file, const char *argv, const char *envp) {
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

    const char *argv_rest = argv + strlen(argv) + 1;
    size_t args_size = strings_size(argv_rest);
    size_t extra_args_size = strlen(interpreter) + 1 + strlen(file) + 1;
    if (argument)
        extra_args_size += strlen(argument) + 1;
    if (args_size + extra_args_size >= 4096)
        return _E2BIG;

    char real_argv[4096];
    size_t n = 0;
    strcpy(real_argv, interpreter);
    n += strlen(interpreter) + 1;
    if (argument) {
        strcpy(real_argv + n, argument);
        n += strlen(argument) + 1;
    }
    strcpy(real_argv + n, file);
    n += strlen(file) + 1;
    memcpy(real_argv + n, argv_rest, args_size);

    struct fd *interpreter_fd = generic_open(interpreter, O_RDONLY_, 0);
    if (IS_ERR(interpreter_fd))
        return PTR_ERR(interpreter_fd);
    int err = format_exec(interpreter_fd, interpreter, real_argv, envp);
    fd_close(interpreter_fd);
    return err;
}

int sys_execve(const char *file, const char *argv, const char *envp) {
    struct fd *fd = generic_open(file, O_RDONLY, 0);
    if (IS_ERR(fd))
        return PTR_ERR(fd);

    struct statbuf stat;
    int err = fd->mount->fs->fstat(fd, &stat);
    if (err < 0) {
        fd_close(fd);
        return err;
    }

    // if nobody has permission to execute, it should be safe to not execute
    if (!(stat.mode & 0111)) {
        fd_close(fd);
        return _EACCES;
    }

    err = format_exec(fd, file, argv, envp);
    if (err == _ENOEXEC)
        err = shebang_exec(fd, file, argv, envp);
    fd_close(fd);
    if (err < 0)
        return err;

    // setuid/setgid
    if (stat.mode & S_ISUID) {
        current->suid = current->euid;
        current->euid = stat.uid;
    }
    if (stat.mode & S_ISGID) {
        current->sgid = current->egid;
        current->egid = stat.gid;
    }

    // save current->comm
    lock(&current->general_lock);
    const char *basename = strrchr(file, '/');
    if (basename == NULL)
        basename = file;
    else
        basename++;
    strncpy(current->comm, basename, sizeof(current->comm));
    unlock(&current->general_lock);

    // set the thread name
    char threadname[16];
    strncpy(threadname, current->comm, sizeof(threadname)-1);
    threadname[15] = '\0';
#if __APPLE__
    pthread_setname_np(threadname);
#else
    pthread_setname_np(pthread_self(), threadname);
#endif

    // cloexec
    // consider putting this in fd.c?
    fdtable_do_cloexec(current->files);

    // reset signal handlers
    lock(&current->sighand->lock);
    for (int sig = 0; sig < NUM_SIGS; sig++) {
        struct sigaction_ *action = &current->sighand->action[sig];
        if (action->handler != SIG_IGN_)
            action->handler = SIG_DFL_;
    }
    current->sighand->altstack = 0;
    unlock(&current->sighand->lock);

    current->did_exec = true;
    vfork_notify(current);
    return 0;
}

static int user_read_string_array(addr_t addr, char *buf, size_t max) {
    size_t i = 0;
    size_t p = 0;
    for (;;) {
        addr_t str_addr;
        if (user_get(addr + i * sizeof(addr_t), str_addr))
            return _EFAULT;
        if (str_addr == 0)
            break;
        size_t str_p = 0;
        for (;;) {
            if (p >= max)
                return _E2BIG;
            if (user_get(str_addr + str_p, buf[p]))
                return _EFAULT;
            str_p++;
            p++;
            if (buf[p - 1] == '\0')
                break;
        }
        i++;
    }
    if (p >= max)
        return _E2BIG;
    buf[p] = '\0';
    return 0;
}

#define MAX_ARGS 256 // for now
dword_t _sys_execve(addr_t filename_addr, addr_t argv_addr, addr_t envp_addr) {
    char filename[MAX_PATH];
    if (user_read_string(filename_addr, filename, sizeof(filename)))
        return _EFAULT;
    char argv[4096];
    int err = user_read_string_array(argv_addr, argv, sizeof(argv));
    if (err < 0)
        return err;
    char envp[4096];
    err = user_read_string_array(envp_addr, envp, sizeof(envp));
    if (err < 0)
        return err;

    STRACE("execve(\"%s\", {", filename);
    const char *args = argv;
    while (*args != '\0') {
        STRACE("\"%s\", ", args);
        args += strlen(args) + 1;
    }
    args = envp;
    while (*args != '\0') {
        STRACE("\"%s\", ", args);
        args += strlen(args) + 1;
    }
    STRACE("})");

    return sys_execve(filename, argv, envp);
}
