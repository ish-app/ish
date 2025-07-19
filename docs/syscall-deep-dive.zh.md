# 第三章：系统调用的魔法——在用户态伪造一个内核

如果说 Asbestos 是 iSH 的“肌肉”和“神经系统”，那么系统调用转换层就是它的“灵魂”。正是这一层，让 Linux 程序产生了幻觉，以为自己正与一个真正的 Linux 内核对话。本章将揭示 iSH 是如何施展这个“魔法”的。

## 3.1 为何需要转换？两种语言的对话

想象一下，一个讲 Linux 语（系统调用）的程序，想要和一个只懂 iOS 语（XNU 内核 API）的系统沟通。它们之间需要一个翻译。

| 任务 | Linux 程序说... | iOS 内核懂... |
| :--- | :--- | :--- |
| 读取文件 | `sys_read(fd, buffer, count)` | `pread(fd, buffer, count, offset)` |
| 创建进程 | `sys_fork()` | `posix_spawn()` |
| 获取时间 | `sys_gettimeofday(tv, tz)` | `gettimeofday(tv, tz)` (但参数和行为有细微差别) |
| 退出程序 | `sys_exit_group(code)` | `exit(code)` |

iSH 的 `kernel/` 目录，其核心使命就是在用户态实现一个功能完备的翻译层，处理这数百种不同的“对话”。

## 3.2 拦截点：`int 0x80` 的约定

Linux 程序如何发起系统调用？在 x86 架构上，传统的方式是执行 `int 0x80` 软件中断指令。在执行这条指令前，程序会把**系统调用号**放入 `eax` 寄存器，并把参数依次放入 `ebx`, `ecx`, `edx` 等寄存器。

Asbestos JIT 引擎对 `int 0x80` 指令做了特殊处理。当它在编译时遇到这条指令，并不会生成一个真正的中断，而是会生成一个特殊的 Gadget，这个 Gadget 的作用是：
1.  **暂停 JIT 执行**：保存当前的 CPU 状态（寄存器、程序计数器等）。
2.  **调用 C 函数**：调用 iSH 的 C 语言函数 `handle_syscall()`。
3.  **恢复执行**：在 `handle_syscall()` 返回后，从 C 函数的返回值中更新 `eax` 寄存器，并继续 JIT 执行。

这个过程优雅地将控制权从高速的 JIT 执行流，转移到了灵活的 C 语言处理逻辑中。

## 3.3 核心分派：`calls.c` 中的大表

`kernel/calls.c` 是系统调用转换层的核心。其关键是一个巨大的静态数组 `syscall_table`。

```c
// kernel/calls.c (简化后)
const struct syscall_def syscall_table[] = {
    // ...
    [__NR_read] = {sys_read, "read"},
    [__NR_write] = {sys_write, "write"},
    [__NR_open] = {sys_open, "open"},
    [__NR_close] = {sys_close, "close"},
    [__NR_fork] = {sys_fork, "fork"},
    [__NR_execve] = {sys_execve, "execve"},
    // ... 200 多个系统调用
};

int handle_syscall(struct task *task) {
    dword_t syscall_num = task->cpu.eax;
    if (syscall_num >= sizeof(syscall_table)/sizeof(syscall_table[0]))
        return -_ENOSYS; // 无效的系统调用号

    const struct syscall_def *def = &syscall_table[syscall_num];
    if (def->func == NULL)
        return -_ENOSYS; // 不支持的系统调用

    // 调用具体的实现函数
    return def->func(task);
}
```

`handle_syscall` 的逻辑非常清晰：它以 `eax` 寄存器中的系统调用号为索引，在表中查找对应的处理函数，然后调用它。

## 3.4 实现剖析：从简单映射到复杂模拟

系统调用的实现并非都像查表那么简单。我们可以将其分为几类：

### A. 简单的一对一映射

许多文件和时间相关的系统调用，在 iOS (POSIX) 中有非常相似的对应。例如 `sys_read`：

```c
// kernel/fs.c (简化后)
static int sys_read(struct task *task) {
    // 1. 从模拟的 CPU 寄存器中获取参数
    int fd_num = task->cpu.ebx;
    addr_t user_buf_addr = task->cpu.ecx;
    size_t count = task->cpu.edx;

    // 2. 将模拟的文件描述符(fd_num)转换为 iSH 内部的 fd 结构体
    struct fd *fd = fd_get(task, fd_num);
    if (fd == NULL) return -_EBADF;

    // 3. 在 iSH 堆上分配一个临时缓冲区
    char *kernel_buf = malloc(count);

    // 4. 调用 VFS 层的 read 操作，这最终会调用 iOS 的 pread 或其他实现
    ssize_t bytes_read = fd->ops->read(fd, kernel_buf, count);

    // 5. 如果读取成功，将数据从 iSH 的缓冲区复制到模拟程序的内存空间
    if (bytes_read > 0) {
        if (user_write(task, user_buf_addr, kernel_buf, bytes_read) < 0) {
            bytes_read = -_EFAULT;
        }
    }
    
    free(kernel_buf);
    return bytes_read;
}
```
这个过程揭示了一个关键点：**iSH 内核与模拟的 Linux 程序之间存在严格的内存隔离**。数据交换必须通过 `user_read()` 和 `user_write()` 这类函数进行，它们会处理地址翻译和权限检查。

### B. 状态模拟：`fork` 的幻术

`fork()` 是 UNIX 的基石，但 iOS 禁止应用创建子进程。iSH 如何凭空造出一个 `fork`？答案是：**用线程模拟进程**。

当 `sys_fork`被调用时，iSH 会：
1.  **创建一个新的 `task` 结构体**：这个结构体代表了一个“进程”，包含了 CPU 状态、内存映射、文件描述符表等所有信息。
2.  **复制父进程的内存**：调用 `pt_copy_on_write()`，以写时复制（COW）的方式，高效地复制父进程的整个地址空间。这意味着在写入发生前，子进程与父进程共享物理内存页，开销极小。
3.  **复制文件描述符**：复制父进程的 `fdtable`，并增加每个文件描述符的引用计数。
4.  **启动一个新的 `pthread` 线程**：这个线程将成为“子进程”的执行体。它的入口函数会设置好 `current` 任务指针，然后跳转到 JIT 引擎开始执行。
5.  **巧妙的返回值**：
    *   在“父进程”的 `task` 中，`sys_fork` 返回新创建的子进程的 PID。
    *   在“子进程”的 `task` 中，`eax` 寄存器被设置为 0。

通过这一系列操作，iSH 在一个单进程的应用内，完美地模拟了多进程的行为。

### C. 抽象与虚拟化：文件系统的例子

Linux 程序期望看到一个熟悉的根目录 `/`，以及 `/proc`, `/dev` 等。但 iOS 应用被囚禁在一个沙盒目录中。iSH 的虚拟文件系统（VFS）层 (`fs/`) 解决了这个问题。

VFS 定义了一套标准的文件操作接口（`struct fs_ops`），例如 `open`, `read`, `stat`。然后为不同类型的文件系统提供具体的实现：
*   **`realfs` (`fs/real.c`)**：将操作映射到 iOS 沙盒内的真实文件。
*   **`procfs` (`fs/proc/`)**：当程序访问 `/proc/cpuinfo` 时，它并不会读取一个真实文件，而是会调用一个函数，该函数动态生成模拟的 CPU 信息。
*   **`devfs` (`fs/dev.c`)**：模拟 `/dev/null`, `/dev/zero`, `/dev/random` 等设备文件。

## 3.5 总结：一个精心构建的“谎言”

iSH 的系统调用转换层是一个宏大而精密的“谎言”。它通过拦截、翻译、模拟和虚拟化，让 Linux 程序相信自己正运行在一个标准的环境中。这个“谎言”的质量，直接决定了 iSH 的兼容性。

*   **兼容性**：每多实现一个系统调用，iSH 的兼容性就更进一步。
*   **性能**：每个系统调用实现的效率，尤其是文件 I/O 和进程管理，对整体性能至关重要。
*   **安全性**：这一层也是一个安全边界，确保模拟的程序无法逃逸出 iSH 的控制，访问到不该访问的系统资源。

现在，我们已经探索了 iSH 的两大支柱。在后续的章节中，我们将深入内存管理、文件系统等更多细节，进一步完善我们对这个精巧系统的认知。