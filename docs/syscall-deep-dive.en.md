# Chapter 3: The Magic of Syscalls—Faking a Kernel in User Space

If Asbestos is the "muscles" and "nervous system" of iSH, then the syscall translation layer is its "soul." It is this layer that creates the illusion for Linux programs that they are communicating with a real Linux kernel. This chapter will reveal how iSH performs this "magic."

## 3.1 Why Translate? A Dialogue in Two Languages

Imagine a program that speaks Linux (its system calls) trying to communicate with a system that only understands iOS (the XNU kernel APIs). They need a translator.

| Task | What the Linux Program Says... | What the iOS Kernel Understands... |
| :--- | :--- | :--- |
| Read a file | `sys_read(fd, buffer, count)` | `pread(fd, buffer, count, offset)` |
| Create a process | `sys_fork()` | `posix_spawn()` |
| Get time | `sys_gettimeofday(tv, tz)` | `gettimeofday(tv, tz)` (with subtle differences) |
| Exit program | `sys_exit_group(code)` | `exit(code)` |

The core mission of iSH's `kernel/` directory is to implement a full-featured translation layer in user space to handle these hundreds of different "dialogues."

## 3.2 The Interception Point: The `int 0x80` Convention

How does a Linux program initiate a system call? On the x86 architecture, the traditional way is to execute the `int 0x80` software interrupt instruction. Before executing this instruction, the program places the **system call number** into the `eax` register and the arguments into `ebx`, `ecx`, `edx`, etc.

The Asbestos JIT engine gives special treatment to the `int 0x80` instruction. When it encounters this instruction during compilation, it doesn't generate a real interrupt. Instead, it generates a special Gadget that:
1.  **Pauses JIT execution**: Saves the current CPU state (registers, program counter, etc.).
2.  **Calls a C function**: Invokes iSH's C function, `handle_syscall()`.
3.  **Resumes execution**: After `handle_syscall()` returns, it updates the `eax` register from the C function's return value and continues JIT execution.

This process elegantly transfers control from the high-speed JIT execution flow to the flexible C language processing logic.

## 3.3 The Core Dispatch: The Big Table in `calls.c`

`kernel/calls.c` is the heart of the syscall translation layer. Its key is a massive static array, `syscall_table`.

```c
// kernel/calls.c (simplified)
const struct syscall_def syscall_table[] = {
    // ...
    [__NR_read] = {sys_read, "read"},
    [__NR_write] = {sys_write, "write"},
    [__NR_open] = {sys_open, "open"},
    [__NR_close] = {sys_close, "close"},
    [__NR_fork] = {sys_fork, "fork"},
    [__NR_execve] = {sys_execve, "execve"},
    // ... over 200 more syscalls
};

int handle_syscall(struct task *task) {
    dword_t syscall_num = task->cpu.eax;
    if (syscall_num >= sizeof(syscall_table)/sizeof(syscall_table[0]))
        return -_ENOSYS; // Invalid syscall number

    const struct syscall_def *def = &syscall_table[syscall_num];
    if (def->func == NULL)
        return -_ENOSYS; // Unsupported syscall

    // Call the specific implementation function
    return def->func(task);
}
```

The logic of `handle_syscall` is crystal clear: it uses the syscall number from the `eax` register as an index to find the corresponding handler function in the table and then calls it.

## 3.4 Implementation Deep Dive: From Simple Mapping to Complex Simulation

The implementation of system calls is not always as simple as a table lookup. We can categorize them into several types:

### A. Simple One-to-One Mapping

Many file and time-related system calls have very similar counterparts in iOS (POSIX). For example, `sys_read`:

```c
// kernel/fs.c (simplified)
static int sys_read(struct task *task) {
    // 1. Get arguments from the emulated CPU registers
    int fd_num = task->cpu.ebx;
    addr_t user_buf_addr = task->cpu.ecx;
    size_t count = task->cpu.edx;

    // 2. Convert the emulated file descriptor (fd_num) to an internal iSH fd struct
    struct fd *fd = fd_get(task, fd_num);
    if (fd == NULL) return -_EBADF;

    // 3. Allocate a temporary buffer on the iSH heap
    char *kernel_buf = malloc(count);

    // 4. Call the VFS layer's read operation, which eventually calls iOS's pread or other implementations
    ssize_t bytes_read = fd->ops->read(fd, kernel_buf, count);

    // 5. If the read was successful, copy the data from iSH's buffer to the emulated program's memory space
    if (bytes_read > 0) {
        if (user_write(task, user_buf_addr, kernel_buf, bytes_read) < 0) {
            bytes_read = -_EFAULT;
        }
    }
    
    free(kernel_buf);
    return bytes_read;
}
```
This process reveals a key point: **there is strict memory isolation between the iSH kernel and the emulated Linux program**. Data must be exchanged through functions like `user_read()` and `user_write()`, which handle address translation and permission checks.

### B. State Simulation: The Magic of `fork`

`fork()` is a cornerstone of UNIX, but iOS forbids apps from creating child processes. How does iSH conjure a `fork` out of thin air? The answer is: **simulating processes with threads**.

When `sys_fork` is called, iSH:
1.  **Creates a new `task` struct**: This struct represents a "process" and contains all its information, including CPU state, memory map, file descriptor table, etc.
2.  **Copies the parent's memory**: Calls `pt_copy_on_write()` to efficiently duplicate the parent's entire address space using Copy-on-Write (COW). This means the child shares physical memory pages with the parent until a write occurs, making the operation very cheap.
3.  **Duplicates file descriptors**: Copies the parent's `fdtable` and increments the reference count for each file descriptor.
4.  **Starts a new `pthread` thread**: This thread becomes the execution body of the "child process." Its entry function sets up the `current` task pointer and then jumps into the JIT engine to start execution.
5.  **Provides a clever return value**:
    *   In the "parent process's" `task`, `sys_fork` returns the PID of the newly created child.
    *   In the "child process's" `task`, the `eax` register is set to 0.

Through this series of operations, iSH perfectly simulates multi-process behavior within a single-process application.

### C. Abstraction and Virtualization: The Filesystem Example

Linux programs expect to see a familiar root directory `/`, along with `/proc`, `/dev`, etc. But iOS apps are confined to a sandbox directory. iSH's Virtual File System (VFS) layer (`fs/`) solves this problem.

The VFS defines a standard set of file operation interfaces (`struct fs_ops`), such as `open`, `read`, and `stat`. It then provides specific implementations for different types of filesystems:
*   **`realfs` (`fs/real.c`)**: Maps operations to real files within the iOS sandbox.
*   **`procfs` (`fs/proc/`)**: When a program accesses `/proc/cpuinfo`, it doesn't read a real file. Instead, it calls a function that dynamically generates the emulated CPU information.
*   **`devfs` (`fs/dev.c`)**: Emulates device files like `/dev/null`, `/dev/zero`, and `/dev/random`.

## 3.5 Conclusion: A Well-Crafted "Lie"

The iSH syscall translation layer is a grand and elaborate "lie." Through interception, translation, simulation, and virtualization, it makes Linux programs believe they are running in a standard environment. The quality of this "lie" directly determines the compatibility of iSH.

*   **Compatibility**: With every new system call implemented, iSH's compatibility improves.
*   **Performance**: The efficiency of each syscall implementation, especially for file I/O and process management, is crucial for overall performance.
*   **Security**: This layer also acts as a security boundary, ensuring that emulated programs cannot escape iSH's control to access unauthorized system resources.

Now that we have explored the two main pillars of iSH, we will delve into more details like memory management and the file system in subsequent chapters, further refining our understanding of this ingenious system.