// Lockstep check for interp64.c: every instruction is run by the interpreter
// and then again, from the same starting state, by unicorn. Registers, flags
// and the memory the interpreter wrote are compared. Development tool only.
int unicorn64_run(struct cpu_state *cpu, bool step);

static pthread_mutex_t check_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *check_seen;
static unsigned check_limit = 6;
static unsigned long check_reports, check_compared, check_skipped;

static void check_stats(void) {
    fprintf(stderr, "interp64 check: %lu instructions compared, %lu not compared, %lu mismatches\n",
            check_compared, check_skipped, check_reports);
}

static void check_host_bytes(struct tlb *tlb, uint64_t addr, uint8_t *out, unsigned n, bool write, const uint8_t *in) {
    for (unsigned i = 0; i < n; i++) {
        uint8_t *p = write ? __tlb_write_ptr(tlb, (addr_t) (addr + i)) : __tlb_read_ptr(tlb, (addr_t) (addr + i));
        if (p == NULL)
            continue;
        if (write) *p = in[i]; else out[i] = *p;
    }
}

static const char *const check_reg_names[16] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
};

// MMX registers are not carried over to the reference CPU, so instructions
// that use them are not compared (tests/simd.c compares them with native runs).
static bool check_is_mmx(const struct insn *in) {
    unsigned op = in->op;
    bool simd_pfx = in->pfx & (P_66 | P_F2 | P_F3);
    if (op == 0x12a || op == 0x12c || op == 0x12d)
        return !(in->pfx & (P_F2 | P_F3));
    if (op == 0x1d6)
        return in->pfx & (P_F2 | P_F3);
    if ((op >= 0x160 && op < 0x180) || (op >= 0x1d0 && op < 0x200) || op == 0x1c4 || op == 0x1c5)
        return !simd_pfx;
    if (op >= 0x200 && op < 0x400 && op != 0x2f0 && op != 0x2f1)
        return !simd_pfx;
    return false;
}

static int check_run(struct cpu_state *cpu, struct tlb *tlb) {
    pthread_mutex_lock(&check_lock);
    if (check_seen == NULL) {
        check_seen = calloc(1 << 22, 1);
        if (getenv("ISH_CHECK_LIMIT"))
            check_limit = (unsigned) strtoul(getenv("ISH_CHECK_LIMIT"), NULL, 10);
        atexit(check_stats);
    }
    int result = INT_TIMER;
    for (int n = 0; n < 20000; n++) {
        if (__atomic_exchange_n(cpu->poked_ptr, false, __ATOMIC_SEQ_CST))
            break;
        // the reference CPU resolves pages on its own, which can change the
        // address space behind this TLB's back
        if (tlb->mem_changes != cpu->mmu->changes)
            tlb_flush(tlb);
        struct cpu_state before = *cpu;
        struct ictx c;
        ctx_enter(&c, cpu, tlb);
        c.single = c.log = true;
        c.undef = 0;
        int r;
        if (_setjmp(c.fault))
            r = INT_GPF;
        else
            r = step(&c);
        ctx_leave(&c);
        if (r != INT_NONE) {
            result = r;
            break;
        }
        unsigned op = c.last.op;
        bool skip = (op >= 0xd8 && op <= 0xdf) || op == 0x1ae || op == 0x1a2 || op == 0x131 || c.log_overflow
            // the reference CPU has tzcnt/lzcnt; this one reports no BMI and runs them as bsf/bsr
            || ((op == 0x1bc || op == 0x1bd) && (c.last.pfx & P_F3))
            || check_is_mmx(&c.last)
            // the reference CPU model has no popcnt or movbe, and its
            // pcmpXstrX disagree with real hardware (which tests/simd.c matches)
            || op == 0x1b8 || op == 0x2f0 || op == 0x2f1 || (op >= 0x360 && op <= 0x363);
        size_t h = (before.rip ^ (before.rip >> 22)) & ((1 << 22) - 1);
        if (!skip && check_limit != 0) {
            if (check_seen[h] >= check_limit)
                skip = true;
            else
                check_seen[h]++;
        }
        if (skip) {
            check_skipped++;
            continue;
        }
        check_compared++;
        if (check_compared % 100000 == 0)
            check_stats();

        struct cpu_state after = *cpu;
        for (int i = 0; i < c.nlog; i++)
            check_host_bytes(tlb, c.wl[i].addr, c.wl[i].new, c.wl[i].size, false, NULL);
        int ur = INT_TIMER;
        for (int try = 0; try < 4 && ur == INT_TIMER; try++) {
            for (int i = 0; i < c.nlog; i++)
                check_host_bytes(tlb, c.wl[i].addr, NULL, c.wl[i].size, true, c.wl[i].old);
            *cpu = before;
            ur = unicorn64_run(cpu, true);
        }

        char diff[2048];
        size_t dl = 0;
        diff[0] = 0;
#define DIFF(...) do { if (dl < sizeof(diff) - 200) dl += (size_t) snprintf(diff + dl, sizeof(diff) - dl, __VA_ARGS__); } while (0)
        if (ur != INT_NONE)
            DIFF("  reference stopped with interrupt %d\n", ur);
        for (int i = 0; i < 16; i++)
            if (cpu->regs[i] != after.regs[i])
                DIFF("  %s: before %#llx interp %#llx ref %#llx\n", check_reg_names[i],
                        (unsigned long long) before.regs[i], (unsigned long long) after.regs[i], (unsigned long long) cpu->regs[i]);
        if (cpu->rip != after.rip)
            DIFF("  rip: interp %#llx ref %#llx\n", (unsigned long long) after.rip, (unsigned long long) cpu->rip);
        uint32_t fmask = (ARITH_FLAGS | F_DF) & ~c.undef;
        if ((cpu->eflags ^ after.eflags) & fmask)
            DIFF("  eflags: before %#x interp %#x ref %#x (compared bits %#x)\n", before.eflags & (ARITH_FLAGS | F_DF),
                    after.eflags & fmask, cpu->eflags & fmask, fmask);
        for (int i = 0; i < 16; i++)
            if (cpu->xmm[i].u128 != after.xmm[i].u128)
                DIFF("  xmm%d: before %016llx%016llx interp %016llx%016llx ref %016llx%016llx\n", i,
                        (unsigned long long) before.xmm[i].qw[1], (unsigned long long) before.xmm[i].qw[0],
                        (unsigned long long) after.xmm[i].qw[1], (unsigned long long) after.xmm[i].qw[0],
                        (unsigned long long) cpu->xmm[i].qw[1], (unsigned long long) cpu->xmm[i].qw[0]);
        if (cpu->fs_base != after.fs_base || cpu->gs_base != after.gs_base)
            DIFF("  segment base differs\n");
        for (int i = 0; i < c.nlog; i++) {
            uint8_t now[16] = {0};
            check_host_bytes(tlb, c.wl[i].addr, now, c.wl[i].size, false, NULL);
            if (memcmp(now, c.wl[i].new, c.wl[i].size) != 0) {
                DIFF("  memory %#llx (%u bytes): interp", (unsigned long long) c.wl[i].addr, c.wl[i].size);
                for (unsigned k = 0; k < c.wl[i].size; k++) DIFF(" %02x", c.wl[i].new[k]);
                DIFF(" ref");
                for (unsigned k = 0; k < c.wl[i].size; k++) DIFF(" %02x", now[k]);
                DIFF("\n");
            }
        }
#undef DIFF
        if (dl != 0) {
            check_reports++;
            if (check_reports <= 300) {
                uint8_t bytes[15] = {0};
                check_host_bytes(tlb, before.rip, bytes, 15, false, NULL);
                fprintf(stderr, "interp64 check: MISMATCH at %#llx (op %#x pfx %#x len %d):", (unsigned long long) before.rip,
                        c.last.op, c.last.pfx, c.last.len);
                for (int k = 0; k < c.last.len && k < 15; k++)
                    fprintf(stderr, " %02x", bytes[k]);
                fprintf(stderr, "\n%s", diff);
            }
            check_seen[h] = 255; // do not report this address again
        }
        if (ur != INT_NONE) {
            // the reference did not complete the instruction: keep the interpreter's result
            *cpu = after;
            for (int i = 0; i < c.nlog; i++)
                check_host_bytes(tlb, c.wl[i].addr, NULL, c.wl[i].size, true, c.wl[i].new);
        } else {
            // x87 and MMX state is not carried by the reference
            memcpy(cpu->fp, after.fp, sizeof(cpu->fp));
            memcpy(cpu->mm, after.mm, sizeof(cpu->mm));
            cpu->fsw = after.fsw;
            cpu->fcw = after.fcw;
        }
    }
    cpu->trapno = result;
    pthread_mutex_unlock(&check_lock);
    return result;
}
