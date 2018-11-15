// Fun little utility that single-steps a program using ptrace and
// simultaneously runs the program in ish, and asserts that everything's
// working the same.
// Many apologies for the messy code.
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#undef PAGE_SIZE // defined in sys/user.h, but we want the version from emu/memory.h
#include <sys/personality.h>
#include <sys/socket.h>

#include "debug.h"
#include "kernel/calls.h"
#include "fs/path.h"
#include "fs/fd.h"
#include "emu/interrupt.h"
#include "emu/cpuid.h"

#include "kernel/elf.h"
#include "tools/transplant.h"
#include "tools/ptutil.h"
#include "undefined-flags.h"
#include "kernel/vdso.h"

#include "xX_main_Xx.h"

// ptrace utility functions

// returns 1 for a signal stop
static inline int step(int pid) {
    trycall(ptrace(PTRACE_SINGLESTEP, pid, NULL, 0), "ptrace step");
    int status;
    trycall(waitpid(pid, &status, 0), "wait step");
    if (WIFSTOPPED(status) && WSTOPSIG(status) != SIGTRAP) {
        int signal = WSTOPSIG(status);
        // a signal arrived, we now have to actually deliver it
        trycall(ptrace(PTRACE_SINGLESTEP, pid, NULL, signal), "ptrace step");
        trycall(waitpid(pid, &status, 0), "wait step");
        return 1;
    }
    return 0;
}

static inline void getregs(int pid, struct user_regs_struct *regs) {
    trycall(ptrace(PTRACE_GETREGS, pid, NULL, regs), "ptrace getregs");
}

static inline void setregs(int pid, struct user_regs_struct *regs) {
    trycall(ptrace(PTRACE_SETREGS, pid, NULL, regs), "ptrace setregs");
}

static int compare_cpus(struct cpu_state *cpu, struct tlb *tlb, int pid, int undefined_flags) {
    struct user_regs_struct regs;
    struct user_fpregs_struct fpregs;
    getregs(pid, &regs);
    trycall(ptrace(PTRACE_GETFPREGS, pid, NULL, &fpregs), "ptrace getregs compare");
    collapse_flags(cpu);
#define CHECK(real, fake, name) \
    if ((real) != (fake)) { \
        printk(name ": real 0x%llx, fake 0x%llx\n", (unsigned long long) (real), (unsigned long long) (fake)); \
        debugger; \
        return -1; \
    }
#define CHECK_REG(pt, cp) CHECK(regs.pt, cpu->cp, #cp)
    CHECK_REG(rax, eax);
    CHECK_REG(rbx, ebx);
    CHECK_REG(rcx, ecx);
    CHECK_REG(rdx, edx);
    CHECK_REG(rsi, esi);
    CHECK_REG(rdi, edi);
    CHECK_REG(rsp, esp);
    CHECK_REG(rbp, ebp);
    CHECK_REG(rip, eip);
    undefined_flags |= (1 << 8); // treat trap flag as undefined
    regs.eflags = (regs.eflags & ~undefined_flags) | (cpu->eflags & undefined_flags);
    // give a nice visual representation of the flags
    if (regs.eflags != cpu->eflags) {
#define f(x,n) ((regs.eflags & (1 << n)) ? #x : "-"),
        printf("real eflags = 0x%llx %s%s%s%s%s%s%s%s%s, fake eflags = 0x%x %s%s%s%s%s%s%s%s%s\r\n%0d",
                regs.eflags, f(o,11)f(d,10)f(i,9)f(t,8)f(s,7)f(z,6)f(a,4)f(p,2)f(c,0)
#undef f
#define f(x,n) ((cpu->eflags & (1 << n)) ? #x : "-"),
                cpu->eflags, f(o,11)f(d,10)f(i,9)f(t,8)f(s,7)f(z,6)f(a,4)f(p,2)f(c,0)0);
        debugger;
        return -1;
    }
#define CHECK_XMMREG(i) \
    CHECK(*(uint64_t *) &fpregs.xmm_space[i * 2],   cpu->xmm[i].qw[0], "xmm" #i " low") \
    CHECK(*(uint64_t *) &fpregs.xmm_space[(i+1)*2], cpu->xmm[i].qw[1], "xmm" #i " high")
    CHECK_XMMREG(0);
    CHECK_XMMREG(1);
    CHECK_XMMREG(2);
    CHECK_XMMREG(3);
    CHECK_XMMREG(4);
    CHECK_XMMREG(5);
    CHECK_XMMREG(6);
    CHECK_XMMREG(7);

#define FSW_MASK 0x7d00 // only look at top, c0, c2, c3
    CHECK(fpregs.swd & FSW_MASK, cpu->fsw & FSW_MASK, "fsw");
    CHECK(fpregs.cwd, cpu->fcw, "fcw");
    fpregs.swd &= FSW_MASK;

#define CHECK_FPREG(i) \
    CHECK(*(uint64_t *) &fpregs.st_space[i * 4], cpu->fp[(cpu->top + i)%8].signif,  "st(" #i ") signif") \
    CHECK(*(uint16_t *) &fpregs.st_space[i*4+2], cpu->fp[(cpu->top + i)%8].signExp, "st(" #i ") sign/exp")
    CHECK_FPREG(0);
    CHECK_FPREG(1);
    CHECK_FPREG(2);
    CHECK_FPREG(3);
    CHECK_FPREG(4);
    CHECK_FPREG(5);
    CHECK_FPREG(6);
    CHECK_FPREG(7);

    // compare pages marked dirty
    if (tlb->dirty_page != TLB_PAGE_EMPTY) {
        int fd = open_mem(pid);
        page_t dirty_page = tlb->dirty_page;
        char real_page[PAGE_SIZE];
        trycall(lseek(fd, dirty_page, SEEK_SET), "compare seek mem");
        trycall(read(fd, real_page, PAGE_SIZE), "compare read mem");
        close(fd);
        struct pt_entry entry = cpu->mem->pt[PAGE(dirty_page)];
        void *fake_page = entry.data->data + entry.offset;

        if (memcmp(real_page, fake_page, PAGE_SIZE) != 0) {
            printk("page %x doesn't match\n", dirty_page);
            debugger;
            return -1;
        }
        tlb->dirty_page = TLB_PAGE_EMPTY;
    }

    setregs(pid, &regs);
    trycall(ptrace(PTRACE_SETFPREGS, pid, NULL, &fpregs), "ptrace setregs compare");
    return 0;
}

// I'd like to apologize in advance for this code
static int transmit_fd(int pid, int sender, int receiver, int fake_fd) {
    // this sends the fd over a unix domain socket. yes, I'm crazy

    // sending part
    int real_fd = f_get(fake_fd)->real_fd;
    struct msghdr msg = {};
    char cmsg[CMSG_SPACE(sizeof(int))];
    memset(cmsg, 0, sizeof(cmsg));

    msg.msg_control = cmsg;
    msg.msg_controllen = sizeof(cmsg);

    struct cmsghdr *cmsg_hdr = CMSG_FIRSTHDR(&msg);
    cmsg_hdr->cmsg_level = SOL_SOCKET;
    cmsg_hdr->cmsg_type = SCM_RIGHTS;
    cmsg_hdr->cmsg_len = CMSG_LEN(sizeof(int));
    *(int *) CMSG_DATA(cmsg_hdr) = real_fd;

    trycall(sendmsg(sender, &msg, 0), "sendmsg insanity");

    // receiving part
    // painful, because we're 64-bit and the child is 32-bit and I want to kill myself
    struct user_regs_struct saved_regs;
    getregs(pid, &saved_regs);
    struct user_regs_struct regs = saved_regs;

    // reserve space for 32-bit version of cmsg
    regs.rsp -= 16; // according to my calculations
    addr_t cmsg_addr = regs.rsp;
    char cmsg_bak[16];
    pt_readn(pid, regs.rsp, cmsg_bak, sizeof(cmsg_bak));

    // copy 32-bit msghdr
    regs.rsp -= 32;
    int msg32[] = {0, 0, 0, 0, cmsg_addr, 20, 0};
    addr_t msg_addr = regs.rsp;
    char msg_bak[32];
    pt_readn(pid, regs.rsp, msg_bak, sizeof(msg_bak));
    pt_writen(pid, regs.rsp, &msg32, sizeof(msg32));

    regs.rax = 372;
    regs.rbx = receiver;
    regs.rcx = msg_addr;
    regs.rdx = 0;
    // assume we're already on an int $0x80
    setregs(pid, &regs);
    step(pid);
    getregs(pid, &regs);

    int sent_fd;
    if ((long) regs.rax >= 0)
        pt_readn(pid, cmsg_addr + 12, &sent_fd, sizeof(sent_fd));
    else
        sent_fd = regs.rax;

    // restore crap
    pt_writen(pid, cmsg_addr, cmsg_bak, sizeof(cmsg_bak));
    pt_writen(pid, msg_addr, msg_bak, sizeof(msg_bak));
    setregs(pid, &regs);

    if (sent_fd < 0) {
        errno = -sent_fd;
        perror("remote recvmsg insanity");
        exit(1);
    }

    return sent_fd;
}

static void remote_close_fd(int pid, int fd, long int80_ip) {
    // lettuce spray
    struct user_regs_struct saved_regs;
    getregs(pid, &saved_regs);
    struct user_regs_struct regs = saved_regs;
    regs.rip = int80_ip;
    regs.rax = 6;
    regs.rbx = fd;
    setregs(pid, &regs);
    step(pid);
    getregs(pid, &regs);
    if ((long) regs.rax < 0) {
        errno = -regs.rax;
        perror("remote close fd");
        exit(1);
    }
    setregs(pid, &regs);
}

static void pt_copy(int pid, addr_t start, size_t size) {
    if (start == 0)
        return;
    byte_t byte;
    for (addr_t addr = start; addr < start + size; addr++) {
        (void) user_get(addr, byte);
        pt_write8(pid, addr, byte);
    }
}

// Please don't use unless absolutely necessary.
static void pt_copy_to_real(int pid, addr_t start, size_t size) {
    byte_t byte;
    for (addr_t addr = start; addr < start + size; addr++) {
        pt_readn(pid, addr, &byte, sizeof(byte));
        (void) user_put(addr, byte);
    }
}

static void step_tracing(struct cpu_state *cpu, struct tlb *tlb, int pid, int sender, int receiver) {
    // step fake cpu
    cpu->tf = 1;
    int changes = cpu->mem->changes;
    int interrupt = cpu_step32(cpu, tlb);
    if (interrupt != INT_NONE) {
        cpu->trapno = interrupt;
        // hack to clean up before the exit syscall
        if (interrupt == INT_SYSCALL && cpu->eax == 1) {
            if (kill(pid, SIGKILL) < 0) {
                perror("kill tracee during exit");
                exit(1);
            }
        }
        handle_interrupt(interrupt);
    }
    if (cpu->mem->changes != changes)
        tlb_flush(tlb);

    // step real cpu
    // intercept cpuid, rdtsc, and int $0x80, though
    struct user_regs_struct regs;
    errno = 0;
    getregs(pid, &regs);
    long inst = trycall(ptrace(PTRACE_PEEKTEXT, pid, regs.rip, NULL), "ptrace get inst step");
    long saved_fd = -1; // annoying hack for mmap
    long old_sp = regs.rsp; // so we know where a sigframe ends

    if ((inst & 0xff) == 0x0f) {
        if (((inst & 0xff00) >> 8) == 0xa2) {
            // cpuid
            do_cpuid((dword_t *) &regs.rax, (dword_t *) &regs.rbx, (dword_t *) &regs.rcx, (dword_t *) &regs.rdx);
            regs.rip += 2;
        } else if (((inst & 0xff00) >> 8) == 0x31) {
            // rdtsc, no good way to get the same result here except copy from fake cpu
            regs.rax = cpu->eax;
            regs.rdx = cpu->edx;
            regs.rip += 2;
        } else {
            goto do_step;
        }
    } else if ((inst & 0xff) == 0xcd && ((inst & 0xff00) >> 8) == 0x80) {
        // int $0x80, intercept the syscall unless it's one of a few actually important ones
        dword_t syscall_num = (dword_t) regs.rax;
        switch (syscall_num) {
            // put syscall result from fake process into real process
            case 3: // read
                pt_copy(pid, regs.rcx, cpu->edx); break;
            case 7: // waitpid
                pt_copy(pid, regs.rcx, sizeof(dword_t)); break;
            case 13: // time
                if (regs.rbx != 0)
                    pt_copy(pid, regs.rbx, sizeof(dword_t));
                break;
            case 43:
                pt_copy(pid, regs.rbx, sizeof(struct tms_)); break;
            case 54: { // ioctl (god help us)
                struct fd *fd = f_get(cpu->ebx);
                if (fd && fd->ops->ioctl_size) {
                    ssize_t ioctl_size = fd->ops->ioctl_size(fd, cpu->ecx);
                    if (ioctl_size >= 0)
                        pt_copy(pid, regs.rdx, ioctl_size);
                }
                break;
            }
            case 85: // readlink
                pt_copy(pid, regs.rcx, regs.rdx); break;
            case 102: { // socketcall
                dword_t args[6];
                (void) user_get(regs.rcx, args);
                dword_t len;
                switch (cpu->ebx) {
                    case 6: // getsockname
                        (void) user_get(args[2], len);
                        pt_copy(pid, args[1], len);
                        break;
                    case 8: // socketpair
                        pt_copy(pid, args[3], sizeof(dword_t[2]));
                        break;
                    case 12: // recvfrom
                        pt_copy(pid, args[1], args[2]);
                        (void) user_get(args[5], len);
                        pt_copy(pid, args[4], len);
                        break;
                }
                break;
            }
            case 104: // setitimer
                pt_copy(pid, regs.rdx, sizeof(struct itimerval_)); break;
            case 116: // sysinfo
                pt_copy(pid, regs.rbx, sizeof(struct sys_info)); break;
            case 122: // uname
                pt_copy(pid, regs.rbx, sizeof(struct uname)); break;
            case 140: // _llseek
                pt_copy(pid, regs.rsi, 8); break;
            case 145: { // readv
                struct io_vec vecs[regs.rdx];
                (void) user_get(regs.rcx, vecs);
                for (unsigned i = 0; i < regs.rdx; i++)
                    pt_copy(pid, vecs[i].base, vecs[i].len);
                break;
            }
            case 162: // nanosleep
                pt_copy(pid, regs.rcx, sizeof(struct timespec_)); break;
            case 168: // poll
                pt_copy(pid, regs.rbx, sizeof(struct pollfd_) * regs.rcx); break;
            case 183: // getcwd
                pt_copy(pid, regs.rbx, cpu->eax); break;

            case 195: // stat64
            case 196: // lstat64
            case 197: // fstat64
                pt_copy(pid, regs.rcx, sizeof(struct newstat64)); break;
            case 300: // fstatat64
                pt_copy(pid, regs.rdx, sizeof(struct newstat64)); break;
            case 220: // getdents64
                pt_copy(pid, regs.rcx, cpu->eax); break;
            case 265: // clock_gettime
                pt_copy(pid, regs.rcx, sizeof(struct timespec_)); break;
	    case 340: // prlimit
		if (regs.rsi != 0) pt_copy(pid, regs.rsi, sizeof(struct rlimit_)); break;

            case 355: // getrandom
                pt_copy(pid, regs.rbx, regs.rcx); break;

            case 90: // mmap
            case 192: // mmap2
                if (cpu->eax < 0xfffff000 && cpu->edi != (dword_t) -1) {
                    // fake mmap didn't fail, change fd
                    saved_fd = regs.rdi;
                    regs.rdi = transmit_fd(pid, sender, receiver, cpu->edi);
                }
                goto do_step;

            // some syscalls need to just happen
            case 45: // brk
            case 91: // munmap
            case 125: // mprotect
            case 173: // rt_sigreturn
            case 174: // rt_sigaction
            case 175: // rt_sigprocmask
            case 243: // set_thread_area
                goto do_step;
        }
        regs.rax = cpu->eax;
        regs.rip += 2;
    } else {
do_step:
        setregs(pid, &regs);
        // single step on a repeated string instruction only does one
        // iteration, so loop until ip changes
        unsigned long ip = regs.rip;
        int was_signal;
        while (regs.rip == ip) {
            was_signal = step(pid);
            getregs(pid, &regs);
        }
        if (saved_fd >= 0) {
            remote_close_fd(pid, regs.rdi, ip);
            regs.rdi = saved_fd;
        }

        if (was_signal) {
            // copy the return address
            pt_copy(pid, regs.rsp, sizeof(addr_t));
            // and copy the rest the other way
            pt_copy_to_real(pid, regs.rsp + sizeof(addr_t), old_sp - regs.rsp - sizeof(addr_t));
        }
    }
    setregs(pid, &regs);
}

static void prepare_tracee(int pid) {
    transplant_vdso(pid, vdso_data, sizeof(vdso_data));

    // copy the stack
    pt_copy(pid, 0xffffd000, 0x1000);
    struct user_regs_struct regs;
    getregs(pid, &regs);
    regs.rsp = current->cpu.esp;
    setregs(pid, &regs);

    // find out how big the signal stack frame needs to be
    __asm__("cpuid"
            : "=b" (xsave_extra)
            : "a" (0xd), "c" (0)
            : "edx");

    int features_ecx, features_edx;
    __asm__("cpuid"
            : "=c" (features_ecx), "=d" (features_edx)
            : "a" (1)
            : "ebx");
    // if xsave is supported, add 4 bytes. why? idk
    if (features_ecx & (1 << 26))
        xsave_extra += 4;
    // if fxsave/fxrestore is supported, use 112 bytes for that
    if (features_edx & (1 << 24))
        fxsave_extra = 112;

}

int main(int argc, char *const argv[]) {
    int err = xX_main_Xx(argc, argv, NULL);
    if (err < 0) {
        fprintf(stderr, "%s\n", strerror(-err));
        return err;
    }

    // execute the traced program in a new process and throw up some sockets
    char exec_path[MAX_PATH];
    if (path_normalize(AT_PWD, argv[optind], exec_path, true) != 0) {
        fprintf(stderr, "enametoolong\n"); exit(1);
    }
    struct mount *mount = find_mount_and_trim_path(exec_path);
    int fds[2];
    trycall(socketpair(AF_UNIX, SOCK_DGRAM, 0, fds), "socketpair");
    int pid = start_tracee(mount->root_fd, fix_path(exec_path), argv + optind, (char *[]) {NULL});
    int sender = fds[0], receiver = fds[1];
    /* close(receiver); // only needed in the child */
    prepare_tracee(pid);

    struct cpu_state *cpu = &current->cpu;
    struct tlb *tlb = tlb_new(cpu->mem);
    int undefined_flags = 2;
    struct cpu_state old_cpu = *cpu;
    int i = 0;
    while (true) {
        if (compare_cpus(cpu, tlb, pid, undefined_flags) < 0) {
            printk("failure: resetting cpu\n");
            *cpu = old_cpu;
            __asm__("int3");
            cpu_step32(cpu, tlb);
            return -1;
        }
        undefined_flags = undefined_flags_mask(cpu, tlb);
        old_cpu = *cpu;
        step_tracing(cpu, tlb, pid, sender, receiver);
        i++;
    }
}

// useful for calling from the debugger
void dump_memory(int pid, const char *file, addr_t start, addr_t end) {
    FILE *f = fopen(file, "w");
    for (addr_t addr = start; addr <= end; addr += sizeof(dword_t)) {
        dword_t val = pt_read(pid, addr);
        fwrite(&val, sizeof(dword_t), 1, f);
    }
    fclose(f);
}
