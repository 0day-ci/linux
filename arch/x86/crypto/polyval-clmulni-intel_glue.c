// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated POLYVAL implementation with Intel PCLMULQDQ-NI
 * instructions. This file contains glue code.
 *
 * Copyright (c) 2007 Nokia Siemens Networks - Mikko Herranen <mh1@iki.fi>
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 * Copyright 2021 Google LLC
 */
/*
 * Glue code based on ghash-clmulni-intel_glue.c.
 *
 * This implementation of POLYVAL uses montgomery multiplication
 * accelerated by PCLMULQDQ-NI to implement the finite field
 * operations.
 *
 */

#include <crypto/algapi.h>
#include <crypto/gf128mul.h>
#include <crypto/internal/hash.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/simd.h>

#define POLYVAL_BLOCK_SIZE	16
#define POLYVAL_DIGEST_SIZE	16
#define NUM_PRECOMPUTE_POWERS	8

struct polyval_ctx {
	be128 key_powers[NUM_PRECOMPUTE_POWERS];
};

struct polyval_desc_ctx {
	u8 buffer[POLYVAL_BLOCK_SIZE];
	u32 bytes;
};

asmlinkage void clmul_polyval_update(const u8 *in, const be128 *keys, size_t
	nblocks, be128 *accumulator);
asmlinkage void clmul_polyval_mul(be128 *op1, const be128 *op2);

static int polyval_init(struct shash_desc *desc)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);

	memset(dctx, 0, sizeof(*dctx));

	return 0;
}

static int polyval_setkey(struct crypto_shash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct polyval_ctx *ctx = crypto_shash_ctx(tfm);
	int i;

	if (keylen != POLYVAL_BLOCK_SIZE)
		return -EINVAL;

	memcpy(&ctx->key_powers[NUM_PRECOMPUTE_POWERS-1], key, sizeof(be128));

	for (i = NUM_PRECOMPUTE_POWERS-2; i >= 0; i--) {
		memcpy(&ctx->key_powers[i], key, sizeof(be128));
		clmul_polyval_mul(&ctx->key_powers[i], &ctx->key_powers[i+1]);
	}

	return 0;
}

static int polyval_update(struct shash_desc *desc,
			 const u8 *src, unsigned int srclen)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	struct polyval_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *dst = dctx->buffer;
	u8 *pos;
	unsigned int nblocks;
	int n;

	kernel_fpu_begin();
	if (dctx->bytes) {
		n = min(srclen, dctx->bytes);
		pos = dst + POLYVAL_BLOCK_SIZE - dctx->bytes;

		dctx->bytes -= n;
		srclen -= n;

		while (n--)
			*pos++ ^= *src++;

		if (!dctx->bytes)
			clmul_polyval_mul((be128 *)dst, &ctx->key_powers[NUM_PRECOMPUTE_POWERS-1]);
	}

	nblocks = srclen/POLYVAL_BLOCK_SIZE;
	clmul_polyval_update(src, ctx->key_powers, nblocks, (be128 *)dst);
	srclen -= nblocks*POLYVAL_BLOCK_SIZE;
	kernel_fpu_end();

	if (srclen) {
		dctx->bytes = POLYVAL_BLOCK_SIZE - srclen;
		src += nblocks*POLYVAL_BLOCK_SIZE;
		pos = dst;
		while (srclen--)
			*pos++ ^= *src++;
	}

	return 0;
}

static int polyval_final(struct shash_desc *desc, u8 *dst)
{
	struct polyval_desc_ctx *dctx = shash_desc_ctx(desc);
	struct polyval_ctx *ctx = crypto_shash_ctx(desc->tfm);
	u8 *buf = dctx->buffer;

	if (dctx->bytes) {
		kernel_fpu_begin();
		clmul_polyval_mul((be128 *)dst, &ctx->key_powers[NUM_PRECOMPUTE_POWERS-1]);
		kernel_fpu_end();
	}

	dctx->bytes = 0;
	memcpy(dst, buf, POLYVAL_BLOCK_SIZE);

	return 0;
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
		.cra_driver_name	= "polyval-pclmulqdqni",
		.cra_priority		= 200,
		.cra_blocksize		= POLYVAL_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct polyval_ctx),
		.cra_module		= THIS_MODULE,
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
MODULE_DESCRIPTION("POLYVAL hash function accelerated by PCLMULQDQ-NI");
MODULE_ALIAS_CRYPTO("polyval");
