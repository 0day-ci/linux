// SPDX-License-Identifier: GPL-2.0+
/*
 * EDDSA generic algorithm.
 *
 * Copyright (c) 2021 Hongbo Li <herberthbli@tencent.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/mpi.h>
#include <linux/module.h>
#include <linux/oid_registry.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <crypto/ecdh.h>
#include <crypto/curve25519.h>
#include <crypto/internal/akcipher.h>
#include "ec_mpi.h"

struct eddsa_ctx {
	enum OID algo_oid;
	struct mpi_ec_ctx ec_ctx;
};

static MPI p58;
static MPI seven;
static MPI m1;

static const struct ecc_domain_parms ed25519_domain_params = {
	.desc = "ed25519",
	.nbits = 256,
	.fips = 0,
	.model = MPI_EC_EDWARDS,
	.dialect = ECC_DIALECT_ED25519,
	.p = "0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFED",
	.a = "-0x01",
	.b = "-0x2DFC9311D490018C7338BF8688861767FF8FF5B2BEBE27548A14B235ECA6874A",
	.n = "0x1000000000000000000000000000000014DEF9DEA2F79CD65812631A5CF5D3ED",
	.g_x = "0x216936D3CD6E53FEC0A4E231FDD6DC5C692CC7609525A7B2C9562D608F25D51A",
	.g_y = "0x6666666666666666666666666666666666666666666666666666666666666658",
	.h = 8,
};

static void reverse_buffer(u8 *buffer, u32 length)
{
	u32 tmp, i;

	for (i = 0; i < length / 2; i++) {
		tmp = buffer[i];
		buffer[i] = buffer[length - 1 - i];
		buffer[length - 1 - i] = tmp;
	}
}

static int eddsa_encode_x_y(MPI x, MPI y, u8 *buf, u32 key_size)
{
	memcpy(buf, y->d, key_size);
	if (mpi_test_bit(x, 0))
		buf[key_size - 1] |= 0x80;

	return 0;
}

static int ecc_eddsa_encodepoint(MPI_POINT point, struct mpi_ec_ctx *ec,
				 MPI x, MPI y, u8 *buf, u32 key_size)
{
	if (mpi_ec_get_affine(x, y, point, ec))
		return -EINVAL;

	return eddsa_encode_x_y(x, y, buf, key_size);
}

/* Recover X from Y and SIGN (which actually is a parity bit).  */
static int eddsa_recover_x(MPI x, MPI y, int sign, struct mpi_ec_ctx *ec)
{
	MPI u, v, v3, t;
	int ret = 0;

	if (ec->dialect != ECC_DIALECT_ED25519)
		return -ENOPKG;

	u = mpi_new(0);
	v = mpi_new(0);
	v3 = mpi_new(0);
	t = mpi_new(0);

	/* Compute u and v */
	/* u = y^2 */
	mpi_mulm(u, y, y, ec->p);
	/* v = b*y^2 */
	mpi_mulm(v, ec->b, u, ec->p);
	/* u = y^2-1 */
	mpi_sub_ui(u, u, 1);
	/* v = b*y^2+1 */
	mpi_add_ui(v, v, 1);

	/* Compute sqrt(u/v) */
	/* v3 = v^3 */
	mpi_powm(v3, v, mpi_const(MPI_C_THREE), ec->p);
	/* t = v3 * v3 * u * v = u * v^7 */
	mpi_powm(t, v, seven, ec->p);
	mpi_mulm(t, t, u, ec->p);
	/* t = t^((p-5)/8) = (u * v^7)^((p-5)/8)  */
	mpi_powm(t, t, p58, ec->p);
	/* x = t * u * v^3 = (u * v^3) * (u * v^7)^((p-5)/8) */
	mpi_mulm(t, t, u, ec->p);
	mpi_mulm(x, t, v3, ec->p);

	/* Adjust if needed. */
	/* t = v * x^2 */
	mpi_mulm(t, x, x, ec->p);
	mpi_mulm(t, t, v, ec->p);
	/* -t == u ? x = x * sqrt(-1) */
	mpi_sub(t, ec->p, t);
	if (!mpi_cmp(t, u)) {
		mpi_mulm(x, x, m1, ec->p);
		/* t = v * x^2 */
		mpi_mulm(t, x, x, ec->p);
		mpi_mulm(t, t, v, ec->p);
		/* -t == u ? x = x * sqrt(-1) */
		mpi_sub(t, ec->p, t);
		if (!mpi_cmp(t, u))
			ret = -EINVAL;
	}

	/* Choose the desired square root according to parity */
	if (mpi_test_bit(x, 0) != !!sign)
		mpi_sub(x, ec->p, x);

	mpi_free(t);
	mpi_free(v3);
	mpi_free(v);
	mpi_free(u);

	return ret;
}

static int ecc_eddsa_decodepoint(const u8 *pk, int key_size,
				 struct mpi_ec_ctx *ec, MPI_POINT result)
{
	MPI y;
	u8 *rawmpi;
	int sign, ret = 0;

	rawmpi = kmalloc(key_size, GFP_KERNEL);
	if (!rawmpi)
		return -ENOMEM;
	memcpy(rawmpi, pk, key_size);
	reverse_buffer(rawmpi, key_size);

	sign = !!(rawmpi[0] & 0x80);
	rawmpi[0] &= 0x7f;

	y = mpi_read_raw_data(rawmpi, key_size);
	if (!y) {
		ret = -EINVAL;
		goto out;
	}

	mpi_normalize(y);
	mpi_set(result->y, y);
	mpi_free(y);

	ret = eddsa_recover_x(result->x, result->y, sign, ec);
	mpi_set_ui(result->z, 1);
out:
	kfree(rawmpi);
	return ret;
}

static int eddsa_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct eddsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct mpi_ec_ctx *ec = &ctx->ec_ctx;
	struct gcry_mpi_point sb, ka;
	MPI s = NULL;
	MPI k = NULL;
	u8 sig[CURVE25519_KEY_SIZE * 2],digest[SHA512_DIGEST_SIZE];
	u8 *buf;
	u32 key_size;
	int ret = 0;

	if (ctx->algo_oid != OID_ed25519)
		return -ENOPKG;

	key_size = CURVE25519_KEY_SIZE;

	if (!ec->Q || req->src_len != key_size * 2)
		return -EINVAL;

	sg_copy_to_buffer(req->src, sg_nents_for_len(req->src, req->src_len),
			  sig, req->src_len);

	sg_pcopy_to_buffer(req->src,
			   sg_nents_for_len(req->src,
					    req->src_len + req->dst_len),
			   digest, req->dst_len, req->src_len);

	reverse_buffer(digest, SHA512_DIGEST_SIZE);
	k = mpi_read_raw_data(digest, SHA512_DIGEST_SIZE);

	reverse_buffer(sig + key_size, key_size);
	s = mpi_read_raw_data(sig + key_size, key_size);

	mpi_point_init(&sb);
	mpi_point_init(&ka);

	mpi_ec_mul_point(&sb, s, ec->G, ec);
	mpi_ec_mul_point(&ka, k, ec->Q, ec);
	mpi_sub(ka.x, ec->p, ka.x);
	mpi_ec_add_points(&sb, &sb, &ka, ec);

	buf = kmalloc(key_size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	ret = ecc_eddsa_encodepoint(&sb, ec, s, k, buf, key_size);
	if (ret)
		goto out;

	if (memcmp(buf, sig, key_size))
		ret = -EKEYREJECTED;

out:
	mpi_point_free_parts(&sb);
	mpi_point_free_parts(&ka);
	mpi_free(k);
	mpi_free(s);
	kfree(buf);
	return ret;
}

static int eddsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen)
{
	struct eddsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct mpi_ec_ctx *ec = &ctx->ec_ctx;
	const u8 *pk = key;

	if (ctx->algo_oid != OID_ed25519)
		return -ENOPKG;

	if (keylen != CURVE25519_KEY_SIZE)
		return -EINVAL;

	return ecc_eddsa_decodepoint(pk, keylen, ec, ec->Q);
}

static u32 eddsa_max_size(struct crypto_akcipher *tfm)
{
	struct eddsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	if (ctx->algo_oid == OID_ed25519)
		return CURVE25519_KEY_SIZE;

	return 0;
}

static int eddsa_25519_init_tfm(struct crypto_akcipher *tfm)
{
	struct eddsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	ctx->algo_oid = OID_ed25519;
	p58 = mpi_scanval("0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFD");
	if (!p58)
		return -ENOMEM;

	m1 = mpi_scanval("2B8324804FC1DF0B2B4D00993DFBD7A72F431806AD2FE478C4EE1B274A0EA0B0");
	if (!m1)
		return -ENOMEM;

	seven = mpi_set_ui(NULL, 7);

	return ec_mpi_ctx_init(&ctx->ec_ctx, &ed25519_domain_params);
}

static void eddsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct eddsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	ec_mpi_ctx_deinit(&ctx->ec_ctx);
	mpi_free(p58);
	mpi_free(seven);
	mpi_free(m1);
}


static struct akcipher_alg eddsa_25519 = {
	.verify = eddsa_verify,
	.set_pub_key = eddsa_set_pub_key,
	.max_size = eddsa_max_size,
	.init = eddsa_25519_init_tfm,
	.exit = eddsa_exit_tfm,
	.base = {
		.cra_name = "eddsa-25519",
		.cra_driver_name = "eddsa-25519-generic",
		.cra_priority = 100,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct eddsa_ctx),
	},
};

static int eddsa_mod_init(void)
{
	return crypto_register_akcipher(&eddsa_25519);
}

static void eddsa_mod_exit(void)
{
	crypto_unregister_akcipher(&eddsa_25519);
}

module_init(eddsa_mod_init);
module_exit(eddsa_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Hongbo Li <herberthbli@tencent.com>");
MODULE_ALIAS_CRYPTO("eddsa");
MODULE_ALIAS_CRYPTO("eddsa-generic");
MODULE_DESCRIPTION("EDDSA generic algorithm");
