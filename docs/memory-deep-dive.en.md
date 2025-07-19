# Chapter 4: The Illusion of Memory—Building a Virtual Address Space

Every Linux program running in iSH lives in a beautiful illusion: it exclusively owns a complete, private 4GB memory space, from `0x00000000` to `0xFFFFFFFF`. The reality, however, is harsh. It's just a small piece of memory within a regular app on the iOS system. This chapter will reveal how iSH's Memory Management Unit (MMU) builds and maintains this elaborate illusion.

## 4.1 The Core Challenge: Mapping from "Guest" to "Host"

The core task of memory management is **address translation**: converting the **Guest Virtual Address** (GVA) used by the program into a **Host Virtual Address** (HVA) that iSH can access.

```
  GVA (e.g., 0x08048000)  ----[iSH MMU]---->  HVA (e.g., 0x10fe3c000)
(Address seen by Linux program)             (Real memory address in the iOS process)
```

This translation process must be extremely efficient, as it occurs on every memory access. A slow address translation could easily negate the performance gains from the JIT engine.

The iSH MMU design (`kernel/memory.c`) revolves around two core data structures:
1.  **Page Table**: The "master ledger" that stores the mapping from GVA to HVA.
2.  **Translation Lookaside Buffer (TLB)**: A high-speed cache that stores recently used address translation results to avoid repetitive page table lookups.

## 4.2 Page Tables: The Art of a Two-Level Structure

iSH uses a classic two-level page table structure to manage the 4GB virtual address space. A 32-bit virtual address is split into three parts:

```
| 10 bits (PGDIR Index) | 10 bits (PT Index) | 12 bits (Offset) |
|-----------------------|--------------------|------------------|
|      (1024 entries)   |   (1024 entries)   |   (4096 bytes)   |
```

*   **Page Directory (`pgdir`)**: An array of 1024 pointers, each pointing to a page table.
*   **Page Table (`pt`)**: An array of 1024 Page Table Entries (PTEs).
*   **Page Table Entry (PTE, `struct pt_entry`)**: The core of the mapping. It contains a pointer to the real memory and access permissions.

```c
// kernel/memory.h (simplified)
struct pt_entry {
    struct data *data; // Points to the real data block
    size_t offset;     // Offset within the data block
    unsigned flags;    // Permission flags (P_READ, P_WRITE, P_COW, ...)
};

struct data {
    void *data;        // Pointer to host memory (HVA) allocated by mmap()
    size_t size;
    int refcount;      // Reference count, used for COW
};
```

When an address needs to be translated, the `mem_pt()` function performs the following steps:
1.  Uses the first 10 bits of the address as an index to find the corresponding page table pointer in `pgdir`.
2.  Uses the middle 10 bits as an index to find the corresponding PTE in the page table.
3.  If the `pgdir` or `pt` does not exist, or the PTE is empty, it means the address is not mapped, triggering a **Page Fault**.

This two-level structure is very space-efficient. For unused address regions, iSH doesn't need to allocate page tables; the corresponding pointer in `pgdir` will simply be `NULL`.

## 4.3 The TLB: A High-Speed Cache Built for Speed

Looking up the two-level page table for every memory access is still too expensive. The TLB (`emu/tlb.h`, `emu/tlb.c`) exists to solve this problem. It's a simple array that acts as a cache for address translations.

```c
// emu/tlb.h
struct tlb_entry {
    page_t page;             // Cached guest page number (GVA >> 12)
    page_t page_if_writable; // Matches only if the page is writable
    uintptr_t data_minus_addr; // A clever pre-calculated value
};

struct tlb {
    struct tlb_entry entries[TLB_SIZE]; // Typically TLB_SIZE = 1024
};
```

When code compiled by the JIT needs to access memory, it calls `__tlb_read_ptr` or `__tlb_write_ptr`. Let's look at the implementation of `__tlb_read_ptr`:

```c
// emu/tlb.h
forceinline void *__tlb_read_ptr(struct tlb *tlb, addr_t addr) {
    // 1. Calculate the index for the address in the TLB
    struct tlb_entry entry = tlb->entries[TLB_INDEX(addr)];

    // 2. Check for a TLB hit
    if (entry.page == TLB_PAGE(addr)) {
        // Hit! Directly calculate the HVA and return
        // data_minus_addr = HVA_base - GVA_base
        // HVA = HVA_base - GVA_base + GVA = data_minus_addr + addr
        return (void *) (entry.data_minus_addr + addr);
    }

    // 3. TLB miss, enter the slow path
    return tlb_handle_miss(tlb, addr, MEM_READ);
}
```

*   **TLB Hit**: This is the expected, common path. Address translation is completed with a single array access and a comparison. The pre-calculation of `data_minus_addr` avoids multiple pointer operations on the fast path.
*   **TLB Miss**: `tlb_handle_miss` will call `mem_pt()` to query the page table. If successful, it fills the TLB entry with the new translation and returns the HVA. If `mem_pt()` fails (page fault), it returns `NULL`, eventually causing the emulated program to receive a Segmentation Fault signal.

According to the project's documentation, iSH achieves a TLB hit rate of 98.7%, meaning the vast majority of memory accesses are completed via the fast path.

## 4.4 Copy-on-Write (COW): The Performance Cornerstone of `fork()`

As mentioned in the previous chapter, `fork()` uses COW to efficiently copy memory. The `pt_copy_on_write` function in `kernel/memory.c` reveals its mechanism. When `fork` occurs:
1.  iSH iterates through the parent process's page table.
2.  For each PTE, it does **not** allocate new physical memory for the child.
3.  Instead, it just copies the parent's PTE to the child and **increments** the reference count `refcount` of the underlying `struct data`.
4.  At the same time, it marks the PTEs of both the parent and child as `P_COW` and **read-only**.

Now, the parent and child share the same physical memory pages. When either process attempts to **write** to a shared page:
1.  The JIT code will find the page is read-only, triggering a protection fault.
2.  The `mem_ptr()` function catches this fault and checks for the `P_COW` flag.
3.  It allocates a new physical memory page and copies the content of the old page to it.
4.  Then, it modifies the current process's PTE to point to the new memory page, removes the `P_COW` flag, and marks the page as writable.
5.  Finally, it decrements the reference count of the old `struct data`.

This process is completely transparent to the program, but it dramatically reduces the overhead of process creation, making `fork` a lightweight operation in iSH.

## 4.5 Conclusion: Dancing Within Constraints

The iSH memory management system is a brilliant implementation of classic operating system theory under the strict constraints of iOS.
*   **Virtualization**: Through a two-level page table, it successfully builds an independent, private virtual address space for the emulated program within the host process's address space.
*   **Performance**: Through the TLB cache, it minimizes the overhead of address translation, ensuring execution efficiency.
*   **Efficiency**: Through Copy-on-Write, it achieves lightweight process creation, allowing complex shell scripts and multi-process applications to run smoothly.

Understanding memory management is key to truly appreciating how iSH supports a fully functional Linux environment with limited resources.