// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * ESDM interface with the RNG framework of the kernel crypto API
 *
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#include <crypto/esdm.h>
#include <linux/module.h>
#include <crypto/internal/rng.h>

#include "esdm_drng_mgr.h"
#include "esdm_es_aux.h"

static int esdm_kcapi_if_init(struct crypto_tfm *tfm)
{
	return 0;
}

static void esdm_kcapi_if_cleanup(struct crypto_tfm *tfm) { }

static int esdm_kcapi_if_reseed(const u8 *src, unsigned int slen)
{
	int ret;

	if (!slen)
		return 0;

	/* Insert caller-provided data without crediting entropy */
	ret = esdm_pool_insert_aux((u8 *)src, slen, 0);
	if (ret)
		return ret;

	/* Make sure the new data is immediately available to DRNG */
	esdm_drng_force_reseed();

	return 0;
}

static int esdm_kcapi_if_random(struct crypto_rng *tfm,
				const u8 *src, unsigned int slen,
				u8 *rdata, unsigned int dlen)
{
	int ret = esdm_kcapi_if_reseed(src, slen);

	if (!ret)
		esdm_get_random_bytes_full(rdata, dlen);

	return ret;
}

static int esdm_kcapi_if_reset(struct crypto_rng *tfm,
			       const u8 *seed, unsigned int slen)
{
	return esdm_kcapi_if_reseed(seed, slen);
}

static struct rng_alg esdm_alg = {
	.generate		= esdm_kcapi_if_random,
	.seed			= esdm_kcapi_if_reset,
	.seedsize		= 0,
	.base			= {
		.cra_name               = "stdrng",
		.cra_driver_name        = "esdm",
		.cra_priority           = 500,
		.cra_ctxsize            = 0,
		.cra_module             = THIS_MODULE,
		.cra_init               = esdm_kcapi_if_init,
		.cra_exit               = esdm_kcapi_if_cleanup,

	}
};

static int __init esdm_kcapi_if_mod_init(void)
{
	return crypto_register_rng(&esdm_alg);
}

static void __exit esdm_kcapi_if_mod_exit(void)
{
	crypto_unregister_rng(&esdm_alg);
}

module_init(esdm_kcapi_if_mod_init);
module_exit(esdm_kcapi_if_mod_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Entropy Source and DRNG Manager kernel crypto API RNG framework interface");
MODULE_ALIAS_CRYPTO("esdm");
MODULE_ALIAS_CRYPTO("stdrng");
