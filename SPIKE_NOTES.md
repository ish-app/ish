# iSH x86_64 guest: working notes (started 2026-11-13, branch x86_64-spike)

## Status (2026-11-14)
Experimental. A C interpreter for x86_64 guests plus the 64-bit Linux layer, behind
`-Dguest=x86_64` (Xcode: `ISH_GUEST = x86_64`). The 32-bit build still passes its tests.

Verified: `tests/guest64/run_all.sh` 14 of 14 groups on an x86_64 Linux host and on an arm64
Linux build under qemu-aarch64; macOS native CLI build (at an earlier commit); the app runs on
an iPhone (A20) and an iPad Air M2 and gives an Alpine 3.19 x86_64 shell in which apk, Python,
Node.js, Go, git, OpenSSH and Claude Code work.

Known issues:
- Interpreter only, no JIT. On the x86_64 Linux host it measured somewhat faster than stock 32-bit iSH on the same commands; not compared on iOS.
- No AVX/AVX2/BMI/AES-NI (cpuid reports x86-64-v2). No vdso, SysV IPC, signalfd, PTRACE_SYSCALL.
- iOS gives an app about 10 GB of address space. Large anonymous mappings are backed lazily
  now; programs that reserve very large regions and touch much of them can still run out.
- The last commit (execve ends sibling threads; epoll_wait retains its descriptor) is tested on
  Linux hosts only, not yet on a device. Before it, a threaded program that re-executed itself
  (Claude Code's `/tui` switch) crashed on the iPad.
- Terminal view: the keyboard-padding change ("Terminal: measure the keyboard padding again
  after every layout") is untested on a device. A report from an iPad with a hardware keyboard
  (no on-screen keyboard): typing scrolls the view to a blank area until Return. Cause not
  established; that commit is the first suspect. Scroll position also jumps on rotation.
- One unexplained watchdog termination on the iPad (main thread unresponsive for 10 s).
- execve called from a thread that is not the group leader does not take over the leader's id.
- The app's bundled offline package repository is 32-bit only; a 64-bit root needs the network.
- Paths like `~/src/ish64-roots/...` below are the development machine's test roots
  (Alpine 3.19 x86_64 minirootfs copies); the test scripts take them from there.

The sections below are a chronological log; later sections supersede earlier ones.

**Read the section "Second pass" at the end first: it supersedes several statements above it
(addresses are 64-bit now, more instructions and system calls exist, the test set is different).**

Goal: find out how hard a 64-bit (x86_64) guest is for iSH. Upstream base: ce96e698.

## Build and run
    CC=clang meson setup build64r -Dguest=x86_64 -Dbuildtype=release      # interpreter only
    CC=clang meson setup build64i -Dguest=x86_64 -Dlog=strace -Dunicorn_ref=true -Dbuildtype=debugoptimized  # + reference CPU, needs libunicorn-dev
    ninja -C build64r
    build64r/ish -f <a COPY of ~/src/ish64-roots/alpine64-clean> /bin/sh
    ~/src/ish64-roots/run_checks.sh        # 15 shell checks
32-bit build is `build32` (`build` is a symlink to it, the e2e test expects ./build/ish).
strace log goes to fd 666 (`666>file`). `ISH_UC_STATS=1` prints engine counters to stderr.
Never run two ish instances on the same root at once (shared sqlite db + apk lock).

## Design choices
- `-Dguest=x86_64` defines ISH_GUEST_64. All 64-bit code is behind it; the 32-bit build is unchanged in behavior.
- **Addresses stay 32-bit (`addr_t`).** A 64-bit process gets an address space below 4 GB; the kernel
  picks every address (PIE base, interpreter, stack, mmap, brk), so page tables and ~640 addr_t uses are untouched.
  Cost: MAP_FIXED above 4 GB fails, no process can use more than 4 GB.
- `uword_t`/`sword_t` (misc.h) = the guest's long. Guest-visible structs were widened in place
  (timeval, timespec, iovec, rusage, sigaction, stack_t, siginfo, msghdr, cmsghdr, sys_info, aux_ent) so one
  definition serves both guests. stat and statfs have a separate x86_64 layout.
- kernel/calls64.c: x86_64 syscall table. Existing sys_ functions are reused; wrappers only for mmap, lseek,
  truncate, clone (argument order), get/setrlimit. Dispatch in handle_interrupt (calls.c).
- kernel/signal.c: x86_64 rt_sigframe + FXSAVE block, sys64_rt_sigreturn. x87 stack registers are NOT saved.
- kernel/exec.c + elf.h: ELF64 loader, 8-byte stack words, no vdso.
- emu/unicorn64.c: CPU = unicorn (QEMU TCG). Scaffold only, a JIT, cannot ship on iOS.
  Pages are mapped into unicorn lazily, one region per page.

## State (2026-11-13, after page batching)
- 64-bit busybox/ash from Alpine 3.19 x86_64 runs: fork, exec, pipes, files, signals (trap), float, sha256.
- run_checks.sh: 14/15. The failing one (`read n < file`, ash polls a regular file) hangs the same way in the 32-bit build.
- 32-bit `meson test`: 2/2 OK (e2e 5/5) with all shared changes.
- `apk update` 46 s (32-bit asbestos build: 10 s). `apk add python3` works (32 packages, 52 MiB).
- Python 3.11 runs: bigints, math, hashlib (matches host), json, 8 threads, subprocess. Root with python kept at
  ~/src/ish64-roots/alpine64-python; test script py_only.sh; timing script time_apk.sh.

## Unicorn scaffold: what was learned
- Cost is per REGION: a map costs ~0.1 ms at ~100 regions, ~5 ms at ~1200; hard assert above 4096.
  So pages are mapped as runs (contiguous in guest AND host, same writability), cap 400 regions then flush
  (`ISH_UC_MAX_REGIONS`), and kernel/memory.c asks for anonymous memory at host_base + guest address
  (MAP_FIXED_NOREPLACE hint, Linux only) so neighbors merge.
- Never replace a mapping or call uc_emu_stop from inside a memory hook: stop at a block boundary (want_stop),
  swap after the run (remap_pending). Copy-on-write pages are copied on first touch for that reason.
- After an aborted fault unicorn re-reports it as int 13/14 on the next run (stale_fault).
- Locked instructions are not atomic across unicorn instances: guest threads deadlocked in musl locks.
  One guest thread executes at a time now (serial_lock; `ISH_UC_PARALLEL=1` turns that off).
- Remaining slowness vs 32-bit is probably retranslation after every unmap plus the per-block hook (not measured).

## The 64-bit interpreter (2026-11-13, emu/interp64*.c/.h)
Portable C, no generated code, no assembly: `emu/interp64.c` (decoder, generic executor `exec()`, block cache,
run loop), `interp64_fast.h` (specialized threaded handlers + `pick_handler`), `interp64_sse.h` (SSE/SSE2),
`interp64_x87.h` (x87 through ish's emu/fpu.c soft-float), `interp64_check.h` (lockstep checker).
- Builds: `build64i` = debugoptimized + strace log + `-Dunicorn_ref=true` (reference CPU linked in);
  `build64r` = release, no unicorn at all (`ldd` shows none). `build64` is the old unicorn-only debug build.
- `ISH_ENGINE=check` (build64i): every instruction runs on the interpreter, is rolled back, runs on unicorn, and
  registers/flags/xmm/written memory are compared. `ISH_CHECK_LIMIT=n` compares each address n times (0 = always).
  `ISH_ENGINE=unicorn` runs on unicorn only. Not compared: x87 (D8-DF), fxsave/fxrstor, cpuid, rdtsc.
- `ISH_STATS=1`: instructions per second on stderr.
- Design: decode once into blocks of `struct insn` (per-thread cache, validated by per-page generation numbers
  that memory.c bumps through `asbestos_invalidate_page`); each insn has a handler pointer; handlers tail-call
  the next (`musttail`, clang). Flags are kept as source values (zr/sn/pr/aux/cf/of) in `struct ictx`.
  Faults longjmp out; instructions do all memory access before changing registers so they can restart.
  Locked instructions use real compare-and-swap on host memory, so guest threads run in parallel.
  `cpu->zero_reg`, `fs_base`, `gs_base` sit behind `regs[]` so "no base/index" and segment bases index like registers.
- Not implemented: MMX-register instructions, SSE3 and later (cpuid reports baseline x86-64 only), enter, bcd,
  fsave/frstor, far calls. An unimplemented instruction prints "interp64: unimplemented instruction" + bytes.
- Measured on the development machine (Ryzen 9 3900X, x86_64 Linux), same commands: shell loop of 200k iterations 22 s (stock 32-bit iSH 33.5 s),
  sha256 of 16 MB 2.5 s (32-bit 4.1 s), `apk update` 7 s (32-bit 10 s, unicorn 46 s), Python test 1.2 s (unicorn 5 s).
  run_checks 14/15 on build64i and build64r (same known failure); lockstep: 56.7 M instructions of shell/awk/sha256
  and 400k distinct-address samples of Python, 0 mismatches.
- Found on the way: kernel/signal.c sys_rt_sigaction read an uninitialized struct in its STRACE line; with
  optimization + strace logging clang turned that into EFAULT for every sigaction(sig, NULL, &old). Initialized it.
- arm64 host: cross-built (`build64arm`, clang --target=aarch64-linux-gnu, static, cross file and SQLite source in
  `~/src/ish64-arm/`) and run under `qemu-aarch64` (user-mode emulation of an ARM64 Linux host):
  run_checks 14/15, torture test identical to native, Python test passes. Not run on real ARM hardware or iOS.
  meson.build now builds the CLI in a cross build when an exe wrapper exists.
- Tests: `~/src/ish64-roots/tests/run_torture.sh` compiles `tests/torture.c` with gcc INSIDE the guest
  (root `alpine64-dev`: gcc, musl-dev, git, openssh-client, make) and runs the static binary both natively on
  the host and in ish; output is identical at -O0 and -O2 (integers, SSE floats, long double, strings, qsort/setjmp,
  4 threads with atomics). `tests/sig.c`: a 2 ms timer signal whose handler uses x87/SSE leaves an interrupted
  long-double sum bit-identical. In the guest: git clone over HTTPS, `make -j4 test` of zlib passes, `ssh -V` runs.
- Fixed in shared code on the way (32-bit build benefits too, its tests still pass 2/2):
  `f80_mod` (fprem) was inexact, now exact integer remainder; `f80_sqrt` was off by one ulp in ~23% of cases, now
  correctly rounded; pselect6's {sigmask, size} argument is two guest longs (make -j failed with EINVAL);
  64-bit signal frames now carry the x87 registers.
- Guest programs need PATH in the environment (gcc could not find cc1 without it); `ish ... /bin/sh -c` gets none.

## Not done
- Syscalls not mapped yet (seen missing: 285 fallocate by apk, 324 membarrier by python): waitid, getdents (old), fallocate, timerfd/timer_*, ptrace, sysv ipc, flock structs in fcntl,
  getitimer, clock_nanosleep, accept4, and more. /proc and ptrace code still assume 32-bit registers.
- vdso, core dumps.
- Speed ideas not done: chain blocks directly (skip the hash lookup on taken branches), handlers for more SSE
  arithmetic, lazy flags with fewer stores. x87 is soft-float and slow (same as 32-bit iSH).

## Second pass (2026-11-13)

### What changed
- **Address space: 8 TB instead of 4 GB.** `addr_t` is 64 bits for the 64-bit guest (`uword_t`), page numbers stay
  32 bits (`MEM_PAGES` = 2^31), `PAGE()` saturates so an out-of-range address is simply an unmapped page.
  Three-level page table (kernel/memory.c `mem_leaf`), 64-bit TLB keys (emu/tlb.h), `pt_find_hole` falls back to
  the space above 4 GB, mmap/munmap/mprotect/mremap/brk take 64-bit lengths and return wide results.
  Layout of a new process is unchanged (everything below 4 GB); high memory is used on request (hints, MAP_FIXED)
  or when the low part is full. Host memory is mapped MAP_NORESERVE. Page tables are still one entry per page,
  so reserving many GB costs time and memory in proportion. `-Wno-format` for guest64: most log lines still
  print addresses with %x (truncated in the log only).
- **Instructions:** MMX, SSE3, SSSE3, SSE4.1, SSE4.2 (incl. pcmpXstrX, crc32), popcnt, movbe; cpuid reports
  x86-64-v2 + movbe. Three-byte maps decode as op 0x200|b (0F 38) and 0x300|b (0F 3A); `emu/interp64_sse4.h`.
  NaN results follow the x86 SSE rules on any host (`nf`/`nd` in interp64_sse.h); built with `-ffp-contract=off`.
- **Self-modifying code:** pages instructions were decoded from are tracked exactly (`space->code` bitmaps);
  such pages never get a cached writable TLB entry and every write to them drops their decoded blocks
  (`xlat_w_miss`). Node.js 20 (V8 JIT) runs.
- **Block cache:** multiplicative hash, two entries per bucket (the old hash made hot blocks evict each other).
- **x87 soft-float (emu/float80.c, shared with the 32-bit build):** add, sub, mul, div, mod, to_double rewritten
  to round once (`f80_round_pack`); new `f80_to_float`; sqrt by integer square root; NaN operand rules and the
  negative default NaN as on hardware; 0*x, 0/x, 0/0 fixed. `tests/guest64/f80fuzz.c` compares every routine
  with this machine's real x87 in all four rounding modes: 0 differences in 100 M cases per operation.
- **System calls** (kernel/calls64.c table; new ones also registered for the 32-bit guest): getitimer,
  timer_gettime/getoverrun, timerfd_gettime, clock_nanosleep, accept4, preadv/pwritev(2), close_range,
  faccessat2, getcpu, getdents (long-sized), fallocate, waitid, utime, sched_*, mlock*, timer_*, timerfd_*,
  splice/copy_file_range (iSH's stubs), xattr stubs, ptrace (64-bit register layouts, FXSAVE fpregs),
  futex WAIT_BITSET/WAKE_BITSET/CMP_REQUEUE, fcntl record locks with the 64-bit struct flock, prctl GET_NAME etc,
  CPU-time clocks (2, 3 and pthread_getcpuclockid ids), MONOTONIC_RAW/COARSE/BOOTTIME.
- **Bugs fixed in the shared kernel layer** (all found by the tests below; most affect the 32-bit guest too):
  poll on a regular file hung on Linux hosts (epoll EPERM) = the old "15th check"; unix-socket connect() blocked
  until accept() (now a token handshake, `unix_pending`); futex timed waits expired at once on Linux hosts
  (COND_INITIALIZER vs monotonic clock); F_SETLKW never blocked (flag swapped); waitid returned a raw status word;
  timer_create(NULL sigevent) faulted and its signals went to nobody; sigaltstack was per process instead of per
  thread and ignored SA_ONSTACK (Go died with "morestack on gsignal"); exit_group/halt with threads still exiting
  crashed or killed iSH with SIGTERM; poll free-list race and poll_destroy race (Go build crashed iSH);
  copy-on-write and stack-growth races between threads (host segfault in mem_ptr); truncate() of a relative path
  aborted iSH (assert); mremap rounded lengths down (shrinking below a page unmapped everything) and took no lock;
  time() wrote 4 bytes into an 8-byte time_t; inherited stdio reported mode 0 from fstat (node printed nothing).
- kernel/memory.c no longer pins anonymous memory at host_base + address unless the unicorn reference engine
  is in use (`mem_anon_hint`): pinned pages cannot be merged by the host kernel into few mappings.

### Tests (sources in `tests/guest64/`, run from `~/src/ish64-roots/tests/`)
- `tests/run_all.sh x86|arm`: shell checks 15/15; torture (-O0, -O2), simd (168 lines), nan (589 lines) identical
  to the native run of the same binaries; sys (40 checks), as64 (22), ptrace (15), smc (8), sig; python script.
  2026-11-13: 0 failed on build64r and on build64arm under qemu-aarch64. It leaves two temporary root copies
  behind and prints their names.
- Guest binaries are built inside the guest: `tests/guest_cc.sh name [gcc flags]` (root `alpine64-dev`).
- Lockstep (`ISH_ENGINE=check`): 0 mismatches for torture, simd, sys, as64, python. Not compared (reference
  cannot): x87, MMX, popcnt, movbe, pcmpXstrX, fxsave, cpuid, rdtsc, tzcnt/lzcnt-as-bsf/bsr. Known reference
  differences that are NOT interpreter bugs (native agrees with the interpreter): SSE operations with two NaN
  operands (nan test, 10 reports), stale code after self-modification (smc test).
- Root `alpine64-big` (dev + go, nodejs, python3-tests, lua): Go 1.21 builds and runs tests/guest64/hello.go;
  node 20 runs t.js with the same output as the host's node; zlib `make -j4 test` OK; `python3 -m test` on 37
  modules (math, float, decimal, struct, json, threading, io, ...): all OK, 6,788 tests.
- Python OS-level modules still failing (not analyzed one by one; may be general iSH gaps rather than 64-bit ones):
  test_epoll test_fcntl test_glob test_os test_poll test_posix test_pty test_resource test_shutil test_signal
  test_subprocess, one case in test_mmap. test_re fails two locale cases that musl cannot pass anywhere.
  test_fcntl/test_poll/... can block when run alone, so run them with `--timeout`.
- 32-bit build (`build32`): `meson test` 2/2 OK after all shared changes. Nothing else was re-tested on it.

### Still not done
- AVX/AVX2/BMI/FMA (x86-64-v3, needs a VEX decoder), AES-NI, SHA. rcpps/rsqrtps are exact reciprocals, not the
  hardware's approximations. x87 precision control and exception flags are not modeled; x87 stays soft-float
  (about 10% faster than before, exact now).
- No SysV IPC, inotify, signalfd, memfd_create, clone3, io_uring, PTRACE_SYSCALL (so no strace; gdb untested).
- Process CPU-time clock counts the calling thread only. No vdso. No core dumps.
- Never run on real ARM hardware or iOS; the Xcode project is untouched.
- Other threads' cached writable TLB entries for a page that just became a code page are dropped at their next
  TLB miss or kernel entry, not instantly (a short window in which a cross-thread code patch can be missed).

## Third pass (2026-11-13: commits, Python OS-test triage, macOS and iOS)

### Commits on `x86_64-spike`
- `f31fa783` shared kernel-layer fixes (builds and passes the 32-bit tests alone)
- `8fc4ede7` the x86_64 guest: interpreter, 64-bit Linux layer, tests
- `ac79328d` more shared fixes found with Python's OS test modules
- `be740ad8` Xcode setting `ISH_GUEST`, syscall table entries, test updates
- `2d97433b` recvfrom fixes
- `09173eda` F_SETPIPE_SZ on hosts without it (macOS/iOS)
- `a142a344` app: no built-in-APK prompt for the 64-bit guest; `INSTALL_IOS_64.md`

### Triage of Python's OS-level test modules
None of the failures was specific to the 64-bit build: the same cases failed in the 32-bit
build, and the fixes are in shared code. After the fixes both builds pass 12 of the 14 modules
(test_posix, test_subprocess, test_signal, test_epoll, test_poll, test_pty, test_glob,
test_resource, test_shutil, test_mmap, test_selectors, test_select). Left:
- `test_os`: one case, `fpathconf` on a bad fd (musl answers without asking the kernel).
- `test_fcntl`: one case, `F_NOTIFY` (directory notifications; not implemented).
- `test_socket`: most pass; the failures are missing features: IPv6 ancillary data (RFC 3542),
  UDP-Lite, abstract-namespace unix sockets.
Bugs fixed along the way include: dup3 with a negative fd (heap overflow, found with ASan),
fstatfs NULL deref, listen() twice corrupting a list, SCM_RIGHTS overflow, poll hanging on a
closed fd, FIFO + O_DIRECTORY hang, pty EIO and packet mode, sigtimedwait, edge-triggered epoll,
i386 fstatfs64 argument order, readv/writev total-length overflow, memfd_create.

### Hosts
- x86_64 Linux: `tests/run_all.sh x86` 12/12 groups.
- arm64 Linux under qemu-aarch64: `tests/run_all.sh arm` 12/12 groups.
- macOS 26 (Intel Mac), native CLI build: all groups, OS behaviors 30/30.
- iOS: `xcodebuild ... -sdk iphoneos ARCHS=arm64 ISH_GUEST=x86_64` builds (unsigned).
  The 32-bit app still builds too.
- iOS simulator (x86_64 host): the real app boots Alpine 3.19 x86_64 to a shell;
  `uname -m` = x86_64, bigint and float output right, sha256 matches the host.
- Real ARM hardware: NOT run yet. Needs a signed build (see `INSTALL_IOS_64.md`).

### Still not done
AVX/AVX2/BMI/AES-NI (cpuid reports x86-64-v2, so well-behaved programs do not use them);
PTRACE_SYSCALL (no strace; gdb untested); SysV IPC, inotify, signalfd; process CPU-time clock;
the socket features above. The app's bundled offline package repository is 32-bit only, so a
64-bit root needs the network for `apk`.

## Fourth pass (2026-11-14): Claude Code in the guest
Claude Code can be installed in the 64-bit build:
`apk add nodejs npm libstdc++ libgcc && npm install -g @anthropic-ai/claude-code` (the package is a
352 MB Bun executable, musl build; npm's Node 22 warning does not matter). `claude --version`,
`--help`, `doctor`, `-p` and the interactive screen work; `claude -p hi` answers
"Not logged in" in 13 s on the Linux host, 21 s natively on an Intel Mac, 94 s on the arm64 build
under qemu. A logged-in session had not been run at this point (see the fifth pass).
Test root: `~/src/ish64-roots/cc-test` (also has a standalone `bun`).

Three bugs had to be fixed, all found with this one program:
- `f132327f` madvise(MADV_DONTNEED) was a stub; Linux promises zeroed pages and WebKit's
  allocator relies on it (stale memory handed out as zeroed: hangs in a hash-table probe,
  random segfaults). Same commit: mprotect dropped copy-on-write/shared/anonymous page flags
  (a child that called mprotect and wrote changed the parent's memory). Both are stock iSH bugs.
- `2a321e8b` cmpxchg16b was a mutex, not a real 16-byte compare-and-swap.
- `de307682` on macOS/iOS hosts sysinfo() reported 0 bytes of RAM, so JavaScriptCore collected
  garbage forever. Would have hit iOS devices. /proc/meminfo units fixed too.
How they were found: `ISH_SAMPLE=1` (new) logs each thread's rip ~20 times a second to fd 666;
the syscall trace of `build64i` also goes to fd 666 (`666>file`).
Still missing, seen in the log but harmless so far: inotify_init1 (294), pidfd_open (434);
/proc/self/statm reports zeros.

## Fifth pass (2026-11-14): inotify, pidfd, statm; a logged-in Claude Code session; ssh
- `5d2dd6fb` /proc/pid/statm and stat report mapped and touched pages (were zeros).
- `37a31729` EPOLLONESHOT entries are disarmed, not removed (Bun lost child output). Stock iSH bug.
- `c6b30ea2` inotify (events generated by the file layer, watches by mount + inode),
  pidfd_open / pidfd_send_signal / waitid(P_PIDFD), close_range(CLOSE_RANGE_CLOEXEC).
- follow-up commit: an open /proc/<pid> directory works as a pidfd for pidfd_send_signal.
- `tests/guest64/notify.c`: output identical to native Linux on x86, arm64 (qemu) and macOS hosts;
  it is part of `run_all.sh` (13 groups now).
- Python OS modules, both guests: 17 of 20 pass; left are the same three as before
  (fpathconf, F_NOTIFY, socket features).
- inotify limits: no events for changes made outside the guest, for writes through shared
  mappings, or IN_ACCESS.

Logged-in Claude Code 2.1.325 in the guest on the Linux host: `-p` prompts, Write/Edit/Grep/Glob/Bash/WebFetch tools, git, and the interactive
screen all worked. The Bash tool needs `apk add bash` and `SHELL=/bin/bash` (Alpine has only ash).
ssh: OpenSSH 9.6 client in the guest (ed25519 + RSA keys, pty, pipes, scp both ways, -L forward,
three ciphers, git over ssh) and sshd in the guest (commands, pty, scp, sftp) both work.
scp runs at about 2-5 MB/s on the Linux host: the ciphers run in the interpreter.

## Sixth pass (2026-11-14): first run on real ARM hardware
Installed on an iPhone (A20, iOS 27.2.1) and an iPad Air M2 (iPadOS 27.2.1), signed with a free
Apple ID team and installed with `pymobiledevice3 apps install` (Xcode 26.5 has no developer
disk image for these OS versions, and Xcode 27 does not run on an Intel Mac).
Phone results (run by Claude Code inside the app): every check
correct, nothing failed. Wall times, phone vs the Linux host (Ryzen 9 3900X), same commands:
sh loop 200k 11.0 s vs 20.6 s; 20 MB sha256sum 2.0 vs 2.6; python sum of squares 2.4 vs 5.0;
node 3e7 loop 1.3 vs 2.8; node 2e6 objects 4.6 vs 8.7. So about 2x the Linux host.
Found on the phone:
- CPU times were garbage (`time sh -c true`: 14 s user). Commit after this note's parent:
  thread_info unchecked + mach_thread_self port leak. The cause on iOS is inferred, not
  confirmed; the fix logs once if thread_info fails.
- /proc/loadavg missing: added.
- Inner display: iOS letterboxes apps not built with Xcode 27 (48 columns). Needs an
  Apple-silicon Mac to build for the full width.
- Scroll position jumps on rotation, inner display only. Not investigated (terminal view /
  hterm scroll sync; cannot be reproduced without the device).

## Seventh pass (2026-11-14): iPad findings
- "Large anonymous mappings get host memory when first touched": the iPad build died with a
  NULL write in the decoder once the app's ~10 GB address space was used up (Node.js could not
  reserve its code range; a second Claude Code process segfaulted now and then). Private
  anonymous mappings of 1 MB or more, and PROT_NONE reservations, now have no host memory
  until a page is touched (`P_RESERVED`, `pt_back`); a failed block-cache allocation aborts
  with a message instead of writing through NULL.
- "mremap: ...": a regression from the statm counters (per-page bookkeeping bits made adjacent
  pages look different) and MREMAP_MAYMOVE, which was missing.
- "execve ends the process's other threads; epoll_wait keeps its descriptor": execve left the
  process's other threads running on the address space it had just destroyed (stock iSH
  behavior), and a thread blocked in epoll_wait used the descriptor after another thread closed
  it. `tests/guest64/execthr.c` (nine threads, re-executes itself) is in `run_all.sh`.
- A guest fault now logs the page-table state of the faulting address.
