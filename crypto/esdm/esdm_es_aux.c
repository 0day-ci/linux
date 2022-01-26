// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * ESDM Slow Entropy Source: Auxiliary entropy pool
 *
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/esdm.h>

#include "esdm_es_aux.h"
#include "esdm_es_mgr.h"

/*
 * This is the auxiliary pool
 *
 * The aux pool array is aligned to 8 bytes to comfort the kernel crypto API
 * cipher implementations of the hash functions used to read the pool: for some
 * accelerated implementations, we need an alignment to avoid a realignment
 * which involves memcpy(). The alignment to 8 bytes should satisfy all crypto
 * implementations.
 */
struct esdm_pool {
	u8 aux_pool[ESDM_POOL_SIZE];	/* Aux pool: digest state */
	atomic_t aux_entropy_bits;
	atomic_t digestsize;		/* Digest size of used hash */
	bool initialized;		/* Aux pool initialized? */

	/* Serialize read of entropy pool and update of aux pool */
	spinlock_t lock;
};

static struct esdm_pool esdm_pool __aligned(ESDM_KCAPI_ALIGN) = {
	.aux_entropy_bits	= ATOMIC_INIT(0),
	.digestsize		= ATOMIC_INIT(ESDM_ATOMIC_DIGEST_SIZE),
	.initialized		= false,
	.lock			= __SPIN_LOCK_UNLOCKED(esdm_pool.lock)
};

/********************************** Helper ***********************************/

/* Entropy in bits present in aux pool */
static u32 esdm_aux_avail_entropy(u32 __unused)
{
	/* Cap available entropy with max entropy */
	u32 avail_bits = min_t(u32, esdm_get_digestsize(),
			       atomic_read_u32(&esdm_pool.aux_entropy_bits));

	/* Consider oversampling rate due to aux pool conditioning */
	return esdm_reduce_by_osr(avail_bits);
}

/* Set the digest size of the used hash in bytes */
static void esdm_set_digestsize(u32 digestsize)
{
	struct esdm_pool *pool = &esdm_pool;
	u32 ent_bits = atomic_xchg_relaxed(&pool->aux_entropy_bits, 0),
	    old_digestsize = esdm_get_digestsize();

	atomic_set(&esdm_pool.digestsize, digestsize);

	/*
	 * Update the write wakeup threshold which must not be larger
	 * than the digest size of the current conditioning hash.
	 */
	digestsize = esdm_reduce_by_osr(digestsize << 3);
	esdm_write_wakeup_bits = digestsize;

	/*
	 * In case the new digest is larger than the old one, cap the available
	 * entropy to the old message digest used to process the existing data.
	 */
	ent_bits = min_t(u32, ent_bits, old_digestsize);
	atomic_add(ent_bits, &pool->aux_entropy_bits);
}

static int __init esdm_init_wakeup_bits(void)
{
	u32 digestsize = esdm_reduce_by_osr(esdm_get_digestsize());

	esdm_write_wakeup_bits = digestsize;
	return 0;
}
core_initcall(esdm_init_wakeup_bits);

/* Obtain the digest size provided by the used hash in bits */
u32 esdm_get_digestsize(void)
{
	return atomic_read_u32(&esdm_pool.digestsize) << 3;
}

/* Set entropy content in user-space controllable aux pool */
void esdm_pool_set_entropy(u32 entropy_bits)
{
	atomic_set(&esdm_pool.aux_entropy_bits, entropy_bits);
}

static void esdm_aux_reset(void)
{
	esdm_pool_set_entropy(0);
}

/*
 * Replace old with new hash for auxiliary pool handling
 *
 * Assumption: the caller must guarantee that the new_cb is available during the
 * entire operation (e.g. it must hold the write lock against pointer updating).
 */
static int
esdm_aux_switch_hash(struct esdm_drng *drng, int __unused,
		     const struct esdm_hash_cb *new_cb, void *new_hash,
		     const struct esdm_hash_cb *old_cb)
{
	struct esdm_drng *init_drng = esdm_drng_init_instance();
	struct esdm_pool *pool = &esdm_pool;
	struct shash_desc *shash = (struct shash_desc *)pool->aux_pool;
	u8 digest[ESDM_MAX_DIGESTSIZE];
	int ret;

	if (!IS_ENABLED(CONFIG_CRYPTO_ESDM_CRYPTO_SWITCH))
		return -EOPNOTSUPP;

	if (unlikely(!pool->initialized))
		return 0;

	/* We only switch if the processed DRNG is the initial DRNG. */
	if (init_drng != drng)
		return 0;

	/* Get the aux pool hash with old digest ... */
	ret = old_cb->hash_final(shash, digest) ?:
	      /* ... re-initialize the hash with the new digest ... */
	      new_cb->hash_init(shash, new_hash) ?:
	      /*
	       * ... feed the old hash into the new state. We may feed
	       * uninitialized memory into the new state, but this is
	       * considered no issue and even good as we have some more
	       * uncertainty here.
	       */
	      new_cb->hash_update(shash, digest, sizeof(digest));
	if (!ret) {
		esdm_set_digestsize(new_cb->hash_digestsize(new_hash));
		pr_debug("Re-initialize aux entropy pool with hash %s\n",
			 new_cb->hash_name());
	}

	memzero_explicit(digest, sizeof(digest));
	return ret;
}

/* Insert data into auxiliary pool by using the hash update function. */
static int
esdm_aux_pool_insert_locked(const u8 *inbuf, u32 inbuflen, u32 entropy_bits)
{
	struct esdm_pool *pool = &esdm_pool;
	struct shash_desc *shash = (struct shash_desc *)pool->aux_pool;
	struct esdm_drng *drng = esdm_drng_init_instance();
	const struct esdm_hash_cb *hash_cb;
	unsigned long flags;
	void *hash;
	int ret;

	entropy_bits = min_t(u32, entropy_bits, inbuflen << 3);

	read_lock_irqsave(&drng->hash_lock, flags);
	hash_cb = drng->hash_cb;
	hash = drng->hash;

	if (unlikely(!pool->initialized)) {
		ret = hash_cb->hash_init(shash, hash);
		if (ret)
			goto out;
		pool->initialized = true;
	}

	ret = hash_cb->hash_update(shash, inbuf, inbuflen);
	if (ret)
		goto out;

	/*
	 * Cap the available entropy to the hash output size compliant to
	 * SP800-90B section 3.1.5.1 table 1.
	 */
	entropy_bits += atomic_read_u32(&pool->aux_entropy_bits);
	atomic_set(&pool->aux_entropy_bits,
		   min_t(u32, entropy_bits,
			 hash_cb->hash_digestsize(hash) << 3));

out:
	read_unlock_irqrestore(&drng->hash_lock, flags);
	return ret;
}

int esdm_pool_insert_aux(const u8 *inbuf, u32 inbuflen, u32 entropy_bits)
{
	struct esdm_pool *pool = &esdm_pool;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pool->lock, flags);
	ret = esdm_aux_pool_insert_locked(inbuf, inbuflen, entropy_bits);
	spin_unlock_irqrestore(&pool->lock, flags);

	esdm_es_add_entropy();

	return ret;
}
EXPORT_SYMBOL(esdm_pool_insert_aux);

/************************* Get data from entropy pool *************************/

/*
 * Get auxiliary entropy pool and its entropy content for seed buffer.
 * Caller must hold esdm_pool.pool->lock.
 * @outbuf: buffer to store data in with size requested_bits
 * @requested_bits: Requested amount of entropy
 * @return: amount of entropy in outbuf in bits.
 */
static u32 esdm_aux_get_pool(u8 *outbuf, u32 requested_bits)
{
	struct esdm_pool *pool = &esdm_pool;
	struct shash_desc *shash = (struct shash_desc *)pool->aux_pool;
	struct esdm_drng *drng = esdm_drng_init_instance();
	const struct esdm_hash_cb *hash_cb;
	unsigned long flags;
	void *hash;
	u32 collected_ent_bits, returned_ent_bits, unused_bits = 0,
	    digestsize, digestsize_bits, requested_bits_osr;
	u8 aux_output[ESDM_MAX_DIGESTSIZE];

	if (unlikely(!pool->initialized))
		return 0;

	read_lock_irqsave(&drng->hash_lock, flags);

	hash_cb = drng->hash_cb;
	hash = drng->hash;
	digestsize = hash_cb->hash_digestsize(hash);
	digestsize_bits = digestsize << 3;

	/* Cap to maximum entropy that can ever be generated with given hash */
	esdm_cap_requested(digestsize_bits, requested_bits);

	/* Ensure that no more than the size of aux_pool can be requested */
	requested_bits = min_t(u32, requested_bits, (ESDM_MAX_DIGESTSIZE << 3));
	requested_bits_osr = requested_bits + esdm_compress_osr();

	/* Cap entropy with entropy counter from aux pool and the used digest */
	collected_ent_bits = min_t(u32, digestsize_bits,
			       atomic_xchg_relaxed(&pool->aux_entropy_bits, 0));

	/* We collected too much entropy and put the overflow back */
	if (collected_ent_bits > requested_bits_osr) {
		/* Amount of bits we collected too much */
		unused_bits = collected_ent_bits - requested_bits_osr;
		/* Put entropy back */
		atomic_add(unused_bits, &pool->aux_entropy_bits);
		/* Fix collected entropy */
		collected_ent_bits = requested_bits_osr;
	}

	/* Apply oversampling: discount requested oversampling rate */
	returned_ent_bits = esdm_reduce_by_osr(collected_ent_bits);

	pr_debug("obtained %u bits by collecting %u bits of entropy from aux pool, %u bits of entropy remaining\n",
		 returned_ent_bits, collected_ent_bits, unused_bits);

	/* Get the digest for the aux pool to be returned to the caller ... */
	if (hash_cb->hash_final(shash, aux_output) ||
	    /*
	     * ... and re-initialize the aux state. Do not add the aux pool
	     * digest for backward secrecy as it will be added with the
	     * insertion of the complete seed buffer after it has been filled.
	     */
	    hash_cb->hash_init(shash, hash)) {
		returned_ent_bits = 0;
	} else {
		/*
		 * Do not truncate the output size exactly to collected_ent_bits
		 * as the aux pool may contain data that is not credited with
		 * entropy, but we want to use them to stir the DRNG state.
		 */
		memcpy(outbuf, aux_output, requested_bits >> 3);
	}

	read_unlock_irqrestore(&drng->hash_lock, flags);
	memzero_explicit(aux_output, digestsize);
	return returned_ent_bits;
}

static void esdm_aux_get_backtrack(struct entropy_buf *eb, u32 requested_bits,
				   bool __unused)
{
	struct esdm_pool *pool = &esdm_pool;
	unsigned long flags;

	/* Ensure aux pool extraction and backtracking op are atomic */
	spin_lock_irqsave(&pool->lock, flags);

	eb->e_bits[esdm_ext_es_aux] = esdm_aux_get_pool(eb->e[esdm_ext_es_aux],
							requested_bits);

	/* Mix the extracted data back into pool for backtracking resistance */
	if (esdm_aux_pool_insert_locked((u8 *)eb,
					sizeof(struct entropy_buf), 0))
		pr_warn("Backtracking resistance operation failed\n");

	spin_unlock_irqrestore(&pool->lock, flags);
}

static void esdm_aux_es_state(unsigned char *buf, size_t buflen)
{
	const struct esdm_drng *esdm_drng_init = esdm_drng_init_instance();

	/* Assume the esdm_drng_init lock is taken by caller */
	snprintf(buf, buflen,
		 " Hash for operating entropy pool: %s\n"
		 " Available entropy: %u\n",
		 esdm_drng_init->hash_cb->hash_name(),
		 esdm_aux_avail_entropy(0));
}

struct esdm_es_cb esdm_es_aux = {
	.name			= "Auxiliary",
	.get_ent		= esdm_aux_get_backtrack,
	.curr_entropy		= esdm_aux_avail_entropy,
	.max_entropy		= esdm_get_digestsize,
	.state			= esdm_aux_es_state,
	.reset			= esdm_aux_reset,
	.switch_hash		= esdm_aux_switch_hash,
};
