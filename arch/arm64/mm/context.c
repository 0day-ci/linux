// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/cpufeature.h>
#include <asm/lib_asid.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

static DEFINE_PER_CPU(atomic64_t, active_asids);
static DEFINE_PER_CPU(u64, reserved_asids);

static struct asid_info asid_info;

/* Get the ASIDBits supported by the current CPU */
static u32 get_cpu_asid_bits(void)
{
	u32 asid;
	int fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64MMFR0_EL1),
						ID_AA64MMFR0_ASID_SHIFT);

	switch (fld) {
	default:
		pr_warn("CPU%d: Unknown ASID size (%d); assuming 8-bit\n",
					smp_processor_id(),  fld);
		fallthrough;
	case 0:
		asid = 8;
		break;
	case 2:
		asid = 16;
	}

	return asid;
}

/* Check if the current cpu's ASIDBits is compatible with asid_bits */
void verify_cpu_asid_bits(void)
{
	u32 asid = get_cpu_asid_bits();

	if (asid < asid_info.bits) {
		/*
		 * We cannot decrease the ASID size at runtime, so panic if we support
		 * fewer ASID bits than the boot CPU.
		 */
		pr_crit("CPU%d: smaller ASID size(%u) than boot CPU (%u)\n",
				smp_processor_id(), asid, asid_info.bits);
		cpu_panic_kernel();
	}
}

static void set_kpti_asid_bits(struct asid_info *info, unsigned long *map)
{
	unsigned int len = BITS_TO_LONGS(NUM_CTXT_ASIDS(info)) * sizeof(unsigned long);
	/*
	 * In case of KPTI kernel/user ASIDs are allocated in
	 * pairs, the bottom bit distinguishes the two: if it
	 * is set, then the ASID will map only userspace. Thus
	 * mark even as reserved for kernel.
	 */
	memset(map, 0xaa, len);
}

static void set_reserved_asid_bits(struct asid_info *info)
{
	if (info->pinned_map)
		bitmap_copy(info->map, info->pinned_map, NUM_CTXT_ASIDS(info));
	else if (arm64_kernel_unmapped_at_el0())
		set_kpti_asid_bits(info, info->map);
	else
		bitmap_clear(info->map, 0, NUM_CTXT_ASIDS(info));
}

void check_and_switch_context(struct mm_struct *mm)
{
	if (system_supports_cnp())
		cpu_set_reserved_ttbr0();

	asid_check_context(&asid_info, &mm->context.id,
			   &mm->context.pinned);

	arm64_apply_bp_hardening();

	/*
	 * Defer TTBR0_EL1 setting for user threads to uaccess_enable() when
	 * emulating PAN.
	 */
	if (!system_uses_ttbr0_pan())
		cpu_switch_mm(mm->pgd, mm);
}

unsigned long arm64_mm_context_get(struct mm_struct *mm)
{
	u64 asid;
	struct asid_info *info = &asid_info;

	asid = asid_context_pinned_get(info, &mm->context.id,
				       &mm->context.pinned);

	/* Set the equivalent of USER_ASID_BIT */
	if (asid && arm64_kernel_unmapped_at_el0())
		asid |= 1;

	return asid;
}
EXPORT_SYMBOL_GPL(arm64_mm_context_get);

void arm64_mm_context_put(struct mm_struct *mm)
{
	struct asid_info *info = &asid_info;

	asid_context_pinned_put(info, &mm->context.id, &mm->context.pinned);
}
EXPORT_SYMBOL_GPL(arm64_mm_context_put);

/* Errata workaround post TTBRx_EL1 update. */
asmlinkage void post_ttbr_update_workaround(void)
{
	if (!IS_ENABLED(CONFIG_CAVIUM_ERRATUM_27456))
		return;

	asm(ALTERNATIVE("nop; nop; nop",
			"ic iallu; dsb nsh; isb",
			ARM64_WORKAROUND_CAVIUM_27456));
}

void cpu_do_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm)
{
	unsigned long ttbr1 = read_sysreg(ttbr1_el1);
	unsigned long asid = ASID(mm);
	unsigned long ttbr0 = phys_to_ttbr(pgd_phys);

	/* Skip CNP for the reserved ASID */
	if (system_supports_cnp() && asid)
		ttbr0 |= TTBR_CNP_BIT;

	/* SW PAN needs a copy of the ASID in TTBR0 for entry */
	if (IS_ENABLED(CONFIG_ARM64_SW_TTBR0_PAN))
		ttbr0 |= FIELD_PREP(TTBR_ASID_MASK, asid);

	/* Set ASID in TTBR1 since TCR.A1 is set */
	ttbr1 &= ~TTBR_ASID_MASK;
	ttbr1 |= FIELD_PREP(TTBR_ASID_MASK, asid);

	write_sysreg(ttbr1, ttbr1_el1);
	isb();
	write_sysreg(ttbr0, ttbr0_el1);
	isb();
	post_ttbr_update_workaround();
}

static void asid_flush_cpu_ctxt(void)
{
	local_flush_tlb_all();
}

static int asids_update_limit(void)
{
	struct asid_info *info = &asid_info;
	unsigned long num_available_asids = NUM_CTXT_ASIDS(info);

	if (arm64_kernel_unmapped_at_el0()) {
		num_available_asids /= 2;
		if (info->pinned_map)
			set_kpti_asid_bits(info, info->pinned_map);
	}
	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more ASID than CPUs. ASID #0 is reserved for init_mm.
	 */
	WARN_ON(num_available_asids - 1 <= num_possible_cpus());
	pr_info("ASID allocator initialised with %lu entries\n",
		num_available_asids);

	/*
	 * There must always be an ASID available after rollover. Ensure that,
	 * even if all CPUs have a reserved ASID and the maximum number of ASIDs
	 * are pinned, there still is at least one empty slot in the ASID map.
	 */
	info->max_pinned_asids = num_available_asids - num_possible_cpus() - 2;
	return 0;
}
arch_initcall(asids_update_limit);

static int asids_init(void)
{
	struct asid_info *info = &asid_info;

	if (asid_allocator_init(info, get_cpu_asid_bits(), true))
		panic("Unable to initialize ASID allocator for %lu ASIDs\n",
		      NUM_CTXT_ASIDS(info));

	info->active = &active_asids;
	info->reserved = &reserved_asids;
	info->flush_cpu_ctxt_cb = asid_flush_cpu_ctxt;
	info->set_reserved_bits = set_reserved_asid_bits;

	/*
	 * We cannot call set_reserved_asid_bits() here because CPU
	 * caps are not finalized yet, so it is safer to assume KPTI
	 * and reserve kernel ASID's from beginning.
	 */
	if (IS_ENABLED(CONFIG_UNMAP_KERNEL_AT_EL0))
		set_kpti_asid_bits(info, info->map);
	return 0;
}
early_initcall(asids_init);
