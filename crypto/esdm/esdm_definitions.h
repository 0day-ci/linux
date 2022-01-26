/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _ESDM_DEFINITIONS_H
#define _ESDM_DEFINITIONS_H

#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <linux/slab.h>

/*************************** General ESDM parameter ***************************/

/* Security strength of ESDM -- this must match DRNG security strength */
#define ESDM_DRNG_SECURITY_STRENGTH_BYTES 32
#define ESDM_DRNG_SECURITY_STRENGTH_BITS (ESDM_DRNG_SECURITY_STRENGTH_BYTES * 8)
#define ESDM_DRNG_INIT_SEED_SIZE_BITS \
	(ESDM_DRNG_SECURITY_STRENGTH_BITS +      \
	 CONFIG_CRYPTO_ESDM_SEED_BUFFER_INIT_ADD_BITS)
#define ESDM_DRNG_INIT_SEED_SIZE_BYTES (ESDM_DRNG_INIT_SEED_SIZE_BITS >> 3)

/*
 * SP800-90A defines a maximum request size of 1<<16 bytes. The given value is
 * considered a safer margin.
 *
 * This value is allowed to be changed.
 */
#define ESDM_DRNG_MAX_REQSIZE		(1<<12)

/*
 * SP800-90A defines a maximum number of requests between reseeds of 2^48.
 * The given value is considered a much safer margin, balancing requests for
 * frequent reseeds with the need to conserve entropy. This value MUST NOT be
 * larger than INT_MAX because it is used in an atomic_t.
 *
 * This value is allowed to be changed.
 */
#define ESDM_DRNG_RESEED_THRESH		(1<<20)

/*
 * Maximum DRNG generation operations without reseed having full entropy
 * This value defines the absolute maximum value of DRNG generation operations
 * without a reseed holding full entropy. ESDM_DRNG_RESEED_THRESH is the
 * threshold when a new reseed is attempted. But it is possible that this fails
 * to deliver full entropy. In this case the DRNG will continue to provide data
 * even though it was not reseeded with full entropy. To avoid in the extreme
 * case that no reseed is performed for too long, this threshold is enforced.
 * If that absolute low value is reached, the ESDM is marked as not operational.
 *
 * This value is allowed to be changed.
 */
#define ESDM_DRNG_MAX_WITHOUT_RESEED	(1<<30)

/*
 * Min required seed entropy is 128 bits covering the minimum entropy
 * requirement of SP800-131A and the German BSI's TR02102.
 *
 * This value is allowed to be changed.
 */
#define ESDM_FULL_SEED_ENTROPY_BITS	ESDM_DRNG_SECURITY_STRENGTH_BITS
#define ESDM_MIN_SEED_ENTROPY_BITS	128
#define ESDM_INIT_ENTROPY_BITS		32

/*
 * Wakeup value
 *
 * This value is allowed to be changed but must not be larger than the
 * digest size of the hash operation used update the aux_pool.
 */
#ifdef CONFIG_CRYPTO_ESDM_SHA256
# define ESDM_ATOMIC_DIGEST_SIZE	SHA256_DIGEST_SIZE
#else
# define ESDM_ATOMIC_DIGEST_SIZE	SHA1_DIGEST_SIZE
#endif
#define ESDM_WRITE_WAKEUP_ENTROPY	ESDM_ATOMIC_DIGEST_SIZE

/*
 * If the switching support is configured, we must provide support up to
 * the largest digest size. Without switching support, we know it is only
 * the built-in digest size.
 */
#ifdef CONFIG_CRYPTO_ESDM_CRYPTO_SWITCH
# define ESDM_MAX_DIGESTSIZE		64
#else
# define ESDM_MAX_DIGESTSIZE		ESDM_ATOMIC_DIGEST_SIZE
#endif

/*
 * Oversampling factor of timer-based events to obtain
 * ESDM_DRNG_SECURITY_STRENGTH_BYTES. This factor is used when a
 * high-resolution time stamp is not available. In this case, jiffies and
 * register contents are used to fill the entropy pool. These noise sources
 * are much less entropic than the high-resolution timer. The entropy content
 * is the entropy content assumed with ESDM_[IRQ|SCHED]_ENTROPY_BITS divided by
 * ESDM_ES_OVERSAMPLING_FACTOR.
 *
 * This value is allowed to be changed.
 */
#define ESDM_ES_OVERSAMPLING_FACTOR	10

/* Alignmask that is intended to be identical to CRYPTO_MINALIGN */
#define ESDM_KCAPI_ALIGN		ARCH_KMALLOC_MINALIGN

/*
 * This definition must provide a buffer that is equal to SHASH_DESC_ON_STACK
 * as it will be casted into a struct shash_desc.
 */
#define ESDM_POOL_SIZE	(sizeof(struct shash_desc) + HASH_MAX_DESCSIZE)

/****************************** Helper code ***********************************/

static inline u32 esdm_fast_noise_entropylevel(u32 ent_bits, u32 requested_bits)
{
	/* Obtain entropy statement */
	ent_bits = ent_bits * requested_bits / ESDM_DRNG_SECURITY_STRENGTH_BITS;
	/* Cap entropy to buffer size in bits */
	ent_bits = min_t(u32, ent_bits, requested_bits);
	return ent_bits;
}

/* Convert entropy in bits into nr. of events with the same entropy content. */
static inline u32 esdm_entropy_to_data(u32 entropy_bits, u32 entropy_rate)
{
	return ((entropy_bits * entropy_rate) /
		ESDM_DRNG_SECURITY_STRENGTH_BITS);
}

/* Convert number of events into entropy value. */
static inline u32 esdm_data_to_entropy(u32 irqnum, u32 entropy_rate)
{
	return ((irqnum * ESDM_DRNG_SECURITY_STRENGTH_BITS) /
		entropy_rate);
}

static inline u32 atomic_read_u32(atomic_t *v)
{
	return (u32)atomic_read(v);
}

#endif /* _ESDM_DEFINITIONS_H */
