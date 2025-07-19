# 第二章：深入 Asbestos JIT 引擎

在第一章中，我们将 Asbestos 描述为 iSH 的“心脏”和“大脑”。现在，是时候打开引擎盖，仔细探究其内部的精密构造了。Asbestos 的设计充满了巧妙的工程决策，它直接决定了 iSH 的性能上限。

## 2.1 核心思想：超越解释器，走向“线程化代码”

一个简单的指令模拟器通常采用“解释执行”的模式。它的主循环看起来像这样：

```c
// 伪代码：一个简单的解释器循环
while (running) {
    opcode = fetch_instruction(cpu->eip);
    switch (opcode) {
        case 0x01: // ADD
            execute_add(cpu);
            break;
        case 0x89: // MOV
            execute_mov(cpu);
            break;
        // ... 处理 200 多个指令 ...
    }
    cpu->eip += instruction_length;
}
```

这种方法的缺点显而易见：
1.  **巨大的分派开销**：每一次循环，CPU 都要进行一次 `switch` 跳转，这对于现代处理器的分支预测器来说是场噩梦。
2.  **重复劳动**：如果一个循环体内的代码被执行 1000 次，那么这 1000 次的分派、解码开销都将被重复计算。

Asbestos 采用了更先进的策略，其灵感来源于 Forth 语言中的“线程化代码”（Threaded Code）。其核心思想是：**将指令翻译成一系列可直接调用的函数地址，并让每个函数的末尾直接跳转到下一个函数，从而消除主循环的分派开销。**

在 Asbestos 中，这些小函数被称为 **Gadgets**。一个 Gadget 通常对应一条 x86 指令的实现，它被编写成高度优化的汇编代码。

## 2.2 编译流程：从 x86 字节码到可执行的 Fiber

Asbestos 的编译单位不是单条指令，而是一个“基本块”（Basic Block）——一段没有分支进入或跳出的连续指令序列。这个编译产物，在 iSH 中被称为 **Fiber** 或 **Fiber Block**。

让我们跟随 `asbestos/asbestos.c` 中的 `fiber_block_compile` 函数，看看一个 Fiber 是如何诞生的。

```c
// asbestos/asbestos.c
static struct fiber_block *fiber_block_compile(addr_t ip, struct tlb *tlb) {
    struct gen_state state;
    // 1. 初始化生成器状态
    gen_start(ip, &state);

    while (true) {
        // 2. 步进式生成，一次一条指令
        if (!gen_step(&state, tlb))
            break; // 遇到分支、中断或无法解码的指令，结束编译
        
        // 3. 检查块大小，防止跨页过多
        if (state.ip - ip >= PAGE_SIZE - 15) {
            gen_exit(&state); // 生成块的退出代码
            break;
        }
    }
    // 4. 结束编译，生成最终的 fiber_block
    gen_end(&state);
    return state.block;
}
```

这个过程的关键在于 `gen_step` (`asbestos/gen.c`)。它负责解码单条 x86 指令，并为其选择合适的 Gadget。

```c
// asbestos/gen.c
bool gen_step(struct gen_state *state, struct tlb *tlb) {
    // ... 省略前缀和操作数解码 ...

    // 核心：根据操作码查找对应的 Gadget 生成函数
    const struct gen_operation *op = &operations[opcode];
    if (op->func == NULL)
        return false; // 无法处理的指令

    // 调用 Gadget 生成函数，它会将 Gadget 的地址写入 state.block 的代码区
    return op->func(state, op);
}
```

一个编译好的 `fiber_block` 结构体大致包含：
*   `addr_t addr`: 这段代码在模拟的 x86 地址空间中的起始地址。
*   `void *code`: 指向一段在宿主（ARM）架构上可执行的本地代码。这段代码就是一系列 Gadget 的集合。
*   `addr_t end_addr`: 模拟地址空间中的结束地址。
*   `void **jump_ip[2]`: 指向本地代码中跳转指令的指针。这是实现动态优化的关键，我们稍后会讲到。

## 2.3 执行与优化：Fiber 的链接与缓存

当一个 Fiber 被编译完成后，它如何被执行？答案在 `fiber_enter` 函数中。这是一个用汇编编写的函数，它负责：
1.  保存当前的 CPU 状态。
2.  跳转到 `fiber_block->code` 的起始地址。
3.  执行完毕后，恢复 CPU 状态，并返回。

**真正的魔法发生在 Fiber 之间的链接上。**

假设我们有一个从地址 `A` 跳转到地址 `B` 的指令。
*   第一次执行时，`A` 所在的 Fiber 执行完毕后，会返回到 Asbestos 的主控逻辑。主控逻辑查找或编译 `B` 地址对应的 Fiber，然后 `fiber_enter` 进入执行。
*   这时，Asbestos 会进行一次**动态链接**。它会找到 `A` 的 Fiber 中那条跳转指令对应的 `jump_ip` 指针，然后**直接修改本地代码**，将其跳转目标从“返回主控逻辑”改为 `B` 的 Fiber 的 `code` 地址。

```mermaid
graph TD
    subgraph 第一次执行
        Fiber_A[Fiber A (code)] -- 1. 执行完毕 --> Return_to_Dispatcher[返回分派器]
        Return_to_Dispatcher -- 2. 查找/编译 Fiber B --> Fiber_B[Fiber B (code)]
        Return_to_Dispatcher -- 3. 执行 Fiber B --> Fiber_B
    end

    subgraph 动态链接后
        Fiber_A_Patched[Fiber A (code)] -- "直接跳转 (JMP)" --> Fiber_B_Patched[Fiber B (code)]
    end

    linkStyle 0 stroke-width:2px,stroke:blue;
    linkStyle 1 stroke-width:2px,stroke:blue;
    linkStyle 2 stroke-width:2px,stroke:blue;
    linkStyle 3 stroke-width:2px,stroke:green,stroke-dasharray: 5 5;
```

通过这种方式，频繁执行的代码路径（如循环）会被链接成一个高效的、无分派开销的本地代码链。

### 2.4 缓存与失效：应对动态代码

程序不仅仅是静态的代码，它还可能在运行时修改自身。iSH 必须能够处理这种情况。

*   **缓存**：所有编译好的 Fiber 都存储在一个哈希表中，以其 x86 起始地址为键。`fiber_lookup` 函数负责快速查找。
*   **失效**：当 iSH 的内存管理单元检测到某段内存页被写入时，它会调用 `asbestos_invalidate_page`。这个函数会：
    1.  找到所有起始或结束于该页的 Fiber。
    2.  将它们从主哈希表中移除。
    3.  断开所有指向它们的动态链接。
    4.  将这些失效的 Fiber 放入一个“垃圾回收”列表（`jetsam` 列表）。
    5.  在下一个安全的时间点（当没有线程在执行 JIT 代码时），统一释放这些 Fiber 的内存。

这个机制确保了 JIT 缓存与模拟的内存状态始终保持一致。

## 2.5 总结：一个精巧的动态二进制翻译系统

Asbestos 不仅仅是一个模拟器，它是一个完整的动态二进制翻译（DBT）系统。它通过以下技术实现了卓越的性能：
*   **JIT 编译**：避免了解释执行的开销。
*   **线程化代码**：消除了分派循环。
*   **基本块链接**：优化了控制流转移。
*   **缓存与失效机制**：优雅地处理了自修改代码。

理解了 Asbestos，就理解了 iSH 性能的根基。在下一章，我们将探讨 iSH 的另一大支柱：系统调用转换层，看看它是如何欺骗 Linux 程序，让它们以为自己运行在真正的内核之上。