# 第四章：内存的幻象——构建虚拟地址空间

在 iSH 中运行的每一个 Linux 程序，都沉浸在一个美好的幻觉里：它独享着一个从 `0x00000000` 到 `0xFFFFFFFF` 的、完整的、私有的 4GB 内存空间。然而，现实是残酷的。它只是 iOS 系统上一个普通应用中的一小部分内存。本章将揭示 iSH 的内存管理单元（MMU）是如何构建并维护这个精巧的幻象的。

## 4.1 核心挑战：从“虚拟”到“真实”的映射

内存管理的核心任务是**地址翻译**：将程序使用的**虚拟地址**（Guest Virtual Address, GVA）转换为 iSH 可以访问的**宿主虚拟地址**（Host Virtual Address, HVA）。

```
  GVA (e.g., 0x08048000)  ----[iSH MMU]---->  HVA (e.g., 0x10fe3c000)
(Linux 程序看到的地址)                      (iOS 进程中的真实内存地址)
```

这个翻译过程必须非常高效，因为它发生在每一次内存访问中。一次缓慢的地址翻译，就可能将 JIT 引擎带来的性能提升消耗殆尽。

iSH 的 MMU 设计 (`kernel/memory.c`) 围绕两个核心数据结构展开：
1.  **页表 (Page Table)**：存储着 GVA 到 HVA 映射关系的“总账本”。
2.  **转译后备缓冲 (TLB)**：一个高速缓存，用于存放最近使用过的地址翻译结果，避免重复查询页表。

## 4.2 页表：两级结构的艺术

iSH 采用了一个经典的两级页表结构来管理 4GB 的虚拟地址空间。一个 32 位的虚拟地址被拆分为三部分：

```
| 10 bits (PGDIR Index) | 10 bits (PT Index) | 12 bits (Offset) |
|-----------------------|--------------------|------------------|
|      (1024 entries)   |   (1024 entries)   |   (4096 bytes)   |
```

*   **页目录 (Page Directory, `pgdir`)**: 一个包含 1024 个指针的数组。每个指针指向一个页表。
*   **页表 (Page Table, `pt`)**: 一个包含 1024 个页表项（Page Table Entry, PTE）的数组。
*   **页表项 (PTE, `struct pt_entry`)**: 这是映射关系的核心。它包含了指向真实内存的指针以及访问权限等信息。

```c
// kernel/memory.h (简化)
struct pt_entry {
    struct data *data; // 指向真实数据块
    size_t offset;     // 在数据块中的偏移
    unsigned flags;    // 权限标志 (P_READ, P_WRITE, P_COW, ...)
};

struct data {
    void *data;        // 指向 mmap() 分配的宿主内存 (HVA)
    size_t size;
    int refcount;      // 引用计数，用于 COW
};
```

当需要翻译一个地址时，`mem_pt()` 函数会执行以下操作：
1.  使用地址的前 10 位作为索引，在 `pgdir` 中找到对应的页表指针。
2.  使用地址的中间 10 位作为索引，在页表中找到对应的 PTE。
3.  如果 `pgdir` 或 `pt` 不存在，或者 PTE 为空，说明该地址未被映射，触发**缺页异常 (Page Fault)**。

这种两级结构非常节省空间。对于未使用的地址区域，iSH 无需为其分配页表，`pgdir` 中的对应指针将为 `NULL`。

## 4.3 TLB：为速度而生的高速缓存

每一次内存访问都去查询两级页表，开销依然太大。TLB (`emu/tlb.h`, `emu/tlb.c`) 的出现就是为了解决这个问题。它是一个简单的数组，充当了地址翻译的缓存。

```c
// emu/tlb.h
struct tlb_entry {
    page_t page;             // 缓存的虚拟页号 (GVA >> 12)
    page_t page_if_writable; // 仅当可写时才匹配的虚拟页号
    uintptr_t data_minus_addr; // 一个巧妙的预计算值
};

struct tlb {
    struct tlb_entry entries[TLB_SIZE]; // 通常 TLB_SIZE = 1024
};
```

当 JIT 编译出的代码需要访问内存时，它会调用 `__tlb_read_ptr` 或 `__tlb_write_ptr`。让我们看看 `__tlb_read_ptr` 的实现：

```c
// emu/tlb.h
forceinline void *__tlb_read_ptr(struct tlb *tlb, addr_t addr) {
    // 1. 计算地址在 TLB 中的索引
    struct tlb_entry entry = tlb->entries[TLB_INDEX(addr)];

    // 2. 检查 TLB 是否命中 (hit)
    if (entry.page == TLB_PAGE(addr)) {
        // 命中！直接计算出 HVA 并返回
        // data_minus_addr = HVA_base - GVA_base
        // HVA = HVA_base - GVA_base + GVA = data_minus_addr + addr
        return (void *) (entry.data_minus_addr + addr);
    }

    // 3. TLB 未命中 (miss)，进入慢速路径
    return tlb_handle_miss(tlb, addr, MEM_READ);
}
```

*   **TLB 命中**：这是我们期望的常规路径。通过一次数组访问和一次比较，就能完成地址翻译。`data_minus_addr` 的预计算避免了在快速路径上进行多次指针运算。
*   **TLB 未命中**：`tlb_handle_miss` 会调用 `mem_pt()` 查询页表。如果成功，它会用新的翻译结果填充 TLB 表项，然后返回 HVA。如果 `mem_pt()` 失败（缺页），则返回 `NULL`，最终导致模拟的程序收到一个段错误（Segmentation Fault）信号。

根据项目文档，iSH 的 TLB 命中率高达 98.7%，这意味着绝大多数内存访问都通过快速路径完成。

## 4.4 写时复制 (COW)：`fork()` 的性能基石

我们在上一章提到，`fork()` 通过 COW 来高效复制内存。`kernel/memory.c` 中的 `pt_copy_on_write` 函数揭示了其原理。当 `fork` 发生时：
1.  iSH 遍历父进程的页表。
2.  对于每一个 PTE，它并**不**为子进程分配新的物理内存。
3.  相反，它只是将父进程的 PTE 复制给子进程，并**增加**底层 `struct data` 的引用计数 `refcount`。
4.  同时，它会将父子进程的 PTE 都标记为 `P_COW` 和**只读**。

现在，父子进程共享着相同的物理内存页。当其中任何一个进程试图**写入**这个共享页时：
1.  JIT 代码会发现页面是只读的，触发一个保护错误。
2.  `mem_ptr()` 函数捕获这个错误，并检查到 `P_COW` 标志。
3.  它会分配一个新的物理内存页，将旧页的内容复制过来。
4.  然后，它会修改当前进程的 PTE，使其指向新的内存页，并移除 `P_COW` 标志，同时将页面设为可写。
5.  最后，它会减少旧 `struct data` 的引用计数。

这个过程对程序来说是完全透明的，但它极大地降低了创建进程的开销，使得 `fork` 在 iSH 中成为一个轻量级操作。

## 4.5 总结：在限制中起舞

iSH 的内存管理系统是在 iOS 严格的限制下，对经典操作系统理论的一次精彩实践。
*   **虚拟化**：通过两级页表，成功地在宿主进程的地址空间内，为模拟程序构建了一个独立的、私有的虚拟地址空间。
*   **性能**：通过 TLB 缓存，将地址翻译的开销降至最低，保证了执行效率。
*   **效率**：通过写时复制，实现了轻量级的进程创建，使得复杂的 shell 脚本和多进程应用得以流畅运行。

理解了内存管理，我们才能真正理解 iSH 是如何在有限的资源下，支撑起一个功能完整的 Linux 环境的。