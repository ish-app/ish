#include <stdbool.h>
#include <stdlib.h>

#include <linux/threads.h>
#include <asm/ptrace.h>
#include <emu/exec.h>
#include <emu/kernel.h>
#include "../kernel/irq_user.h"

#include "emu/cpu.h"
#include "emu/tlb.h"
#include "emu/interrupt.h"
#define ENGINE_ASBESTOS 1
#include "asbestos/asbestos.h"

extern int current_pid(void);

struct emu_mm_ctx {
	struct mmu mmu;
	struct emu_mm *emu_mm;
};

static __thread struct tlb the_tlb;

static void *ishemu_translate(struct mmu *mem, addr_t addr, int type)
{
	struct emu_mm *emu_mm = container_of(mem, struct emu_mm_ctx, mmu)->emu_mm;
	bool writable;
	void *ptr = user_to_kernel_emu(emu_mm, addr, &writable);
	if (ptr && type == MEM_WRITE && !writable) {
		ptr = NULL;
	}
	return ptr;
}

static struct mmu_ops ishemu_ops = {
	.translate = ishemu_translate,
};

static bool poke[NR_CPUS];

static void emu_run_to_interrupt(struct emu *emu, struct cpu_state *cpu)
{
	struct pt_regs *regs = emu_pt_regs(emu);
	struct emu_mm_ctx *mm_ctx = emu->mm->ctx;

	cpu->mmu = &mm_ctx->mmu;
	cpu->eax = regs->ax;
	cpu->ebx = regs->bx;
	cpu->ecx = regs->cx;
	cpu->edx = regs->dx;
	cpu->esi = regs->si;
	cpu->edi = regs->di;
	cpu->ebp = regs->bp;
	cpu->esp = regs->sp;
	cpu->eip = regs->ip;
	cpu->eflags = regs->flags;
	cpu->tls_ptr = regs->tls;
	expand_flags(cpu);
	cpu->poked_ptr = &poke[get_smp_processor_id()];

	int interrupt = cpu_run_to_interrupt(cpu, &the_tlb);

	collapse_flags(cpu);
	regs->ax = cpu->eax;
	regs->bx = cpu->ebx;
	regs->cx = cpu->ecx;
	regs->dx = cpu->edx;
	regs->si = cpu->esi;
	regs->di = cpu->edi;
	regs->bp = cpu->ebp;
	regs->sp = cpu->esp;
	regs->ip = cpu->eip;
	regs->flags = cpu->eflags;
	regs->tls = cpu->tls_ptr;

	if (interrupt == INT_GPF) {
		regs->cr2 = cpu->segfault_addr;
		regs->error_code = cpu->segfault_was_write << 1;
	} else {
		regs->cr2 = regs->error_code = 0;
	}
	regs->trap_nr = interrupt;
}

void emu_run(struct emu *emu)
{
	struct cpu_state cpu = {};
	if (emu->snapshot) {
		struct cpu_state *snapshot = emu->snapshot;
		cpu = *snapshot;
		free(snapshot);
		emu->snapshot = NULL;
	}
	emu->ctx = &cpu;
	for (;;) {
		emu_run_to_interrupt(emu, &cpu);
		handle_cpu_trap(emu);
	}
}

void emu_finish_fork(struct emu *emu)
{
	struct cpu_state *cpu = emu->ctx;
	struct cpu_state *snapshot = emu->snapshot = calloc(1, sizeof(*snapshot));
	*snapshot = *cpu;
	emu->ctx = NULL;
}

void emu_destroy(struct emu *emu)
{
}

void emu_poke_cpu(int cpu)
{
	__atomic_store_n(&poke[cpu], true, __ATOMIC_SEQ_CST);
}

void emu_flush_tlb_local(struct emu_mm *mm, unsigned long start, unsigned long end)
{
	if (the_tlb.mmu == NULL)
		return;
	tlb_flush(&the_tlb);
	struct emu_mm_ctx *mm_ctx = mm->ctx;
	if (mm_ctx->mmu.asbestos != NULL)
		asbestos_invalidate_range(mm_ctx->mmu.asbestos, start / PAGE_SIZE, (end + PAGE_SIZE - 1) / PAGE_SIZE /* TODO DIV_ROUND_UP? */);
}

void emu_mmu_init(struct emu_mm *mm)
{
	struct emu_mm_ctx *mm_ctx = mm->ctx = calloc(1, sizeof(*mm_ctx));
	mm_ctx->emu_mm = mm;
	mm_ctx->mmu.asbestos = asbestos_new(&mm_ctx->mmu);
	mm_ctx->mmu.ops = &ishemu_ops;
}

void emu_mmu_destroy(struct emu_mm *mm)
{
	struct emu_mm_ctx *mm_ctx = mm->ctx;
	asbestos_free(mm_ctx->mmu.asbestos);
	mm_ctx->mmu.asbestos = NULL;
}

void emu_switch_mm(struct emu *emu, struct emu_mm *mm)
{
	struct emu_mm_ctx *mm_ctx = mm->ctx;
	tlb_refresh(&the_tlb, &mm_ctx->mmu);
}
