/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _ESDM_DRNG_H
#define _ESDM_DRNG_H

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "esdm_definitions.h"

extern struct wait_queue_head esdm_init_wait;
extern int esdm_drng_reseed_max_time;
extern struct mutex esdm_crypto_cb_update;
extern const struct esdm_drng_cb *esdm_default_drng_cb;
extern const struct esdm_hash_cb *esdm_default_hash_cb;

/* DRNG state handle */
struct esdm_drng {
	void *drng;				/* DRNG handle */
	void *hash;				/* Hash handle */
	const struct esdm_drng_cb *drng_cb;	/* DRNG callbacks */
	const struct esdm_hash_cb *hash_cb;	/* Hash callbacks */
	atomic_t requests;			/* Number of DRNG requests */
	atomic_t requests_since_fully_seeded;	/* Number DRNG requests since
						 * last fully seeded
						 */
	unsigned long last_seeded;		/* Last time it was seeded */
	bool fully_seeded;			/* Is DRNG fully seeded? */
	bool force_reseed;			/* Force a reseed */

	rwlock_t hash_lock;			/* Lock hash_cb replacement */
	/* Lock write operations on DRNG state, DRNG replacement of drng_cb */
	struct mutex lock;			/* Non-atomic DRNG operation */
	spinlock_t spin_lock;			/* Atomic DRNG operation */
};

#define ESDM_DRNG_STATE_INIT(x, d, h, d_cb, h_cb) \
	.drng				= d, \
	.hash				= h, \
	.drng_cb			= d_cb, \
	.hash_cb			= h_cb, \
	.requests			= ATOMIC_INIT(ESDM_DRNG_RESEED_THRESH),\
	.requests_since_fully_seeded	= ATOMIC_INIT(0), \
	.last_seeded			= 0, \
	.fully_seeded			= false, \
	.force_reseed			= true, \
	.hash_lock			= __RW_LOCK_UNLOCKED(x.hash_lock)

struct esdm_drng *esdm_drng_init_instance(void);
struct esdm_drng *esdm_drng_node_instance(void);

void esdm_reset(void);
int esdm_drng_alloc_common(struct esdm_drng *drng,
			   const struct esdm_drng_cb *crypto_cb);
int esdm_drng_initalize(void);
bool esdm_sp80090c_compliant(void);
bool esdm_get_available(void);
void esdm_drng_reset(struct esdm_drng *drng);
void esdm_drng_inject(struct esdm_drng *drng, const u8 *inbuf, u32 inbuflen,
		      bool fully_seeded, const char *drng_type);
int esdm_drng_get(struct esdm_drng *drng, u8 *outbuf, u32 outbuflen);
int esdm_drng_sleep_while_nonoperational(int nonblock);
int esdm_drng_sleep_while_non_min_seeded(void);
int esdm_drng_get_sleep(u8 *outbuf, u32 outbuflen);
void esdm_drng_seed_work(struct work_struct *dummy);
void esdm_drng_force_reseed(void);

static inline u32 esdm_compress_osr(void)
{
	return esdm_sp80090c_compliant() ?
	       CONFIG_CRYPTO_ESDM_OVERSAMPLE_ES_BITS : 0;
}

static inline u32 esdm_reduce_by_osr(u32 entropy_bits)
{
	u32 osr_bits = esdm_compress_osr();

	return (entropy_bits >= osr_bits) ? (entropy_bits - osr_bits) : 0;
}

#endif /* _ESDM_DRNG_H */
