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
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

static struct asid_info
{
	atomic64_t	generation;
	unsigned long	*map;
	unsigned int	map_idx;
	atomic64_t __percpu	*active;
	u64 __percpu		*reserved;
	u32			bits;
	raw_spinlock_t		lock;
	/* Which CPU requires context flush on next call */
	cpumask_t		flush_pending;
	/* Pinned ASIDs info */
	unsigned long		*pinned_map;
	unsigned long		max_pinned_asids;
	unsigned long		nr_pinned_asids;
} asid_info;

#define active_asid(info, cpu)	 (*per_cpu_ptr((info)->active, cpu))
#define reserved_asid(info, cpu) (*per_cpu_ptr((info)->reserved, cpu))

static DEFINE_PER_CPU(atomic64_t, active_asids);
static DEFINE_PER_CPU(u64, reserved_asids);

#define ASID_MASK(info)			(~GENMASK((info)->bits - 1, 0))
#define NUM_CTXT_ASIDS(info)		(1UL << ((info)->bits))
#define ASID_FIRST_VERSION(info)        NUM_CTXT_ASIDS(info)

#define asid2idx(info, asid)		((asid) & ~ASID_MASK(info))
#define idx2asid(info, idx)		asid2idx(info, idx)

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

#define asid_gen_match(asid, info) \
	(!(((asid) ^ atomic64_read(&(info)->generation)) >> info->bits))

static void flush_context(struct asid_info *info)
{
	int i;
	u64 asid;

	/* Update the list of reserved ASIDs and the ASID bitmap. */
	set_reserved_asid_bits(info);

	for_each_possible_cpu(i) {
		asid = atomic64_xchg_relaxed(&active_asid(info, i), 0);
		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * ASID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (asid == 0)
			asid = reserved_asid(info, i);
		__set_bit(asid2idx(info, asid), info->map);
		reserved_asid(info, i) = asid;
	}

	/*
	 * Queue a TLB invalidation for each CPU to perform on next
	 * context-switch
	 */
	cpumask_setall(&info->flush_pending);
}

static bool check_update_reserved_asid(struct asid_info *info, u64 asid,
				       u64 newasid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved ASIDs looking for a match.
	 * If we find one, then we can update our mm to use newasid
	 * (i.e. the same ASID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old ASID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved ASID in a future
	 * generation.
	 */
	for_each_possible_cpu(cpu) {
		if (reserved_asid(info, cpu) == asid) {
			hit = true;
			reserved_asid(info, cpu) = newasid;
		}
	}

	return hit;
}

static u64 new_context(struct asid_info *info, atomic64_t *pasid,
		       refcount_t *pinned)
{
	u64 asid = atomic64_read(pasid);
	u64 generation = atomic64_read(&info->generation);

	if (asid != 0) {
		u64 newasid = generation | (asid & ~ASID_MASK(info));

		/*
		 * If our current ASID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_asid(info, asid, newasid))
			return newasid;

		/*
		 * If it is pinned, we can keep using it. Note that reserved
		 * takes priority, because even if it is also pinned, we need to
		 * update the generation into the reserved_asids.
		 */
		if (pinned && refcount_read(pinned))
			return newasid;

		/*
		 * We had a valid ASID in a previous life, so try to re-use
		 * it if possible.
		 */
		if (!__test_and_set_bit(asid2idx(info, asid), info->map))
			return newasid;
	}

	/*
	 * Allocate a free ASID. If we can't find one, take a note of the
	 * currently active ASIDs and mark the TLBs as requiring flushes.  We
	 * always count from ASID #2 (index 1), as we use ASID #0 when setting
	 * a reserved TTBR0 for the init_mm and we allocate ASIDs in even/odd
	 * pairs.
	 */
	asid = find_next_zero_bit(info->map, NUM_CTXT_ASIDS(info), info->map_idx);
	if (asid != NUM_CTXT_ASIDS(info))
		goto set_asid;

	/* We're out of ASIDs, so increment the global generation count */
	generation = atomic64_add_return_relaxed(ASID_FIRST_VERSION(info),
						 &info->generation);
	flush_context(info);

	/* We have more ASIDs than CPUs, so this will always succeed */
	asid = find_next_zero_bit(info->map, NUM_CTXT_ASIDS(info), 1);

set_asid:
	__set_bit(asid, info->map);
	info->map_idx = asid;
	return idx2asid(info, asid) | generation;
}

/*
 * Generate a new ASID for the context.
 *
 * @pasid: Pointer to the current ASID batch allocated. It will be updated
 * with the new ASID batch.
 * @pinned: refcount if asid is pinned.
 * Caller needs to make sure preempt is disabled before calling this function.
 */
static void asid_new_context(struct asid_info *info, atomic64_t *pasid,
			     refcount_t *pinned)
{
	unsigned long flags;
	u64 asid;
	unsigned int cpu = smp_processor_id();

	raw_spin_lock_irqsave(&info->lock, flags);
	/* Check that our ASID belongs to the current generation. */
	asid = atomic64_read(pasid);
	if (!asid_gen_match(asid, info)) {
		asid = new_context(info, pasid, pinned);
		atomic64_set(pasid, asid);
	}

	if (cpumask_test_and_clear_cpu(cpu, &info->flush_pending))
		local_flush_tlb_all();

	atomic64_set(&active_asid(info, cpu), asid);
	raw_spin_unlock_irqrestore(&info->lock, flags);
}

/*
 * Check the ASID is still valid for the context. If not generate a new ASID.
 *
 * @pasid: Pointer to the current ASID batch
 * @pinned: refcount if asid is pinned
 * Caller needs to make sure preempt is disabled before calling this function.
 */
static void asid_check_context(struct asid_info *info, atomic64_t *pasid,
			       refcount_t *pinned)
{
	u64 asid, old_active_asid;

	asid = atomic64_read(pasid);

	/*
	 * The memory ordering here is subtle.
	 * If our active_asid is non-zero and the ASID matches the current
	 * generation, then we update the active_asid entry with a relaxed
	 * cmpxchg. Racing with a concurrent rollover means that either:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on the
	 *   lock. Taking the lock synchronises with the rollover and so
	 *   we are forced to see the updated generation.
	 *
	 * - We get a valid ASID back from the cmpxchg, which means the
	 *   relaxed xchg in flush_context will treat us as reserved
	 *   because atomic RmWs are totally ordered for a given location.
	 */
	old_active_asid = atomic64_read(this_cpu_ptr(info->active));
	if (old_active_asid && asid_gen_match(asid, info) &&
	    atomic64_cmpxchg_relaxed(this_cpu_ptr(info->active),
				     old_active_asid, asid))
		return;

	asid_new_context(info, pasid, pinned);
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
	unsigned long flags;
	u64 asid;
	struct asid_info *info = &asid_info;

	if (!info->pinned_map)
		return 0;

	raw_spin_lock_irqsave(&info->lock, flags);

	asid = atomic64_read(&mm->context.id);

	if (refcount_inc_not_zero(&mm->context.pinned))
		goto out_unlock;

	if (info->nr_pinned_asids >= info->max_pinned_asids) {
		asid = 0;
		goto out_unlock;
	}

	if (!asid_gen_match(asid, info)) {
		/*
		 * We went through one or more rollover since that ASID was
		 * used. Ensure that it is still valid, or generate a new one.
		 */
		asid = new_context(info, &mm->context.id, &mm->context.pinned);
		atomic64_set(&mm->context.id, asid);
	}

	info->nr_pinned_asids++;
	__set_bit(asid2idx(info, asid), info->pinned_map);
	refcount_set(&mm->context.pinned, 1);

out_unlock:
	raw_spin_unlock_irqrestore(&info->lock, flags);

	asid &= ~ASID_MASK(info);

	/* Set the equivalent of USER_ASID_BIT */
	if (asid && arm64_kernel_unmapped_at_el0())
		asid |= 1;

	return asid;
}
EXPORT_SYMBOL_GPL(arm64_mm_context_get);

void arm64_mm_context_put(struct mm_struct *mm)
{
	unsigned long flags;
	struct asid_info *info = &asid_info;
	u64 asid = atomic64_read(&mm->context.id);

	if (!info->pinned_map)
		return;

	raw_spin_lock_irqsave(&info->lock, flags);

	if (refcount_dec_and_test(&mm->context.pinned)) {
		__clear_bit(asid2idx(info, asid), info->pinned_map);
		info->nr_pinned_asids--;
	}

	raw_spin_unlock_irqrestore(&info->lock, flags);
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

/*
 * Initialize the ASID allocator
 *
 * @info: Pointer to the asid allocator structure
 * @bits: Number of ASIDs available
 * @pinned: Support for Pinned ASIDs
 */
static int asid_allocator_init(struct asid_info *info, u32 bits, bool pinned)
{
	info->bits = bits;

	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more ASID than CPUs. ASID #0 is always reserved.
	 */
	WARN_ON(NUM_CTXT_ASIDS(info) - 1 <= num_possible_cpus());
	atomic64_set(&info->generation, ASID_FIRST_VERSION(info));
	info->map = kcalloc(BITS_TO_LONGS(NUM_CTXT_ASIDS(info)),
			    sizeof(*info->map), GFP_KERNEL);
	if (!info->map)
		return -ENOMEM;

	info->map_idx = 1;
	raw_spin_lock_init(&info->lock);

	if (pinned) {
		info->pinned_map = kcalloc(BITS_TO_LONGS(NUM_CTXT_ASIDS(info)),
					   sizeof(*info->pinned_map), GFP_KERNEL);
		info->nr_pinned_asids = 0;
	}

	return 0;
}

static int asids_init(void)
{
	struct asid_info *info = &asid_info;

	if (asid_allocator_init(info, get_cpu_asid_bits(), true))
		panic("Unable to initialize ASID allocator for %lu ASIDs\n",
		      NUM_CTXT_ASIDS(info));

	info->active = &active_asids;
	info->reserved = &reserved_asids;

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
