// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * ESDM DRNG management
 *
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/esdm.h>
#include <linux/fips.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "esdm_drng_kcapi.h"
#include "esdm_drng_mgr.h"
#include "esdm_es_aux.h"
#include "esdm_es_mgr.h"
#include "esdm_sha.h"

/*
 * Maximum number of seconds between DRNG reseed intervals of the DRNG. Note,
 * this is enforced with the next request of random numbers from the
 * DRNG. Setting this value to zero implies a reseeding attempt before every
 * generated random number.
 */
int esdm_drng_reseed_max_time = 600;

/*
 * Is ESDM for general-purpose use (i.e. is at least the esdm_drng_init
 * fully allocated)?
 */
static atomic_t esdm_avail = ATOMIC_INIT(0);

/*
 * Default hash callback that provides the crypto primitive right from the
 * kernel start. It must not perform any memory allocation operation, but
 * simply perform the hash calculation.
 */
const struct esdm_hash_cb *esdm_default_hash_cb = &esdm_sha_hash_cb;

/*
 * Default DRNG callback that provides the crypto primitive which is
 * allocated either during late kernel boot stage. So, it is permissible for
 * the callback to perform memory allocation operations.
 */
const struct esdm_drng_cb *esdm_default_drng_cb = &esdm_kcapi_drng_cb;

/* DRNG for non-atomic use cases */
static struct esdm_drng esdm_drng_init = {
	ESDM_DRNG_STATE_INIT(esdm_drng_init, NULL, NULL, NULL,
			     &esdm_sha_hash_cb),
	.lock = __MUTEX_INITIALIZER(esdm_drng_init.lock),
};

static u32 max_wo_reseed = ESDM_DRNG_MAX_WITHOUT_RESEED;
#ifdef CONFIG_CRYPTO_ESDM_RUNTIME_MAX_WO_RESEED_CONFIG
module_param(max_wo_reseed, uint, 0444);
MODULE_PARM_DESC(max_wo_reseed,
		 "Maximum number of DRNG generate operation without full reseed\n");
#endif

/* Wait queue to wait until the ESDM is initialized - can freely be used */
DECLARE_WAIT_QUEUE_HEAD(esdm_init_wait);

/********************************** Helper ************************************/

bool esdm_get_available(void)
{
	return likely(atomic_read(&esdm_avail));
}

struct esdm_drng *esdm_drng_init_instance(void)
{
	return &esdm_drng_init;
}

struct esdm_drng *esdm_drng_node_instance(void)
{
	return esdm_drng_init_instance();
}

void esdm_drng_reset(struct esdm_drng *drng)
{
	atomic_set(&drng->requests, ESDM_DRNG_RESEED_THRESH);
	atomic_set(&drng->requests_since_fully_seeded, 0);
	drng->last_seeded = jiffies;
	drng->fully_seeded = false;
	drng->force_reseed = true;
	pr_debug("reset DRNG\n");
}

/* Initialize the DRNG, except the mutex lock */
int esdm_drng_alloc_common(struct esdm_drng *drng,
			   const struct esdm_drng_cb *drng_cb)
{
	if (!drng || !drng_cb)
		return -EINVAL;

	drng->drng_cb = drng_cb;
	drng->drng = drng_cb->drng_alloc(ESDM_DRNG_SECURITY_STRENGTH_BYTES);
	if (IS_ERR(drng->drng))
		return PTR_ERR(drng->drng);

	esdm_drng_reset(drng);
	return 0;
}

/* Initialize the default DRNG during boot and perform its seeding */
int esdm_drng_initalize(void)
{
	int ret;

	if (esdm_get_available())
		return 0;

	/* Catch programming error */
	WARN_ON(esdm_drng_init.hash_cb != esdm_default_hash_cb);

	mutex_lock(&esdm_drng_init.lock);
	if (esdm_get_available()) {
		mutex_unlock(&esdm_drng_init.lock);
		return 0;
	}

	ret = esdm_drng_alloc_common(&esdm_drng_init, esdm_default_drng_cb);
	mutex_unlock(&esdm_drng_init.lock);
	if (ret)
		return ret;

	pr_debug("ESDM for general use is available\n");
	atomic_set(&esdm_avail, 1);

	/* Seed the DRNG with any entropy available */
	if (!esdm_pool_trylock()) {
		pr_info("Initial DRNG initialized triggering first seeding\n");
		esdm_drng_seed_work(NULL);
	} else {
		pr_info("Initial DRNG initialized without seeding\n");
	}

	return 0;
}

static int __init esdm_drng_make_available(void)
{
	return esdm_drng_initalize();
}
late_initcall(esdm_drng_make_available);

bool esdm_sp80090c_compliant(void)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_ESDM_OVERSAMPLE_ENTROPY_SOURCES))
		return false;

	/* SP800-90C only requested in FIPS mode */
	return fips_enabled;
}

/************************* Random Number Generation ***************************/

/* Inject a data buffer into the DRNG - caller must hold its lock */
void esdm_drng_inject(struct esdm_drng *drng, const u8 *inbuf, u32 inbuflen,
		      bool fully_seeded, const char *drng_type)
{
	BUILD_BUG_ON(ESDM_DRNG_RESEED_THRESH > INT_MAX);
	pr_debug("seeding %s DRNG with %u bytes\n", drng_type, inbuflen);
	if (drng->drng_cb->drng_seed(drng->drng, inbuf, inbuflen) < 0) {
		pr_warn("seeding of %s DRNG failed\n", drng_type);
		drng->force_reseed = true;
	} else {
		int gc = ESDM_DRNG_RESEED_THRESH - atomic_read(&drng->requests);

		pr_debug("%s DRNG stats since last seeding: %lu secs; generate calls: %d\n",
			 drng_type,
			 (time_after(jiffies, drng->last_seeded) ?
			  (jiffies - drng->last_seeded) : 0) / HZ, gc);

		/* Count the numbers of generate ops since last fully seeded */
		if (fully_seeded)
			atomic_set(&drng->requests_since_fully_seeded, 0);
		else
			atomic_add(gc, &drng->requests_since_fully_seeded);

		drng->last_seeded = jiffies;
		atomic_set(&drng->requests, ESDM_DRNG_RESEED_THRESH);
		drng->force_reseed = false;

		if (!drng->fully_seeded) {
			drng->fully_seeded = fully_seeded;
			if (drng->fully_seeded)
				pr_debug("%s DRNG fully seeded\n", drng_type);
		}
	}
}

/* Perform the seeding of the DRNG with data from noise source */
static void esdm_drng_seed_es(struct esdm_drng *drng)
{
	struct entropy_buf seedbuf __aligned(ESDM_KCAPI_ALIGN);

	esdm_fill_seed_buffer(&seedbuf,
			      esdm_get_seed_entropy_osr(drng->fully_seeded));

	mutex_lock(&drng->lock);
	esdm_drng_inject(drng, (u8 *)&seedbuf, sizeof(seedbuf),
			 esdm_fully_seeded_eb(drng->fully_seeded, &seedbuf),
			 "regular");
	mutex_unlock(&drng->lock);

	/* Set the seeding state of the ESDM */
	esdm_init_ops(&seedbuf);

	memzero_explicit(&seedbuf, sizeof(seedbuf));
}

static void esdm_drng_seed(struct esdm_drng *drng)
{
	BUILD_BUG_ON(ESDM_MIN_SEED_ENTROPY_BITS >
		     ESDM_DRNG_SECURITY_STRENGTH_BITS);

	if (esdm_get_available()) {
		/* (Re-)Seed DRNG */
		esdm_drng_seed_es(drng);
	} else {
		esdm_init_ops(NULL);
	}
}

static void _esdm_drng_seed_work(struct esdm_drng *drng, u32 node)
{
	pr_debug("reseed triggered by system events for DRNG on NUMA node %d\n",
		 node);
	esdm_drng_seed(drng);
	if (drng->fully_seeded) {
		/* Prevent reseed storm */
		drng->last_seeded += node * 100 * HZ;
	}
}

/*
 * DRNG reseed trigger: Kernel thread handler triggered by the schedule_work()
 */
void esdm_drng_seed_work(struct work_struct *dummy)
{
	if (!esdm_drng_init.fully_seeded) {
		_esdm_drng_seed_work(&esdm_drng_init, 0);
		goto out;
	}

	esdm_pool_all_numa_nodes_seeded(true);

out:
	/* Allow the seeding operation to be called again */
	esdm_pool_unlock();
}

/* Force all DRNGs to reseed before next generation */
void esdm_drng_force_reseed(void)
{
	esdm_drng_init.force_reseed = esdm_drng_init.fully_seeded;
	pr_debug("force reseed of initial DRNG\n");
}
EXPORT_SYMBOL(esdm_drng_force_reseed);

static bool esdm_drng_must_reseed(struct esdm_drng *drng)
{
	return (atomic_dec_and_test(&drng->requests) ||
		drng->force_reseed ||
		time_after(jiffies,
			   drng->last_seeded + esdm_drng_reseed_max_time * HZ));
}

/*
 * esdm_drng_get() - Get random data out of the DRNG which is reseeded
 * frequently.
 *
 * @drng: DRNG instance
 * @outbuf: buffer for storing random data
 * @outbuflen: length of outbuf
 *
 * Return:
 * * < 0 in error case (DRNG generation or update failed)
 * * >=0 returning the returned number of bytes
 */
int esdm_drng_get(struct esdm_drng *drng, u8 *outbuf, u32 outbuflen)
{
	u32 processed = 0;

	if (!outbuf || !outbuflen)
		return 0;

	if (!esdm_get_available())
		return -EOPNOTSUPP;

	outbuflen = min_t(size_t, outbuflen, INT_MAX);

	/* If DRNG operated without proper reseed for too long, block ESDM */
	BUILD_BUG_ON(ESDM_DRNG_MAX_WITHOUT_RESEED < ESDM_DRNG_RESEED_THRESH);
	if (atomic_read_u32(&drng->requests_since_fully_seeded) > max_wo_reseed)
		esdm_unset_fully_seeded(drng);

	while (outbuflen) {
		u32 todo = min_t(u32, outbuflen, ESDM_DRNG_MAX_REQSIZE);
		int ret;

		if (esdm_drng_must_reseed(drng)) {
			if (esdm_pool_trylock()) {
				drng->force_reseed = true;
			} else {
				esdm_drng_seed(drng);
				esdm_pool_unlock();
			}
		}

		mutex_lock(&drng->lock);
		ret = drng->drng_cb->drng_generate(drng->drng,
						   outbuf + processed, todo);
		mutex_unlock(&drng->lock);
		if (ret <= 0) {
			pr_warn("getting random data from DRNG failed (%d)\n",
				ret);
			return -EFAULT;
		}
		processed += ret;
		outbuflen -= ret;
	}

	return processed;
}

int esdm_drng_get_sleep(u8 *outbuf, u32 outbuflen)
{
	struct esdm_drng *drng = &esdm_drng_init;
	int ret;

	might_sleep();

	ret = esdm_drng_initalize();
	if (ret)
		return ret;

	return esdm_drng_get(drng, outbuf, outbuflen);
}

/* Reset ESDM such that all existing entropy is gone */
static void _esdm_reset(struct work_struct *work)
{
	mutex_lock(&esdm_drng_init.lock);
	esdm_drng_reset(&esdm_drng_init);
	mutex_unlock(&esdm_drng_init.lock);

	esdm_set_entropy_thresh(ESDM_INIT_ENTROPY_BITS);

	esdm_reset_state();
}

static DECLARE_WORK(esdm_reset_work, _esdm_reset);

void esdm_reset(void)
{
	schedule_work(&esdm_reset_work);
}

/******************* Generic ESDM kernel output interfaces ********************/

int esdm_drng_sleep_while_nonoperational(int nonblock)
{
	if (likely(!esdm_state_operational()))
		return 0;
	if (nonblock)
		return -EAGAIN;
	return wait_event_interruptible(esdm_init_wait,
					esdm_state_operational());
}

int esdm_drng_sleep_while_non_min_seeded(void)
{
	if (likely(esdm_state_min_seeded()))
		return 0;
	return wait_event_interruptible(esdm_init_wait,
					esdm_state_min_seeded());
}

void esdm_get_random_bytes_full(void *buf, int nbytes)
{
	esdm_drng_sleep_while_nonoperational(0);
	esdm_drng_get_sleep((u8 *)buf, (u32)nbytes);
}
EXPORT_SYMBOL(esdm_get_random_bytes_full);

void esdm_get_random_bytes_min(void *buf, int nbytes)
{
	esdm_drng_sleep_while_non_min_seeded();
	esdm_drng_get_sleep((u8 *)buf, (u32)nbytes);
}
EXPORT_SYMBOL(esdm_get_random_bytes_min);
