// SPDX-License-Identifier: GPL-2.0
/*
 * Generic ASID allocator.
 *
 * Based on arch/arm/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/slab.h>

#include <asm/lib_asid.h>

#define reserved_asid(info, cpu) (*per_cpu_ptr((info)->reserved, cpu))

#define ASID_MASK(info)			(~GENMASK((info)->bits - 1, 0))
#define ASID_FIRST_VERSION(info)        NUM_CTXT_ASIDS(info)

#define asid2idx(info, asid)		((asid) & ~ASID_MASK(info))
#define idx2asid(info, idx)		asid2idx(info, idx)

static void flush_context(struct asid_info *info)
{
	int i;
	u64 asid;

	/* Update the list of reserved ASIDs and the ASID bitmap. */
	if (info->set_reserved_bits)
		info->set_reserved_bits(info);

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
 * @pinned: refcount if asid is pinned
 * @cpu: current CPU ID. Must have been acquired through get_cpu()
 */
void asid_new_context(struct asid_info *info, atomic64_t *pasid,
		      refcount_t *pinned, unsigned int cpu)
{
	unsigned long flags;
	u64 asid;

	raw_spin_lock_irqsave(&info->lock, flags);
	/* Check that our ASID belongs to the current generation. */
	asid = atomic64_read(pasid);
	if (!asid_gen_match(asid, info)) {
		asid = new_context(info, pasid, pinned);
		atomic64_set(pasid, asid);
	}

	if (cpumask_test_and_clear_cpu(cpu, &info->flush_pending) &&
	    info->flush_cpu_ctxt_cb)
		info->flush_cpu_ctxt_cb();

	atomic64_set(&active_asid(info, cpu), asid);
	raw_spin_unlock_irqrestore(&info->lock, flags);
}

unsigned long asid_context_pinned_get(struct asid_info *info,
				      atomic64_t *pasid,
				      refcount_t *pinned)
{
	unsigned long flags;
	u64 asid;

	if (!info->pinned_map)
		return 0;

	raw_spin_lock_irqsave(&info->lock, flags);

	asid = atomic64_read(pasid);

	if (refcount_inc_not_zero(pinned))
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
		asid = new_context(info, pasid, pinned);
		atomic64_set(pasid, asid);
	}

	info->nr_pinned_asids++;
	__set_bit(asid2idx(info, asid), info->pinned_map);
	refcount_set(pinned, 1);

out_unlock:
	raw_spin_unlock_irqrestore(&info->lock, flags);
	asid &= ~ASID_MASK(info);
	return asid;
}

void asid_context_pinned_put(struct asid_info *info, atomic64_t *pasid,
			     refcount_t *pinned)
{
	unsigned long flags;
	u64 asid = atomic64_read(pasid);

	if (!info->pinned_map)
		return;

	raw_spin_lock_irqsave(&info->lock, flags);

	if (refcount_dec_and_test(pinned)) {
		__clear_bit(asid2idx(info, asid), info->pinned_map);
		info->nr_pinned_asids--;
	}

	raw_spin_unlock_irqrestore(&info->lock, flags);
}

/*
 * Initialize the ASID allocator
 *
 * @info: Pointer to the asid allocator structure
 * @bits: Number of ASIDs available
 * @pinned: Support for Pinned ASIDs
 */
int asid_allocator_init(struct asid_info *info, u32 bits, bool pinned)
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
