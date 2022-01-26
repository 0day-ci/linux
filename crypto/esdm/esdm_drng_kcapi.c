// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Backend for the ESDM providing the cryptographic primitives using the
 * kernel crypto API.
 *
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/esdm.h>
#include <crypto/hash.h>
#include <crypto/rng.h>
#include <linux/init.h>
#include <linux/module.h>

#include "esdm_drng_kcapi.h"

static char *drng_name =
#ifdef CONFIG_CRYPTO_DRBG_CTR
	/* CTR_DRBG with AES-256 using derivation function */
	"drbg_nopr_ctr_aes256";
#elif defined CONFIG_CRYPTO_DRBG_HMAC
	/* HMAC_DRBG with SHA-512 */
	"drbg_nopr_hmac_sha512";
#elif defined CONFIG_CRYPTO_DRBG_HASH
	/* Hash_DRBG with SHA-512 using derivation function */
	"drbg_nopr_sha512";
#else
	NULL;
#endif
module_param(drng_name, charp, 0444);
MODULE_PARM_DESC(drng_name, "Kernel crypto API name of DRNG");

static char *seed_hash = NULL;
module_param(seed_hash, charp, 0444);
MODULE_PARM_DESC(seed_hash,
		 "Kernel crypto API name of hash with output size equal to seedsize of DRNG to bring seed string to the size required by the DRNG");

struct esdm_drng_info {
	struct crypto_rng *kcapi_rng;
	struct crypto_shash *hash_tfm;
};

static int esdm_kcapi_drng_seed_helper(void *drng, const u8 *inbuf,
				       u32 inbuflen)
{
	struct esdm_drng_info *esdm_drng_info = (struct esdm_drng_info *)drng;
	struct crypto_rng *kcapi_rng = esdm_drng_info->kcapi_rng;
	struct crypto_shash *hash_tfm = esdm_drng_info->hash_tfm;
	SHASH_DESC_ON_STACK(shash, hash_tfm);
	u32 digestsize;
	u8 digest[HASH_MAX_DIGESTSIZE] __aligned(8);
	int ret;

	if (!hash_tfm)
		return crypto_rng_reset(kcapi_rng, inbuf, inbuflen);

	shash->tfm = hash_tfm;
	digestsize = crypto_shash_digestsize(hash_tfm);

	ret = crypto_shash_digest(shash, inbuf, inbuflen, digest);
	shash_desc_zero(shash);
	if (ret)
		return ret;

	ret = crypto_rng_reset(kcapi_rng, digest, digestsize);
	if (ret)
		return ret;

	memzero_explicit(digest, digestsize);
	return 0;
}

static int esdm_kcapi_drng_generate_helper(void *drng, u8 *outbuf,
					   u32 outbuflen)
{
	struct esdm_drng_info *esdm_drng_info = (struct esdm_drng_info *)drng;
	struct crypto_rng *kcapi_rng = esdm_drng_info->kcapi_rng;
	int ret = crypto_rng_get_bytes(kcapi_rng, outbuf, outbuflen);

	if (ret < 0)
		return ret;

	return outbuflen;
}

static void *esdm_kcapi_drng_alloc(u32 sec_strength)
{
	struct esdm_drng_info *esdm_drng_info;
	struct crypto_rng *kcapi_rng;
	u32 time = random_get_entropy();
	int seedsize, rv;
	void *ret =  ERR_PTR(-ENOMEM);

	if (!drng_name) {
		pr_err("DRNG name missing\n");
		return ERR_PTR(-EINVAL);
	}

	if (!memcmp(drng_name, "stdrng", 6) ||
	    !memcmp(drng_name, "jitterentropy_rng", 17)) {
		pr_err("Refusing to load the requested random number generator\n");
		return ERR_PTR(-EINVAL);
	}

	esdm_drng_info = kzalloc(sizeof(*esdm_drng_info), GFP_KERNEL);
	if (!esdm_drng_info)
		return ERR_PTR(-ENOMEM);

	kcapi_rng = crypto_alloc_rng(drng_name, 0, 0);
	if (IS_ERR(kcapi_rng)) {
		pr_err("DRNG %s cannot be allocated\n", drng_name);
		ret = ERR_CAST(kcapi_rng);
		goto free;
	}

	esdm_drng_info->kcapi_rng = kcapi_rng;

	seedsize = crypto_rng_seedsize(kcapi_rng);
	if (seedsize) {
		struct crypto_shash *hash_tfm;

		if (!seed_hash) {
			switch (seedsize) {
			case 32:
				seed_hash = "sha256";
				break;
			case 48:
				seed_hash = "sha384";
				break;
			case 64:
				seed_hash = "sha512";
				break;
			default:
				pr_err("Seed size %d cannot be processed\n",
				       seedsize);
				goto dealloc;
			}
		}

		hash_tfm = crypto_alloc_shash(seed_hash, 0, 0);
		if (IS_ERR(hash_tfm)) {
			ret = ERR_CAST(hash_tfm);
			goto dealloc;
		}

		if (seedsize != crypto_shash_digestsize(hash_tfm)) {
			pr_err("Seed hash output size not equal to DRNG seed size\n");
			crypto_free_shash(hash_tfm);
			ret = ERR_PTR(-EINVAL);
			goto dealloc;
		}

		esdm_drng_info->hash_tfm = hash_tfm;

		pr_info("Seed hash %s allocated\n", seed_hash);
	}

	rv = esdm_kcapi_drng_seed_helper(esdm_drng_info, (u8 *)(&time),
					 sizeof(time));
	if (rv) {
		ret = ERR_PTR(rv);
		goto dealloc;
	}

	pr_info("Kernel crypto API DRNG %s allocated\n", drng_name);

	return esdm_drng_info;

dealloc:
	crypto_free_rng(kcapi_rng);
free:
	kfree(esdm_drng_info);
	return ret;
}

static void esdm_kcapi_drng_dealloc(void *drng)
{
	struct esdm_drng_info *esdm_drng_info = (struct esdm_drng_info *)drng;
	struct crypto_rng *kcapi_rng = esdm_drng_info->kcapi_rng;

	crypto_free_rng(kcapi_rng);
	if (esdm_drng_info->hash_tfm)
		crypto_free_shash(esdm_drng_info->hash_tfm);
	kfree(esdm_drng_info);
	pr_info("DRNG %s deallocated\n", drng_name);
}

static const char *esdm_kcapi_drng_name(void)
{
	return drng_name;
}

const struct esdm_drng_cb esdm_kcapi_drng_cb = {
	.drng_name	= esdm_kcapi_drng_name,
	.drng_alloc	= esdm_kcapi_drng_alloc,
	.drng_dealloc	= esdm_kcapi_drng_dealloc,
	.drng_seed	= esdm_kcapi_drng_seed_helper,
	.drng_generate	= esdm_kcapi_drng_generate_helper,
};
