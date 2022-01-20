// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM selftest powerpc library code
 *
 * Copyright (C) 2021, IBM Corp.
 */

#define _GNU_SOURCE
//#define DEBUG

#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "processor.h"

/*
 * 2^(12+PRTS) = Process table size
 *
 * But the hardware doesn't seem to care, so 0 for now.
 */
#define PRTS 0
#define RTS ((0x5UL << 5) | (0x2UL << 61)) /* 2^(RTS+31) = 2^52 */
#define RPDS 0xd
#define RPDB_MASK 0x0fffffffffffff00UL
#define RPN_MASK  0x01fffffffffff000UL

#define MIN_FRAME_SZ 32

static const int radix_64k_index_sizes[4] = { 5, 9, 9, 13 };

static inline uint64_t mk_pte(uint64_t pte_val)
{
	return cpu_to_be64(PTE_VALID | pte_val);
}

static inline uint64_t get_pte(uint64_t pte)
{
	return be64_to_cpu(pte);
}

static inline uint64_t pte_rpn(uint64_t entry)
{
	return get_pte(entry) & RPN_MASK;
}

static inline uint64_t next_pde(uint64_t entry)
{
	return get_pte(entry) & RPDB_MASK;
}

static inline uint64_t ptrs_per_pgd(int level)
{
	return 1UL << radix_64k_index_sizes[level];
}

static inline uint64_t level_size(int level)
{
	return sizeof(vm_paddr_t) << (radix_64k_index_sizes[level] + 3);
}

static vm_paddr_t alloc_pgd(struct kvm_vm *vm, int level)
{
	static vm_paddr_t base;
	vm_paddr_t addr;
	uint64_t size = level_size(level);

	if (!base || (base + size) >> vm->page_shift != base >> vm->page_shift)
		addr = vm_alloc_page_table(vm);
	else
		addr = base;
	base = addr + size;

	return addr;
}

static vm_paddr_t pgtable_walk(struct kvm_vm *vm, vm_vaddr_t gva, uint64_t gpa,
			       bool alloc)
{
	uint64_t index_bits, shift, base, index;
	uint64_t *ptep, ptep_gpa;
	int level;

	if (!vm->pgd_created)
		goto unmapped_gva;

	pr_debug("%s %#lx ", (alloc ? "mapping" : "lookup "), gva);

	base = vm->pgd;
	shift = vm->va_bits;

	for (level = 3; level >= 0; --level) {

		index_bits = radix_64k_index_sizes[level];
		shift -= index_bits;

		index = (gva >> shift) & ((1UL << index_bits) - 1);
		ptep_gpa = base + index * sizeof(*ptep);
		ptep = addr_gpa2hva(vm, ptep_gpa);

		if (!*ptep) {
			if (!alloc)
				goto unmapped_gva;
			if (level)
				*ptep = mk_pte(alloc_pgd(vm, level - 1) |
					       radix_64k_index_sizes[level - 1]);
		}

		if (get_pte(*ptep) & PTE_LEAF)
			break;

		base = next_pde(*ptep);
	}

	if (alloc)
		*ptep = mk_pte(PTE_LEAF | gpa | PTE_RC | PTE_RWX);
	else
		gpa = pte_rpn(*ptep);

	pr_debug("-> %#lx pte: %#lx (@%#lx)\n", gpa, get_pte(*ptep), ptep_gpa);

	return gpa | (gva & (vm->page_size - 1));

unmapped_gva:
	TEST_FAIL("No mapping for vm virtual address, gva: %#lx", gva);
	exit(1);
}

void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr)
{
	TEST_ASSERT((vaddr % vm->page_size) == 0,
		    "Virtual address not on page boundary,\n"
		    "  vaddr: 0x%lx vm->page_size: 0x%x", vaddr, vm->page_size);

	TEST_ASSERT(sparsebit_is_set(vm->vpages_valid,
				     (vaddr >> vm->page_shift)),
		    "Invalid virtual address, vaddr: 0x%lx", vaddr);

	TEST_ASSERT((paddr % vm->page_size) == 0,
		    "Physical address not on page boundary,\n"
		    "  paddr: 0x%lx vm->page_size: 0x%x", paddr, vm->page_size);

	TEST_ASSERT((paddr >> vm->page_shift) <= vm->max_gfn,
		    "Physical address beyond maximum supported,\n"
		    "  paddr: 0x%lx vm->max_gfn: 0x%lx vm->page_size: 0x%x",
		    paddr, vm->max_gfn, vm->page_size);

	TEST_ASSERT(vm->pgd_created, "Page table not created\n");

	pgtable_walk(vm, vaddr, paddr, true);
}

vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	return pgtable_walk(vm, gva, 0, false);
}

void virt_pgd_alloc(struct kvm_vm *vm)
{
	struct kvm_ppc_mmuv3_cfg cfg = { 0 };
	vm_paddr_t proc_tb;
	uint64_t *proc_tb_hva;

	if (!kvm_check_cap(KVM_CAP_PPC_MMU_RADIX)) {
		print_skip("Tests only support Radix MMU");
		exit(KSFT_SKIP);
	}

	if (!kvm_check_cap(KVM_CAP_PPC_PAPR)) {
		print_skip("Tests only support Book3s");
		exit(KSFT_SKIP);
	}

	if (vm->pgd_created)
		return;

	/*
	 * Allocate the process table in guest memory and set the
	 * first doubleword of the pid 0 entry.
	 */
	proc_tb = vm_alloc_page_table(vm);
	vm->pgd = vm_alloc_page_table(vm);

	proc_tb_hva = addr_gpa2hva(vm, proc_tb);
	*proc_tb_hva = cpu_to_be64(RTS | vm->pgd | RPDS);

	pr_debug("process table gpa: %#lx\n", proc_tb);
	pr_debug("process table hva: %p\n", proc_tb_hva);
	pr_debug("process table entry 0 dw0: %#lx\n", *proc_tb_hva);

	/* Register the process table with the HV */
	cfg.process_table = PATB_GR | proc_tb | PRTS;
	cfg.flags = KVM_PPC_MMUV3_RADIX | KVM_PPC_MMUV3_GTSE;

	pr_debug("MMU config proc table: %#llx\n", cfg.process_table);

	vm_ioctl(vm, KVM_PPC_CONFIGURE_V3_MMU, &cfg);
	vm->pgd_created = true;
}

void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code)
{
	struct kvm_enable_cap cap = { 0 };
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	vm_vaddr_t stack_vaddr;
	size_t stack_sz;

	vm_vcpu_add(vm, vcpuid);

	cap.cap = KVM_CAP_PPC_PAPR;
	vcpu_enable_cap(vm, vcpuid, &cap);

	stack_sz = DEFAULT_STACK_PGS * vm->page_size;
	stack_vaddr = vm_vaddr_alloc(vm, stack_sz,
				     DEFAULT_GUEST_STACK_VADDR_MIN);

	regs.msr = MSR_SF | MSR_LE;
	regs.msr |= MSR_IR | MSR_DR;
	regs.pc = (unsigned long) guest_code;
	regs.pid = 0;
	regs.gpr[1] = stack_vaddr + stack_sz - MIN_FRAME_SZ;

	pr_debug("stack - low: %#lx high: %#lx size: %#lx SP: %#llx\n",
		 stack_vaddr, stack_vaddr + stack_sz, stack_sz, regs.gpr[1]);

	vcpu_regs_set(vm, vcpuid, &regs);

	sregs.pvr = mfpvr();
	vcpu_sregs_set(vm, vcpuid, &sregs);

	if (kvm_check_cap(KVM_CAP_ONE_REG)) {
		uint64_t lpcr = LPCR_UPRT | LPCR_HR | LPCR_GTSE;
		struct kvm_one_reg reg = {
			.id = KVM_REG_PPC_LPCR_64,
			.addr = (uintptr_t) &lpcr,
		};

		vcpu_ioctl(vm, vcpuid, KVM_SET_ONE_REG, &reg);
	}
}

void vcpu_args_set(struct kvm_vm *vm, uint32_t vcpuid, unsigned int num, ...)
{
	va_list ap;
	struct kvm_regs regs;
	int i;

	TEST_ASSERT(num >= 1 && num <= 8, "Unsupported number of args,\n"
		    "  num: %u\n", num);

	va_start(ap, num);
	vcpu_regs_get(vm, vcpuid, &regs);

	for (i = 0; i < num; i++)
		regs.gpr[i + 3] = va_arg(ap, uint64_t);

	vcpu_regs_set(vm, vcpuid, &regs);
	va_end(ap);
}

static void pte_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent,
		     uint64_t addr, int level)
{
	static const char * const type[] = { "pte", "pmd", "pud", "pgd" };
	uint64_t pde, *hva;

	if (level < 0)
		return;

	fprintf(stream, "%*s (%#lx):\n", indent, type[level], addr);

	for (pde = addr; pde < addr + (ptrs_per_pgd(level) * sizeof(vm_paddr_t));
	     pde += sizeof(vm_paddr_t)) {

		hva = addr_gpa2hva(vm, pde);
		if (!*hva)
			continue;
		fprintf(stream, "%*s %#lx: %#lx\n", indent + 1, "", pde,
			get_pte(*hva));
		pte_dump(stream, vm, indent + 2, next_pde(*hva), level - 1);
	}
}

void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	if (!vm->pgd_created)
		return;

	pte_dump(stream, vm, indent, vm->pgd, 3);
}

void vcpu_dump(FILE *stream, struct kvm_vm *vm, uint32_t vcpuid, uint8_t indent)
{
	struct kvm_regs regs;

	fprintf(stream, "%*scpuid: %u\n", indent, "", vcpuid);

	vcpu_regs_get(vm, vcpuid, &regs);
	fprintf(stream, "%*sregs:\n", indent + 2, "");

	fprintf(stream, "%*spc: %#llx\n", indent + 4, "", regs.pc);
	fprintf(stream, "%*smsr: %#llx\n", indent + 4, "", regs.msr);
	fprintf(stream, "%*ssrr0: %#llx\n", indent + 4, "", regs.srr0);
	fprintf(stream, "%*ssrr1: %#llx\n", indent + 4, "", regs.srr1);

	fprintf(stream, "\n%*sr1: %#llx\n", indent + 4, "", regs.gpr[1]);
	fprintf(stream, "%*sr2: %#llx\n", indent + 4, "", regs.gpr[2]);
	fprintf(stream, "%*sr3: %#llx\n", indent + 4, "", regs.gpr[3]);
	fprintf(stream, "%*sr4: %#llx\n", indent + 4, "", regs.gpr[4]);

	if (kvm_check_cap(KVM_CAP_ONE_REG)) {
		uint64_t lpcr;
		struct kvm_one_reg reg = {
			.id = KVM_REG_PPC_LPCR_64,
			.addr = (uintptr_t) &lpcr,
		};

		vcpu_ioctl(vm, vcpuid, KVM_GET_ONE_REG, &reg);
		fprintf(stream, "%*slpcr: %#lx\n", indent + 4, "", lpcr);
	}
	fprintf(stream, "%*slr: %#llx\n", indent + 4, "", regs.lr);
}

void assert_on_unhandled_exception(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct kvm_run *run;

	run = vcpu_state(vm, vcpuid);
	if (run) {
		switch (run->exit_reason) {
		case KVM_EXIT_PAPR_HCALL:
		case KVM_EXIT_MMIO:
			return;
		default:
			printf("reason: %s\n",
			       exit_reason_str(run->exit_reason));
			break;
		}
	}
#ifdef DEBUG
	vm_dump(stderr, vm, 2);
#endif
	TEST_ASSERT(false, "Unhandled exception");
}
