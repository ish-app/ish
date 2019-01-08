#ifndef SIGNAL_H
#define SIGNAL_H

#include "misc.h"
struct task;

typedef qword_t sigset_t_;

#define SIG_ERR_ -1
#define SIG_DFL_ 0
#define SIG_IGN_ 1
struct sigaction_ {
    addr_t handler;
    dword_t flags;
    addr_t restorer;
    sigset_t_ mask;
} __attribute__((packed));

#define NUM_SIGS 64

#define	SIGHUP_    1
#define	SIGINT_    2
#define	SIGQUIT_   3
#define	SIGILL_    4
#define	SIGTRAP_   5
#define	SIGABRT_   6
#define	SIGIOT_    6
#define	SIGBUS_    7
#define	SIGFPE_    8
#define	SIGKILL_   9
#define	SIGUSR1_   10
#define	SIGSEGV_   11
#define	SIGUSR2_   12
#define	SIGPIPE_   13
#define	SIGALRM_   14
#define	SIGTERM_   15
#define	SIGSTKFLT_ 16
#define	SIGCHLD_   17
#define	SIGCONT_   18
#define	SIGSTOP_   19
#define	SIGTSTP_   20
#define	SIGTTIN_   21
#define	SIGTTOU_   22
#define	SIGURG_    23
#define	SIGXCPU_   24
#define	SIGXFSZ_   25
#define	SIGVTALRM_ 26
#define	SIGPROF_   27
#define	SIGWINCH_  28
#define	SIGIO_     29
#define	SIGPWR_    30
#define SIGSYS_    31
#define SIGUNUSED_ 31

// send a signal
void send_signal(struct task *task, int sig);
// send a signal without regard for whether the signal is blocked or ignored
void deliver_signal(struct task *task, int sig);
// send a signal to all processes in a group, could return ESRCH
int send_group_signal(dword_t pgid, int sig);
// check for and deliver pending signals on current
// returns whether signals were received
// must be called without pids_lock
bool receive_signals(void);

struct sighand {
    atomic_uint refcount;
    struct sigaction_ action[NUM_SIGS];
    addr_t altstack;
    dword_t altstack_size;
    bool on_altstack;
    lock_t lock;
};
struct sighand *sighand_new(void);
struct sighand *sighand_copy(struct sighand *sighand);
void sighand_release(struct sighand *sighand);

dword_t sys_rt_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr, dword_t sigset_size);
dword_t sys_sigaction(dword_t signum, addr_t action_addr, addr_t oldaction_addr);
dword_t sys_rt_sigreturn(dword_t sig);

#define SIG_BLOCK_ 0
#define SIG_UNBLOCK_ 1
#define SIG_SETMASK_ 2
typedef uint64_t sigset_t_;
int do_sigprocmask(dword_t how, sigset_t_ set, sigset_t_ *oldset);
dword_t sys_rt_sigprocmask(dword_t how, addr_t set, addr_t oldset, dword_t size);
int_t sys_rt_sigpending(addr_t set_addr);

struct stack_t_ {
    addr_t stack;
    dword_t flags;
    dword_t size;
};
#define SS_ONSTACK_ 1
#define SS_DISABLE_ 2
dword_t sys_sigaltstack(addr_t ss, addr_t old_ss);

int_t sys_rt_sigsuspend(addr_t mask_addr, uint_t size);

dword_t sys_kill(pid_t_ pid, dword_t sig);
dword_t sys_tkill(pid_t_ tid, dword_t sig);

// signal frame structs. There's a good chance this should go in its own header file

// thanks kernel for giving me something to copy/paste
struct sigcontext_ {
    word_t gs, __gsh;
    word_t fs, __fsh;
    word_t es, __esh;
    word_t ds, __dsh;
    dword_t di;
    dword_t si;
    dword_t bp;
    dword_t sp;
    dword_t bx;
    dword_t dx;
    dword_t cx;
    dword_t ax;
    dword_t trapno;
    dword_t err;
    dword_t ip;
    word_t cs, __csh;
    dword_t flags;
    dword_t sp_at_signal;
    word_t ss, __ssh;

    dword_t fpstate;
    dword_t oldmask;
    dword_t cr2;
};

struct fpreg_ {
    word_t significand[4];
    word_t exponent;
};

struct fpxreg_ {
    word_t significand[4];
    word_t exponent;
    word_t padding[3];
};

struct xmmreg_ {
    uint32_t element[4];
};

struct fpstate_ {
    /* Regular FPU environment.  */
    dword_t cw;
    dword_t sw;
    dword_t tag;
    dword_t ipoff;
    dword_t cssel;
    dword_t dataoff;
    dword_t datasel;
    struct fpreg_ st[8];
    word_t status;
    word_t magic;

    /* FXSR FPU environment.  */
    dword_t _fxsr_env[6];
    dword_t mxcsr;
    dword_t reserved;
    struct fpxreg_ fxsr_st[8];
    struct xmmreg_ xmm[8];
    dword_t padding[56];
};

struct sigframe_ {
    addr_t pretcode;
    dword_t sig;
    struct sigcontext_ sc;
    struct fpstate_ fpstate;
    dword_t extramask;
    struct {
        uint16_t popmov;
        dword_t nr_sigreturn;
        uint16_t int80;
    } __attribute__((packed)) retcode;
};

// On a 64-bit system with 32-bit emulation, the fpu state is stored in extra
// space at the end of the frame, not in the frame itself. We store the fpu
// state in the frame where it should be, and ptraceomatic will set this. If
// they are set we'll add some padding to the bottom to the frame to make
// everything align.
extern int xsave_extra;
extern int fxsave_extra;

#endif
