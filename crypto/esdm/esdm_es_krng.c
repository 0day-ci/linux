// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * ESDM Fast Entropy Source: Linux kernel RNG (random.c)
 *
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fips.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/types.h>

#include "esdm_es_aux.h"
#include "esdm_es_krng.h"

static u32 krng_entropy = CONFIG_CRYPTO_ESDM_KERNEL_RNG_ENTROPY_RATE;
#ifdef CONFIG_CRYPTO_ESDM_RUNTIME_ES_CONFIG
module_param(krng_entropy, uint, 0644);
MODULE_PARM_DESC(krng_entropy, "Entropy in bits of 256 data bits from the kernel RNG noise source");
#endif

static atomic_t esdm_krng_initial_rate = ATOMIC_INIT(0);

static struct random_ready_callback esdm_krng_ready = {
	.owner = THIS_MODULE,
	.func = NULL,
};

static u32 esdm_krng_fips_entropylevel(u32 entropylevel)
{
	return fips_enabled ? 0 : entropylevel;
}

static void esdm_krng_adjust_entropy(struct random_ready_callback *rdy)
{
	u32 entropylevel;

	krng_entropy = atomic_read_u32(&esdm_krng_initial_rate);

	entropylevel = esdm_krng_fips_entropylevel(krng_entropy);
	pr_debug("Kernel RNG is fully seeded, setting entropy rate to %u bits of entropy\n",
		 entropylevel);
	esdm_drng_force_reseed();
	if (entropylevel)
		esdm_es_add_entropy();
}

static u32 esdm_krng_entropylevel(u32 requested_bits)
{
	if (esdm_krng_ready.func == NULL) {
		int err;

		esdm_krng_ready.func = esdm_krng_adjust_entropy;

		err = add_random_ready_callback(&esdm_krng_ready);
		switch (err) {
		case 0:
			atomic_set(&esdm_krng_initial_rate, krng_entropy);
			krng_entropy = 0;
			pr_debug("Kernel RNG is not yet seeded, setting entropy rate to 0 bits of entropy\n");
			break;

		case -EALREADY:
			pr_debug("Kernel RNG is fully seeded, setting entropy rate to %u bits of entropy\n",
				 esdm_krng_fips_entropylevel(krng_entropy));
			break;
		default:
			esdm_krng_ready.func = NULL;
			return 0;
		}
	}

	return esdm_fast_noise_entropylevel(
		esdm_krng_fips_entropylevel(krng_entropy), requested_bits);
}

static u32 esdm_krng_poolsize(void)
{
	return esdm_krng_entropylevel(esdm_security_strength());
}

/*
 * esdm_krng_get() - Get kernel RNG entropy
 *
 * @eb: entropy buffer to store entropy
 * @requested_bits: requested entropy in bits
 */
static void esdm_krng_get(struct entropy_buf *eb, u32 requested_bits,
			  bool __unused)
{
	u32 ent_bits = esdm_krng_entropylevel(requested_bits);

	get_random_bytes(eb->e[esdm_ext_es_krng], requested_bits >> 3);

	pr_debug("obtained %u bits of entropy from kernel RNG noise source\n",
		 ent_bits);

	eb->e_bits[esdm_ext_es_krng] = ent_bits;
}

static void esdm_krng_es_state(unsigned char *buf, size_t buflen)
{
	snprintf(buf, buflen,
		 " Available entropy: %u\n"
		 " Entropy Rate per 256 data bits: %u\n",
		 esdm_krng_poolsize(),
		 esdm_krng_entropylevel(256));
}

struct esdm_es_cb esdm_es_krng = {
	.name			= "KernelRNG",
	.get_ent		= esdm_krng_get,
	.curr_entropy		= esdm_krng_entropylevel,
	.max_entropy		= esdm_krng_poolsize,
	.state			= esdm_krng_es_state,
	.reset			= NULL,
	.switch_hash		= NULL,
};
