#include <unicorn/unicorn.h>
#include <asm/ptrace.h>
#include <asm/page.h>

#include "emu_unicorn.h"

#include <emu/exec.h>
#include <emu/kernel.h>

#define check(err) __check(err, __FUNCTION__, __LINE__)
static void __check(uc_err err, const char *function, int line)
{
	if (err != UC_ERR_OK) {
		panic("%s:%d: %s", function, line, uc_strerror(err));
	}
}

#define uc_reg(op, uc, reg_id, field) \
	check(uc_reg_##op##2((uc), (reg_id), (field), (size_t[]){sizeof(*(field))}))

/****** GDT ******/

struct gdt_desc {
	unsigned limit1:16;
	unsigned base1:16;
	unsigned base2:8;
	unsigned type:4;
	unsigned system:1;
	unsigned dpl:2;
	unsigned present:1;
	unsigned limit2:4;
	unsigned avl:1;
	unsigned _:1;
	unsigned db:1;
	unsigned granularity:1;
	unsigned base3:8;
} __attribute__((packed));

static const uint32_t gdt_base = 0xfffff000;
static const uint32_t gdt_size = 16 * sizeof(struct gdt_desc);

static void install_gdt_segment(uc_engine *uc, int segment, uint32_t base, int dpl)
{
	struct gdt_desc desc = {
		.limit1 = 0xffff,
		.limit2 = 0xf,
		.base1 = (base & 0x0000ffff) >> 0,
		.base2 = (base & 0x00ff0000) >> 16,
		.base3 = (base & 0xff000000) >> 24,
		.type = 3, // read & write
		.system = 1, // user
		.dpl = dpl,
		.present = 1,
		.db = 1, // 32 bit code
		.granularity = 1,
	};
	check(uc_mem_write(uc, gdt_base + (segment * sizeof(desc)), &desc, sizeof(desc)));
}

/****** Entry/exit ******/

static void load_regs(struct emu *emu)
{
	struct emu_uc *ctx = emu->ctx;
	uc_engine *uc = ctx->uc;
	struct pt_regs *regs = emu_pt_regs(emu);
	uc_reg(write, uc, UC_X86_REG_EAX, &regs->ax);
	uc_reg(write, uc, UC_X86_REG_EBX, &regs->bx);
	uc_reg(write, uc, UC_X86_REG_ECX, &regs->cx);
	uc_reg(write, uc, UC_X86_REG_EDX, &regs->dx);
	uc_reg(write, uc, UC_X86_REG_ESI, &regs->si);
	uc_reg(write, uc, UC_X86_REG_EDI, &regs->di);
	uc_reg(write, uc, UC_X86_REG_EBP, &regs->bp);
	uc_reg(write, uc, UC_X86_REG_ESP, &regs->sp);
	uc_reg(write, uc, UC_X86_REG_EIP, &regs->ip);
	uc_reg(write, uc, UC_X86_REG_FLAGS, &regs->flags);

	if (ctx->tls_ptr != regs->tls) {
		ctx->tls_ptr = regs->tls;
		install_gdt_segment(uc, 0xc, ctx->tls_ptr, 3);
	}
	unsigned long mm_flush_count = __atomic_load_n(&emu->mm->flush_count, __ATOMIC_SEQ_CST);
	if (ctx->mm_flush_count != mm_flush_count) {
		check(uc_ctl_flush_tlb(uc));
		ctx->mm_flush_count = mm_flush_count;
	}
}

static void save_regs(struct emu *emu)
{
	struct emu_uc *ctx = emu->ctx;
	uc_engine *uc = ctx->uc;
	struct pt_regs *regs = emu_pt_regs(emu);
	regs->ax = 0;
	uc_reg(read, uc, UC_X86_REG_EAX, &regs->ax);
	uc_reg(read, uc, UC_X86_REG_EBX, &regs->bx);
	uc_reg(read, uc, UC_X86_REG_ECX, &regs->cx);
	uc_reg(read, uc, UC_X86_REG_EDX, &regs->dx);
	uc_reg(read, uc, UC_X86_REG_ESI, &regs->si);
	uc_reg(read, uc, UC_X86_REG_EDI, &regs->di);
	uc_reg(read, uc, UC_X86_REG_EBP, &regs->bp);
	uc_reg(read, uc, UC_X86_REG_ESP, &regs->sp);
	uc_reg(read, uc, UC_X86_REG_EIP, &regs->ip);
	uc_reg(read, uc, UC_X86_REG_FLAGS, &regs->flags);
}

static void do_trap(struct emu *emu, int trap_nr)
{
	save_regs(emu);
	emu_pt_regs(emu)->trap_nr = trap_nr;
	handle_cpu_trap(emu);
	load_regs(emu);
}

/****** Hooks ******/

static bool mem_type_is_write(uc_mem_type type)
{
	switch (type) {
	case UC_MEM_READ:
	case UC_MEM_READ_UNMAPPED:
	case UC_MEM_READ_PROT:
	case UC_MEM_READ_AFTER:
	case UC_MEM_FETCH:
	case UC_MEM_FETCH_UNMAPPED:
	case UC_MEM_FETCH_PROT:
		return false;
	case UC_MEM_WRITE:
	case UC_MEM_WRITE_UNMAPPED:
	case UC_MEM_WRITE_PROT:
		return true;
	}
}

static bool hook_tlb_fill(uc_engine *uc, uint64_t vaddr, uc_mem_type type, uc_tlb_entry *result, void *user_data)
{
	struct emu *emu = user_data;

	if (vaddr == gdt_base) {
		result->paddr = vaddr;
		result->perms = UC_PROT_READ | UC_PROT_WRITE;
		return true;
	}

	bool is_write = mem_type_is_write(type);
	bool writable;
	void *kernel_addr = user_to_kernel_emu(emu->mm, vaddr, &writable);

	if (kernel_addr == NULL || (is_write && !writable)) {
		struct pt_regs *regs = emu_pt_regs(emu);
		regs->cr2 = vaddr;
		regs->error_code = mem_type_is_write(type) ? 2 : 0;
		do_trap(emu, 13);
		kernel_addr = user_to_kernel_emu(emu->mm, vaddr, &writable);
	}
	if (kernel_addr == NULL) {
		uc_emu_stop(uc);
		return false;
	}

	unsigned long paddr = __pa(kernel_addr);
	result->paddr = __pa(kernel_addr);
	result->perms = UC_PROT_READ | UC_PROT_EXEC | (writable ? UC_PROT_WRITE : 0);
	return true;
}

static void hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
	struct emu *emu = user_data;
	do_trap(emu, intno);
}

static bool hook_trace_code(uc_engine *uc, uint64_t address, size_t size, void *user_data)
{
	struct emu *emu = user_data;
	void *ptr = user_to_kernel_emu(emu->mm, address, NULL);
	uint32_t sp;
	uc_reg(read, uc, UC_X86_REG_ESP, &sp);
	extern int current_pid();
	printk("%d code %#llx+%ld %*ph\n", current_pid(), address, size, (int) size, ptr);
	return true;
}

/****** Main implementation ******/

static void create_unicorn(struct emu *emu)
{
	struct emu_uc *ctx = emu->ctx = calloc(1, sizeof(*ctx));

	uc_hook hh;
	check(uc_open(UC_ARCH_X86, UC_MODE_32, &ctx->uc));

	check(uc_mem_map_ptr(ctx->uc, 0x0, ish_phys_size, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC, (void *) ish_phys_base));
	check(uc_ctl_tlb_mode(ctx->uc, UC_TLB_VIRTUAL));
	check(uc_hook_add(ctx->uc, &hh, UC_HOOK_TLB_FILL, hook_tlb_fill, emu, 1, 0));

	check(uc_mem_map(ctx->uc, gdt_base, 0x1000, UC_PROT_READ | UC_PROT_WRITE));
	struct uc_x86_mmr gdtr = {.base = gdt_base, .limit = gdt_size};
	uc_reg(write, ctx->uc, UC_X86_REG_GDTR, &gdtr);
	// unicorn bug (maybe): if you load any segment register other than ss, sp suddenly becomes 16 bit. can be fixed by loading ss correctly
	install_gdt_segment(ctx->uc, 1, 0, 0);
	int seg = (1 << 3) | 0; // ring 0? why?
	uc_reg(write, ctx->uc, UC_X86_REG_SS, &seg);

	check(uc_hook_add(ctx->uc, &hh, UC_HOOK_INTR, hook_intr, emu, 1, 0));

	if (unicorn_trace) {
		check(uc_hook_add(ctx->uc, &hh, UC_HOOK_BLOCK, hook_trace_code, emu, 1, 0));
	}
}

void emu_run(struct emu *emu)
{
	if (!emu->ctx) {
		create_unicorn(emu);
	}
	struct emu_uc *ctx = emu->ctx;

	load_regs(emu);

	for (;;) {
		check(uc_emu_start(ctx->uc, emu_pt_regs(emu)->ip, 0, 0, 0));
	}
}

void emu_finish_fork(struct emu *emu)
{
	struct emu_uc *old = emu->ctx;
	create_unicorn(emu);
	struct emu_uc *new = emu->ctx;

	uc_context *ctx;
	check(uc_context_alloc(old->uc, &ctx));
	check(uc_context_save(old->uc, ctx));
	check(uc_context_restore(new->uc, ctx));
	check(uc_context_free(ctx));
}

void emu_destroy(struct emu *emu)
{
	struct emu_uc *ctx = emu->ctx;
	check(uc_close(ctx->uc));
	free(ctx);
	emu->ctx = NULL;
}

void emu_poke_cpu(int cpu) {}

void emu_mmu_init(struct emu_mm *mm)
{
}
void emu_mmu_destroy(struct emu_mm *mm)
{
}
void emu_switch_mm(struct emu *emu, struct emu_mm *mm)
{
	emu->mm = mm;
}
void emu_flush_tlb_local(struct emu_mm *mm, unsigned long start, unsigned long end)
{
	__atomic_fetch_add(&mm->flush_count, 1, __ATOMIC_SEQ_CST);
}
