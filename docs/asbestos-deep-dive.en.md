# Chapter 2: A Deep Dive into the Asbestos JIT Engine

In Chapter 1, we described Asbestos as the "heart" and "brain" of iSH. Now, it's time to open the hood and examine its intricate internal machinery. The design of Asbestos is filled with clever engineering decisions that directly determine the performance ceiling of iSH.

## 2.1 The Core Idea: Beyond an Interpreter to "Threaded Code"

A simple instruction simulator typically uses an "interpretive execution" model. Its main loop looks something like this:

```c
// Pseudocode: A simple interpreter loop
while (running) {
    opcode = fetch_instruction(cpu->eip);
    switch (opcode) {
        case 0x01: // ADD
            execute_add(cpu);
            break;
        case 0x89: // MOV
            execute_mov(cpu);
            break;
        // ... handle 200+ instructions ...
    }
    cpu->eip += instruction_length;
}
```

The disadvantages of this approach are obvious:
1.  **Huge Dispatch Overhead**: In every loop, the CPU performs a `switch` jump, which is a nightmare for modern processor branch predictors.
2.  **Repetitive Work**: If code inside a loop is executed 1000 times, the dispatch and decoding overhead is incurred 1000 times.

Asbestos employs a more advanced strategy, inspired by "Threaded Code" from the Forth language. The core idea is: **translate instructions into a series of directly callable function addresses, and have the end of each function jump directly to the next one, thus eliminating the main loop's dispatch overhead.**

In Asbestos, these small functions are called **Gadgets**. A Gadget usually corresponds to the implementation of a single x86 instruction, written in highly optimized assembly code.

## 2.2 The Compilation Flow: From x86 Bytecode to Executable Fibers

The compilation unit in Asbestos is not a single instruction, but a "Basic Block"—a contiguous sequence of instructions with no branches in or out. This compilation product is called a **Fiber** or **Fiber Block** in iSH.

Let's follow the `fiber_block_compile` function in `asbestos/asbestos.c` to see how a Fiber is born.

```c
// asbestos/asbestos.c
static struct fiber_block *fiber_block_compile(addr_t ip, struct tlb *tlb) {
    struct gen_state state;
    // 1. Initialize the generator state
    gen_start(ip, &state);

    while (true) {
        // 2. Step-by-step generation, one instruction at a time
        if (!gen_step(&state, tlb))
            break; // End compilation on a branch, interrupt, or undecodable instruction
        
        // 3. Check block size to prevent excessive page crossing
        if (state.ip - ip >= PAGE_SIZE - 15) {
            gen_exit(&state); // Generate the block's exit code
            break;
        }
    }
    // 4. Finalize compilation and generate the final fiber_block
    gen_end(&state);
    return state.block;
}
```

The key to this process is `gen_step` (`asbestos/gen.c`). It's responsible for decoding a single x86 instruction and selecting the appropriate Gadget for it.

```c
// asbestos/gen.c
bool gen_step(struct gen_state *state, struct tlb *tlb) {
    // ... skip prefix and operand decoding ...

    // Core: Look up the corresponding Gadget generation function by opcode
    const struct gen_operation *op = &operations[opcode];
    if (op->func == NULL)
        return false; // Unhandled instruction

    // Call the Gadget generation function, which writes the Gadget's address
    // into the code section of state.block
    return op->func(state, op);
}
```

A compiled `fiber_block` struct roughly contains:
*   `addr_t addr`: The starting address of this code in the emulated x86 address space.
*   `void *code`: A pointer to a block of native, executable code on the host (ARM) architecture. This code is the collection of Gadgets.
*   `addr_t end_addr`: The ending address in the emulated address space.
*   `void **jump_ip[2]`: Pointers to the jump instructions within the native code. This is key to dynamic optimization, which we'll discuss shortly.

## 2.3 Execution and Optimization: Linking and Caching Fibers

How is a Fiber executed after it's compiled? The answer lies in the `fiber_enter` function. This is an assembly-written function that:
1.  Saves the current CPU state.
2.  Jumps to the starting address of `fiber_block->code`.
3.  After execution, restores the CPU state and returns.

**The real magic happens in the linking between Fibers.**

Suppose we have an instruction that jumps from address `A` to address `B`.
*   On the first execution, the Fiber for `A` finishes and returns to the main control logic of Asbestos. The controller then looks up or compiles the Fiber for address `B` and enters it via `fiber_enter`.
*   At this point, Asbestos performs **dynamic linking**. It finds the `jump_ip` pointer corresponding to the jump instruction in `A`'s Fiber and **directly modifies the native code**. It changes the jump target from "return to controller" to the `code` address of `B`'s Fiber.

```mermaid
graph TD
    subgraph First Execution
        Fiber_A[Fiber A (code)] -- 1. Finishes Execution --> Return_to_Dispatcher[Return to Dispatcher]
        Return_to_Dispatcher -- 2. Find/Compile Fiber B --> Fiber_B[Fiber B (code)]
        Return_to_Dispatcher -- 3. Execute Fiber B --> Fiber_B
    end

    subgraph After Dynamic Linking
        Fiber_A_Patched[Fiber A (code)] -- "Direct Jump (JMP)" --> Fiber_B_Patched[Fiber B (code)]
    end

    linkStyle 0 stroke-width:2px,stroke:blue;
    linkStyle 1 stroke-width:2px,stroke:blue;
    linkStyle 2 stroke-width:2px,stroke:blue;
    linkStyle 3 stroke-width:2px,stroke:green,stroke-dasharray: 5 5;
```

This way, frequently executed code paths (like loops) are linked into an efficient chain of native code, free of dispatch overhead.

### 2.4 Caching and Invalidation: Handling Dynamic Code

A program is not just static code; it can modify itself at runtime. iSH must be able to handle this.

*   **Caching**: All compiled Fibers are stored in a hash table, keyed by their x86 starting address. The `fiber_lookup` function handles fast lookups.
*   **Invalidation**: When the iSH memory management unit detects a write to a memory page, it calls `asbestos_invalidate_page`. This function:
    1.  Finds all Fibers that start or end on that page.
    2.  Removes them from the main hash table.
    3.  Breaks all dynamic links pointing to them.
    4.  Places these invalidated Fibers into a "garbage collection" list (the `jetsam` list).
    5.  At the next safe point (when no threads are executing JIT code), it frees the memory of these Fibers.

This mechanism ensures that the JIT cache remains consistent with the state of the emulated memory.

## 2.5 Conclusion: An Elegant Dynamic Binary Translation System

Asbestos is not just an emulator; it's a complete Dynamic Binary Translation (DBT) system. It achieves excellent performance through the following techniques:
*   **JIT Compilation**: Avoids the overhead of interpretation.
*   **Threaded Code**: Eliminates the dispatch loop.
*   **Basic Block Chaining**: Optimizes control flow transfers.
*   **Cache Invalidation Mechanism**: Gracefully handles self-modifying code.

By understanding Asbestos, we understand the foundation of iSH's performance. In the next chapter, we will explore the other major pillar of iSH: the syscall translation layer, and see how it deceives Linux programs into thinking they are running on a real kernel.