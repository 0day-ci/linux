// SPDX-License-Identifier: GPL-2.0-only
/*
 * POLYVAL: hash function for HCTR2.
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 * Copyright 2021 Google LLC
 */

/*
 * Code based on crypto/ghash-generic.c
 *
 * POLYVAL is a keyed hash function similar to GHASH. POLYVAL uses a
 * different modulus for finite field multiplication which makes hardware
 * accelerated implementations on little-endian machines faster.
 *
 * Like GHASH, POLYVAL is not a cryptographic hash function and should
 * not be used outside of crypto modes explicitly designed to use POLYVAL.
 *
 */

#include <asm/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/gf128mul.h>
#include <crypto/polyval.h>
#include <crypto/internal/hash.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct polyval_tfm_ctx {
	struct gf128mul_4k *gf128;
};

static int polyval_init(struct shash_desc *desc)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static void reverse_block(u8 block[POLYVAL_BLOCK_SIZE])
{
	u64 *p1 = (u64 *)block;
	u64 *p2 = (u64 *)&block[8];
	u64 a = get_unaligned(p1);
	u64 b = get_unaligned(p2);

	put_unaligned(swab64(a), p2);
	put_unaligned(swab64(b), p1);
}

static int polyval_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct polyval_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	be128 k;

	if (keylen != POLYVAL_BLOCK_SIZE)
		return -EINVAL;

	gf128mul_free_4k(ctx->gf128);

	BUILD_BUG_ON(sizeof(k) != POLYVAL_BLOCK_SIZE);
	memcpy(&k, key, POLYVAL_BLOCK_SIZE); /* avoid violating alignment rules */

	reverse_block((u8 *)&k);
	gf128mul_x_lle(&k, &k);

	ctx->gf128 = gf128mul_init_4k_lle(&k);
	memzero_explicit(&k, POLYVAL_BLOCK_SIZE);

	if (!ctx->gf128)
		return -ENOMEM;

	return 0;
}

static int polyval_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *dst = dctx->buffer;
	u8 *pos;
	u8 tmp[POLYVAL_BLOCK_SIZE];
	int n;

	if (dctx->bytes) {
		n = min(srclen, dctx->bytes);
		pos = dst + dctx->bytes - 1;

		dctx->bytes -= n;
		srclen -= n;

		while (n--)
			*pos-- ^= *src++;

		if (!dctx->bytes)
			gf128mul_4k_lle((be128 *)dst, ctx->gf128);
	}

	while (srclen >= POLYVAL_BLOCK_SIZE) {
		memcpy(tmp, src, POLYVAL_BLOCK_SIZE);
		reverse_block(tmp);
		crypto_xor(dst, tmp, POLYVAL_BLOCK_SIZE);
		gf128mul_4k_lle((be128 *)dst, ctx->gf128);
		src += POLYVAL_BLOCK_SIZE;
		srclen -= POLYVAL_BLOCK_SIZE;
	}

	if (srclen) {
		dctx->bytes = POLYVAL_BLOCK_SIZE - srclen;
		pos = dst + POLYVAL_BLOCK_SIZE - 1;
		while (srclen--)
			*pos-- ^= *src++;
	}

	return 0;
}

static int polyval_final(struct shash_desc *desc, u8 *dst)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	const struct polyval_tfm_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *buf = dctx->buffer;

	if (dctx->bytes)
		gf128mul_4k_lle((be128 *)dst, ctx->gf128);
	dctx->bytes = 0;

	reverse_block(buf);
	memcpy(dst, buf, POLYVAL_BLOCK_SIZE);

	return 0;
}

static void polyval_exit_tfm(struct crypto_tfm *tfm)
{
	struct polyval_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	gf128mul_free_4k(ctx->gf128);
}

static struct shash_alg polyval_alg = {
	.digestsize	= POLYVAL_DIGEST_SIZE,
	.init		= polyval_init,
	.update		= polyval_update,
	.final		= polyval_final,
	.setkey		= polyval_setkey,
	.descsize	= sizeof(struct polyval_desc_ctx),
	.base		= {
		.cra_name		= "polyval",
		.cra_driver_name	= "polyval-generic",
		.cra_priority		= 100,
		.cra_blocksize		= POLYVAL_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct polyval_tfm_ctx),
		.cra_module		= THIS_MODULE,
		.cra_exit		= polyval_exit_tfm,
	},
};

static int __init polyval_mod_init(void)
{
	return crypto_register_shash(&polyval_alg);
}

static void __exit polyval_mod_exit(void)
{
	crypto_unregister_shash(&polyval_alg);
}

subsys_initcall(polyval_mod_init);
module_exit(polyval_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("POLYVAL hash function");
MODULE_ALIAS_CRYPTO("polyval");
MODULE_ALIAS_CRYPTO("polyval-generic");
