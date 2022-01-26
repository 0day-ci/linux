// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Backend for the ESDM providing the SHA-256 implementation that can be used
 * without the kernel crypto API available including during early boot and in
 * atomic contexts.
 *
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/esdm.h>
#include <crypto/sha2.h>

#include "esdm_sha.h"

static u32 esdm_sha256_hash_digestsize(void *hash)
{
	return SHA256_DIGEST_SIZE;
}

static int esdm_sha256_hash_init(struct shash_desc *shash, void *hash)
{
	/*
	 * We do not need a TFM - we only need sufficient space for
	 * struct sha256_state on the stack.
	 */
	sha256_init(shash_desc_ctx(shash));
	return 0;
}

static int esdm_sha256_hash_update(struct shash_desc *shash,
				   const u8 *inbuf, u32 inbuflen)
{
	sha256_update(shash_desc_ctx(shash), inbuf, inbuflen);
	return 0;
}

static int esdm_sha256_hash_final(struct shash_desc *shash, u8 *digest)
{
	sha256_final(shash_desc_ctx(shash), digest);
	return 0;
}

static const char *esdm_sha256_hash_name(void)
{
	return "SHA-256";
}

static void esdm_sha256_hash_desc_zero(struct shash_desc *shash)
{
	memzero_explicit(shash_desc_ctx(shash), sizeof(struct sha256_state));
}

static void *esdm_sha256_hash_alloc(void)
{
	pr_info("Hash %s allocated\n", esdm_sha256_hash_name());
	return NULL;
}

static void esdm_sha256_hash_dealloc(void *hash) { }

const struct esdm_hash_cb esdm_sha_hash_cb = {
	.hash_name		= esdm_sha256_hash_name,
	.hash_alloc		= esdm_sha256_hash_alloc,
	.hash_dealloc		= esdm_sha256_hash_dealloc,
	.hash_digestsize	= esdm_sha256_hash_digestsize,
	.hash_init		= esdm_sha256_hash_init,
	.hash_update		= esdm_sha256_hash_update,
	.hash_final		= esdm_sha256_hash_final,
	.hash_desc_zero		= esdm_sha256_hash_desc_zero,
};
