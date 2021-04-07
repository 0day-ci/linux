// SPDX-License-Identifier: GPL-2.0+
/*
 * RSA PSS padding templates.
 *
 * Copyright (c) 2021 Hongbo Li <herberthbli@tencent.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <crypto/hash.h>
#include <crypto/internal/rsa.h>
#include <crypto/internal/akcipher.h>

struct psspad_inst_ctx {
	struct crypto_akcipher_spawn spawn;
};

struct psspad_request {
	struct scatterlist out_sg[1];
	uint8_t *out_buf;
	struct akcipher_request child_req;
};

static const u8 *psspad_unpack(void *dst, const void *src, size_t sz)
{
	memcpy(dst, src, sz);
	return src + sz;
}

static int psspad_set_pub_key(struct crypto_akcipher *tfm, const void *key,
			      unsigned int keylen)
{
	struct rsa_pss_ctx *ctx = akcipher_tfm_ctx(tfm);
	const u8 *ptr;
	u32 algo, paramlen;
	int err;

	ctx->key_size = 0;

	err = crypto_akcipher_set_pub_key(ctx->child, key, keylen);
	if (err)
		return err;

	/* Find out new modulus size from rsa implementation */
	err = crypto_akcipher_maxsize(ctx->child);
	if (err > PAGE_SIZE)
		return -EOPNOTSUPP;

	ctx->key_size = err;

	ptr = key + keylen;
	ptr = psspad_unpack(&algo, ptr, sizeof(algo));
	ptr = psspad_unpack(&paramlen, ptr, sizeof(paramlen));
	err = rsa_parse_pss_params(ctx, ptr, paramlen);
	if (err < 0)
		return err;

	if (!ctx->hash_algo)
		ctx->hash_algo = "sha1";
	if (!ctx->mgf_algo)
		ctx->mgf_algo = "mgf1";
	if (!ctx->mgf_hash_algo)
		ctx->mgf_hash_algo = "sha1";
	if (!ctx->salt_len)
		ctx->salt_len = RSA_PSS_DEFAULT_SALT_LEN;

	return 0;
}

static int psspad_mgf1(const char *hash_algo, u8 *seed, u32 seed_len, u8 *mask,
		       u32 masklen)
{
	struct crypto_shash *tfm = NULL;
	u32 hlen, cnt, tlen;
	u8 c[4], digest[RSA_MAX_DIGEST_SIZE], buf[RSA_MAX_DIGEST_SIZE + 4];
	int i, err = 0;
	SHASH_DESC_ON_STACK(desc, tfm);

	tfm = crypto_alloc_shash(hash_algo, 0, 0);
	if (IS_ERR(tfm)) {
		err = PTR_ERR(tfm);
		return err;
	}
	desc->tfm = tfm;
	hlen = crypto_shash_digestsize(tfm);
	cnt = DIV_ROUND_UP(masklen, hlen);
	tlen = 0;
	for (i = 0; i < cnt; i++) {
		/* C = I2OSP (counter, 4) */
		c[0] = (i >> 24) & 0xff;
		c[1] = (i >> 16) & 0xff;
		c[2] = (i >> 8) & 0xff;
		c[3] = i & 0xff;

		memcpy(buf, seed, seed_len);
		memcpy(buf + seed_len, c, 4);
		err = crypto_shash_digest(desc, buf,
					  seed_len + 4, digest);
		if (err < 0)
			goto free;

		/* T = T || Hash(mgfSeed || C) */
		tlen = i * hlen;
		if (i == cnt - 1)
			memcpy(mask + tlen, digest, masklen - tlen);
		else
			memcpy(mask + tlen, digest, hlen);
	}
free:
	crypto_free_shash(tfm);
	return err;
}

/* EMSA-PSS-VERIFY (M, EM, emBits) */
static int psspad_verify_complete(struct akcipher_request *req, int err)
{
	struct crypto_akcipher *ak_tfm = crypto_akcipher_reqtfm(req);
	struct rsa_pss_ctx *ctx = akcipher_tfm_ctx(ak_tfm);
	struct psspad_request *req_ctx = akcipher_request_ctx(req);
	struct crypto_akcipher *rsa_tfm;
	struct rsa_mpi_key *mpi_key;
	struct crypto_shash *tfm = NULL;
	u32 i, hlen, slen, modbits, embits, emlen, masklen, buflen;
	u8 *em, *h, *maskeddb, *dbmask, *db, *salt;
	u8 mhash[RSA_MAX_DIGEST_SIZE], digest[RSA_MAX_DIGEST_SIZE];
	u8 *buf = NULL;
	SHASH_DESC_ON_STACK(desc, tfm);

	if (err)
		goto free;

	tfm = crypto_alloc_shash(ctx->hash_algo, 0, 0);
	if (IS_ERR(tfm)) {
		err = PTR_ERR(tfm);
		tfm = NULL;
		goto free;
	}
	desc->tfm = tfm;
	hlen = crypto_shash_digestsize(tfm);

	/* mhash */
	sg_pcopy_to_buffer(req->src,
			   sg_nents_for_len(req->src,
					    req->src_len + req->dst_len),
			   mhash, hlen, req->src_len);

	err = -EINVAL;

	/* section 8.1.2. emLen = \ceil ((modBits - 1)/8) */
	rsa_tfm = crypto_akcipher_reqtfm(&req_ctx->child_req);
	mpi_key = akcipher_tfm_ctx(rsa_tfm);
	modbits = mpi_get_nbits(mpi_key->n);
	embits = modbits - 1;
	emlen = DIV_ROUND_UP(embits, 8);

	/* 3. If emLen < hLen + sLen + 2, output "inconsistent" and stop. */
	slen = ctx->salt_len;
	if (emlen < hlen + slen + 2)
		goto free;

	/* 4. If the rightmost octet of EM does not have hexadecimal value
	 * 0xbc, output "inconsistent" and stop.
	 */
	em = req_ctx->out_buf;
	if (em[emlen - 1] != 0xbc)
		goto free;


	/* 5. Let maskedDB be the leftmost emLen - hLen - 1 octets of EM,
	 * and let H be the next hLen octets.
	 */
	maskeddb = em;
	masklen = emlen - hlen - 1;
	h = em + masklen;

	/* 6. If the leftmost 8emLen - emBits bits of the leftmost octet in
	 * maskedDB are not all equal to zero, output "inconsistent" and
	 * stop.
	 */
	if (maskeddb[0] & ~(0xff >> (8 * emlen - embits)))
		goto free;

	/* 7. Let dbMask = MGF(H, emLen - hLen - 1). */
	buflen = max_t(u32, masklen, 8 + hlen + slen);
	buf = kmalloc(buflen, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto free;
	}
	dbmask = buf;
	err = psspad_mgf1(ctx->mgf_hash_algo, h, hlen, dbmask, masklen);
	if (err)
		goto free;

	/* 8. Let DB = maskedDB \xor dbMask. */
	db = maskeddb;
	for (i = 0; i < masklen; i++)
		db[i] = maskeddb[i] ^ dbmask[i];

	/* 9. Set the leftmost 8emLen - emBits bits of the leftmost octet
	 * in DB to zero.
	 */
	db[0] &= 0xff >> (8 * emlen - embits);

	/* 10. If the emLen - hLen - sLen - 2 leftmost octets of DB are not
	 * zero or if the octet at position emLen - hLen - sLen - 1 (the
	 * leftmost position is "position 1") does not have hexadecimal
	 * value 0x01, output "inconsistent" and stop.
	 */
	for (i = 0; i < emlen - hlen - slen - 2; i++) {
		if (db[i]) {
			err = -EINVAL;
			goto free;
		}
	}
	if (db[i] != 1)
		goto free;

	/* 11. Let salt be the last sLen octets of DB. */
	salt = db + masklen - slen;

	/* 12. M' = (0x)00 00 00 00 00 00 00 00 || mHash || salt ; */
	memset(buf, 0, 8);
	memcpy(buf + 8, mhash, hlen);
	memcpy(buf + 8 + hlen, salt, slen);

	/* 13. Let H' = Hash(M'), an octet string of length hLen. */
	err = crypto_shash_digest(desc, buf, 8 + hlen + slen, digest);
	if (err < 0)
		goto free;

	/* 14. If H = H', output "consistent". Otherwise, output
	 * "inconsistent".
	 */
	if (memcmp(h, digest, hlen))
		err = -EKEYREJECTED;

free:
	if (tfm)
		crypto_free_shash(tfm);
	kfree_sensitive(req_ctx->out_buf);
	kfree(buf);
	return err;
}

static void psspad_verify_complete_cb(
	struct crypto_async_request *child_async_req, int err)
{
	struct akcipher_request *req = child_async_req->data;
	struct crypto_async_request async_req;

	if (err == -EINPROGRESS)
		return;

	async_req.data = req->base.data;
	async_req.tfm = crypto_akcipher_tfm(crypto_akcipher_reqtfm(req));
	async_req.flags = child_async_req->flags;
	req->base.complete(&async_req, psspad_verify_complete(req, err));
}

static int psspad_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsa_pss_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct psspad_request *req_ctx = akcipher_request_ctx(req);
	int err;

	if (WARN_ON(req->dst) ||
	    WARN_ON(!req->dst_len) ||
	    !ctx->key_size || req->src_len < ctx->key_size)
		return -EINVAL;

	req_ctx->out_buf = kmalloc(ctx->key_size + req->dst_len, GFP_KERNEL);
	if (!req_ctx->out_buf)
		return -ENOMEM;

	sg_init_table(req_ctx->out_sg, 1);
	sg_set_buf(req_ctx->out_sg, req_ctx->out_buf, ctx->key_size);

	akcipher_request_set_tfm(&req_ctx->child_req, ctx->child);
	akcipher_request_set_callback(&req_ctx->child_req, req->base.flags,
				      psspad_verify_complete_cb, req);

	/* Reuse input buffer, output to a new buffer */
	akcipher_request_set_crypt(&req_ctx->child_req, req->src,
				   req_ctx->out_sg, req->src_len,
				   ctx->key_size);

	err = crypto_akcipher_encrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return psspad_verify_complete(req, err);

	return err;
}

static unsigned int psspad_get_max_size(struct crypto_akcipher *tfm)
{
	struct rsa_pss_ctx *ctx = akcipher_tfm_ctx(tfm);

	return ctx->key_size;
}

static int psspad_init_tfm(struct crypto_akcipher *tfm)
{
	struct akcipher_instance *inst = akcipher_alg_instance(tfm);
	struct psspad_inst_ctx *ictx = akcipher_instance_ctx(inst);
	struct rsa_pss_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct crypto_akcipher *child_tfm;

	child_tfm = crypto_spawn_akcipher(&ictx->spawn);
	if (IS_ERR(child_tfm))
		return PTR_ERR(child_tfm);

	ctx->child = child_tfm;
	return 0;
}

static void psspad_exit_tfm(struct crypto_akcipher *tfm)
{
	struct rsa_pss_ctx *ctx = akcipher_tfm_ctx(tfm);

	crypto_free_akcipher(ctx->child);
}

static void psspad_free(struct akcipher_instance *inst)
{
	struct psspad_inst_ctx *ctx = akcipher_instance_ctx(inst);
	struct crypto_akcipher_spawn *spawn = &ctx->spawn;

	crypto_drop_akcipher(spawn);
	kfree(inst);
}

static int psspad_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	u32 mask;
	struct akcipher_instance *inst;
	struct psspad_inst_ctx *ctx;
	struct akcipher_alg *rsa_alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AKCIPHER, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = akcipher_instance_ctx(inst);

	err = crypto_grab_akcipher(&ctx->spawn, akcipher_crypto_instance(inst),
				   crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	rsa_alg = crypto_spawn_akcipher_alg(&ctx->spawn);

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name,
		     CRYPTO_MAX_ALG_NAME, "psspad(%s)",
		     rsa_alg->base.cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	if (snprintf(inst->alg.base.cra_driver_name,
		     CRYPTO_MAX_ALG_NAME, "psspad(%s)",
		     rsa_alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = rsa_alg->base.cra_priority;
	inst->alg.base.cra_ctxsize = sizeof(struct rsa_pss_ctx);

	inst->alg.init = psspad_init_tfm;
	inst->alg.exit = psspad_exit_tfm;
	inst->alg.verify = psspad_verify;
	inst->alg.set_pub_key = psspad_set_pub_key;
	inst->alg.max_size = psspad_get_max_size;
	inst->alg.reqsize = sizeof(struct psspad_request) + rsa_alg->reqsize;

	inst->free = psspad_free;

	err = akcipher_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		psspad_free(inst);
	}
	return err;
}

struct crypto_template rsa_psspad_tmpl = {
	.name = "psspad",
	.create = psspad_create,
	.module = THIS_MODULE,
};
