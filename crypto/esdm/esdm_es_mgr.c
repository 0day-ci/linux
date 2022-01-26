// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * ESDM Entropy sources management
 *
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/random.h>
#include <linux/utsname.h>
#include <linux/workqueue.h>

#include "esdm_drng_mgr.h"
#include "esdm_es_aux.h"
#include "esdm_es_jent.h"
#include "esdm_es_mgr.h"

struct esdm_state {
	bool perform_seedwork;		/* Can seed work be performed? */
	bool esdm_operational;		/* Is DRNG operational? */
	bool esdm_fully_seeded;		/* Is DRNG fully seeded? */
	bool esdm_min_seeded;		/* Is DRNG minimally seeded? */
	bool all_online_numa_node_seeded;/* All NUMA DRNGs seeded? */

	/*
	 * To ensure that external entropy providers cannot dominate the
	 * internal noise sources but yet cannot be dominated by internal
	 * noise sources, the following booleans are intended to allow
	 * external to provide seed once when a DRNG reseed occurs. This
	 * triggering of external noise source is performed even when the
	 * entropy pool has sufficient entropy.
	 */

	atomic_t boot_entropy_thresh;	/* Reseed threshold */
	atomic_t reseed_in_progress;	/* Flag for on executing reseed */
	struct work_struct esdm_seed_work;	/* (re)seed work queue */
};

static struct esdm_state esdm_state = {
	false, false, false, false, false,
	.boot_entropy_thresh	= ATOMIC_INIT(ESDM_INIT_ENTROPY_BITS),
	.reseed_in_progress	= ATOMIC_INIT(0),
};

/*
 * If the entropy count falls under this number of bits, then we
 * should wake up processes which are selecting or polling on write
 * access to /dev/random.
 */
u32 esdm_write_wakeup_bits = (ESDM_WRITE_WAKEUP_ENTROPY << 3);

/*
 * The entries must be in the same order as defined by
 * enum enum esdm_external_es
 */
struct esdm_es_cb *esdm_es[] = {
#ifdef CONFIG_CRYPTO_ESDM_JENT
	&esdm_es_jent,
#endif
	&esdm_es_aux
};

/********************************** Helper ***********************************/

/*
 * Reading of the ESDM pool is only allowed by one caller. The reading is
 * only performed to (re)seed DRNGs. Thus, if this "lock" is already taken,
 * the reseeding operation is in progress. The caller is not intended to wait
 * but continue with its other operation.
 */
int esdm_pool_trylock(void)
{
	return atomic_cmpxchg(&esdm_state.reseed_in_progress, 0, 1);
}

void esdm_pool_unlock(void)
{
	atomic_set(&esdm_state.reseed_in_progress, 0);
}

/* Set new entropy threshold for reseeding during boot */
void esdm_set_entropy_thresh(u32 new_entropy_bits)
{
	atomic_set(&esdm_state.boot_entropy_thresh, new_entropy_bits);
}

/*
 * Reset ESDM state - the entropy counters are reset, but the data that may
 * or may not have entropy remains in the pools as this data will not hurt.
 */
void esdm_reset_state(void)
{
	u32 i;

	for_each_esdm_es(i) {
		if (esdm_es[i]->reset)
			esdm_es[i]->reset();
	}
	esdm_state.esdm_operational = false;
	esdm_state.esdm_fully_seeded = false;
	esdm_state.esdm_min_seeded = false;
	esdm_state.all_online_numa_node_seeded = false;
	pr_debug("reset ESDM\n");
}

/* Set flag that all DRNGs are fully seeded */
void esdm_pool_all_numa_nodes_seeded(bool set)
{
	esdm_state.all_online_numa_node_seeded = set;
}

/* Return boolean whether ESDM reached minimally seed level */
bool esdm_state_min_seeded(void)
{
	return esdm_state.esdm_min_seeded;
}

/* Return boolean whether ESDM reached fully seed level */
bool esdm_state_fully_seeded(void)
{
	return esdm_state.esdm_fully_seeded;
}

/* Return boolean whether ESDM is considered fully operational */
bool esdm_state_operational(void)
{
	return esdm_state.esdm_operational;
}

static void esdm_init_wakeup(void)
{
	wake_up_all(&esdm_init_wait);
}

static bool esdm_fully_seeded(bool fully_seeded, u32 collected_entropy)
{
	return (collected_entropy >= esdm_get_seed_entropy_osr(fully_seeded));
}

/* Policy to check whether entropy buffer contains full seeded entropy */
bool esdm_fully_seeded_eb(bool fully_seeded, struct entropy_buf *eb)
{
	u32 i, collected_entropy = 0;

	for_each_esdm_es(i)
		collected_entropy += eb->e_bits[i];

	return esdm_fully_seeded(fully_seeded, collected_entropy);
}

/* Mark one DRNG as not fully seeded */
void esdm_unset_fully_seeded(struct esdm_drng *drng)
{
	drng->fully_seeded = false;
	esdm_pool_all_numa_nodes_seeded(false);

	/*
	 * The init DRNG instance must always be fully seeded as this instance
	 * is the fall-back if any of the per-NUMA node DRNG instances is
	 * insufficiently seeded. Thus, we mark the entire ESDM as
	 * non-operational if the initial DRNG becomes not fully seeded.
	 */
	if (drng == esdm_drng_init_instance() && esdm_state_operational()) {
		pr_debug("ESDM set to non-operational\n");
		esdm_state.esdm_operational = false;
		esdm_state.esdm_fully_seeded = false;

		/* If sufficient entropy is available, reseed now. */
		esdm_es_add_entropy();
	}
}

/* Policy to enable ESDM operational mode */
static void esdm_set_operational(void)
{
	/*
	 * ESDM is operational if the initial DRNG is fully seeded. This state
	 * can only occur if either the external entropy sources provided
	 * sufficient entropy, or the SP800-90B startup test completed for
	 * the internal ES to supply also entropy data.
	 */
	if (esdm_state.esdm_fully_seeded) {
		esdm_state.esdm_operational = true;
		esdm_init_wakeup();
		pr_info("ESDM fully operational\n");
	}
}

static u32 esdm_avail_entropy_thresh(void)
{
	u32 ent_thresh = esdm_security_strength();

	/*
	 * Apply oversampling during initialization according to SP800-90C as
	 * we request a larger buffer from the ES.
	 */
	if (esdm_sp80090c_compliant() &&
	    !esdm_state.all_online_numa_node_seeded)
		ent_thresh += CONFIG_CRYPTO_ESDM_SEED_BUFFER_INIT_ADD_BITS;

	return ent_thresh;
}

/* Available entropy in the entire ESDM considering all entropy sources */
u32 esdm_avail_entropy(void)
{
	u32 i, ent = 0, ent_thresh = esdm_avail_entropy_thresh();

	BUILD_BUG_ON(ARRAY_SIZE(esdm_es) != esdm_ext_es_last);
	for_each_esdm_es(i)
		ent += esdm_es[i]->curr_entropy(ent_thresh);
	return ent;
}

/*
 * esdm_init_ops() - Set seed stages of ESDM
 *
 * Set the slow noise source reseed trigger threshold. The initial threshold
 * is set to the minimum data size that can be read from the pool: a word. Upon
 * reaching this value, the next seed threshold of 128 bits is set followed
 * by 256 bits.
 *
 * @eb: buffer containing the size of entropy currently injected into DRNG - if
 *	NULL, the function obtains the available entropy from the ES.
 */
void esdm_init_ops(struct entropy_buf *eb)
{
	struct esdm_state *state = &esdm_state;
	u32 i, requested_bits, seed_bits = 0;

	if (state->esdm_operational)
		return;

	requested_bits = esdm_get_seed_entropy_osr(
					state->all_online_numa_node_seeded);

	if (eb) {
		for_each_esdm_es(i)
			seed_bits += eb->e_bits[i];
	} else {
		u32 ent_thresh = esdm_avail_entropy_thresh();

		for_each_esdm_es(i)
			seed_bits += esdm_es[i]->curr_entropy(ent_thresh);
	}

	/* DRNG is seeded with full security strength */
	if (state->esdm_fully_seeded) {
		esdm_set_operational();
		esdm_set_entropy_thresh(requested_bits);
	} else if (esdm_fully_seeded(state->all_online_numa_node_seeded,
				     seed_bits)) {
		state->esdm_fully_seeded = true;
		esdm_set_operational();
		state->esdm_min_seeded = true;
		pr_info("ESDM fully seeded with %u bits of entropy\n",
			seed_bits);
		esdm_set_entropy_thresh(requested_bits);
	} else if (!state->esdm_min_seeded) {

		/* DRNG is seeded with at least 128 bits of entropy */
		if (seed_bits >= ESDM_MIN_SEED_ENTROPY_BITS) {
			state->esdm_min_seeded = true;
			pr_info("ESDM minimally seeded with %u bits of entropy\n",
				seed_bits);
			esdm_set_entropy_thresh(requested_bits);
			esdm_init_wakeup();

		/* DRNG is seeded with at least ESDM_INIT_ENTROPY_BITS bits */
		} else if (seed_bits >= ESDM_INIT_ENTROPY_BITS) {
			pr_info("ESDM initial entropy level %u bits of entropy\n",
				seed_bits);
			esdm_set_entropy_thresh(ESDM_MIN_SEED_ENTROPY_BITS);
		}
	}
}

int __init esdm_rand_initialize(void)
{
	struct seed {
		ktime_t time;
		unsigned long data[(ESDM_MAX_DIGESTSIZE /
				    sizeof(unsigned long))];
		struct new_utsname utsname;
	} seed __aligned(ESDM_KCAPI_ALIGN);
	unsigned int i;

	BUILD_BUG_ON(ESDM_MAX_DIGESTSIZE % sizeof(unsigned long));

	seed.time = ktime_get_real();

	for (i = 0; i < ARRAY_SIZE(seed.data); i++) {
		if (!arch_get_random_seed_long(&(seed.data[i])) &&
		    !arch_get_random_long(&seed.data[i]))
			seed.data[i] = random_get_entropy();
	}
	memcpy(&seed.utsname, utsname(), sizeof(*(utsname())));

	esdm_pool_insert_aux((u8 *)&seed, sizeof(seed), 0);
	memzero_explicit(&seed, sizeof(seed));

	/* Initialize the seed work queue */
	INIT_WORK(&esdm_state.esdm_seed_work, esdm_drng_seed_work);
	esdm_state.perform_seedwork = true;

	return 0;
}

early_initcall(esdm_rand_initialize);

/* Interface requesting a reseed of the DRNG */
void esdm_es_add_entropy(void)
{
	/*
	 * Once all DRNGs are fully seeded, the system-triggered arrival of
	 * entropy will not cause any reseeding any more.
	 */
	if (likely(esdm_state.all_online_numa_node_seeded))
		return;

	/* Only trigger the DRNG reseed if we have collected entropy. */
	if (esdm_avail_entropy() <
	    atomic_read_u32(&esdm_state.boot_entropy_thresh))
		return;

	/* Ensure that the seeding only occurs once at any given time. */
	if (esdm_pool_trylock())
		return;

	/* Seed the DRNG with any available noise. */
	if (esdm_state.perform_seedwork)
		schedule_work(&esdm_state.esdm_seed_work);
	else
		esdm_drng_seed_work(NULL);
}

/* Fill the seed buffer with data from the noise sources */
void esdm_fill_seed_buffer(struct entropy_buf *eb, u32 requested_bits)
{
	struct esdm_state *state = &esdm_state;
	u32 i, req_ent = esdm_sp80090c_compliant() ?
			  esdm_security_strength() : ESDM_MIN_SEED_ENTROPY_BITS;

	/* Guarantee that requested bits is a multiple of bytes */
	BUILD_BUG_ON(ESDM_DRNG_SECURITY_STRENGTH_BITS % 8);

	/* always reseed the DRNG with the current time stamp */
	eb->now = random_get_entropy();

	/*
	 * Require at least 128 bits of entropy for any reseed. If the ESDM is
	 * operated SP800-90C compliant we want to comply with SP800-90A section
	 * 9.2 mandating that DRNG is reseeded with the security strength.
	 */
	if (state->esdm_fully_seeded && (esdm_avail_entropy() < req_ent)) {
		for_each_esdm_es(i)
			eb->e_bits[i] = 0;

		return;
	}

	/* Concatenate the output of the entropy sources. */
	for_each_esdm_es(i) {
		esdm_es[i]->get_ent(eb, requested_bits,
				    state->esdm_fully_seeded);
	}
}
