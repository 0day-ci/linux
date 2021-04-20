// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RSASSA-PSS signature scheme.
 *
 * Copyright (C) 2021, SUSE
 * Authors: Varad Gautam <varad.gautam@suse.com>
 */

#include <crypto/hash.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <crypto/internal/rsa-common.h>
#include <crypto/public_key.h>

static bool psspad_check_hash_algo(const char *hash_algo)
{
	const char *hash_algos[] = { "sha1", "sha224", "sha256", "sha384", "sha512" };
	bool found = false;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(hash_algos); i++) {
		if (strcmp(hash_algo, hash_algos[i]) == 0) {
			found = true;
			break;
		}
	}

	return found;
}

static int psspad_setup_shash(struct crypto_shash **hash_tfm, struct shash_desc **desc,
			      const char *hash_algo)
{
	if (!psspad_check_hash_algo(hash_algo))
		return -EINVAL;

	*hash_tfm = crypto_alloc_shash(hash_algo, 0, 0);
	if (IS_ERR(*hash_tfm))
		return PTR_ERR(*hash_tfm);

	*desc = kzalloc(crypto_shash_descsize(*hash_tfm) + sizeof(**desc),
			GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	(*desc)->tfm = *hash_tfm;

	return 0;
}

static void psspad_free_shash(struct crypto_shash *hash_tfm, struct shash_desc *desc)
{
	kfree(desc);
	crypto_free_shash(hash_tfm);
}

static int psspad_set_sig_params(struct crypto_akcipher *tfm,
				 const void *sig,
				 unsigned int siglen)
{
	struct akcipher_instance *inst = akcipher_alg_instance(tfm);
	struct rsapad_inst_ctx *ictx = akcipher_instance_ctx(inst);
	const struct public_key_signature *s = sig;

	if (!sig)
		return -EINVAL;

	ictx->salt_len = s->salt_length;
	ictx->mgf_hash_algo = s->mgf_hash_algo;

	return 0;
}

/* MGF1 per RFC8017 B.2.1. */
static int pkcs1_mgf1(u8 *seed, unsigned int seed_len,
		      struct shash_desc *desc,
		      u8 *mask, unsigned int mask_len)
{
	unsigned int pos, h_len, i, c;
	u8 *tmp;
	int ret = 0;

	h_len = crypto_shash_digestsize(desc->tfm);

	pos = i = 0;
	while ((i < (mask_len / h_len) + 1) && pos < mask_len) {
		/* Compute T = T || Hash(mgfSeed || C) into mask at pos. */
		c = cpu_to_be32(i);

		ret = crypto_shash_init(desc);
		if (ret < 0)
			goto out_err;

		ret = crypto_shash_update(desc, seed, seed_len);
		if (ret < 0)
			goto out_err;

		ret = crypto_shash_update(desc, (u8 *) &c, sizeof(c));
		if (ret < 0)
			goto out_err;

		if (mask_len - pos >= h_len) {
			ret = crypto_shash_final(desc, mask + pos);
			pos += h_len;
		} else {
			tmp = kzalloc(h_len, GFP_KERNEL);
			if (!tmp) {
				ret = -ENOMEM;
				goto out_err;
			}
			ret = crypto_shash_final(desc, tmp);
			/* copy the last hash */
			memcpy(mask + pos, tmp, mask_len - pos);
			kfree(tmp);
			pos = mask_len;
		}
		if (ret < 0) {
			goto out_err;
		}

		i++;
	}

out_err:
	return ret;
}

static int psspad_verify_complete(struct akcipher_request *req, int err)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	struct akcipher_instance *inst = akcipher_alg_instance(tfm);
	struct rsapad_inst_ctx *ictx = akcipher_instance_ctx(inst);
	const struct rsa_asn1_template *digest_info = ictx->digest_info;
	struct crypto_shash *hash_tfm = NULL;
	struct shash_desc *desc = NULL;
	struct rsa_mpi_key *pkey = akcipher_tfm_ctx(ctx->child);

	u8 *em, *h, *salt, *maskeddb;
	unsigned int em_len, em_bits, h_len, s_len, maskeddb_len;
	u8 *m_hash, *db_mask, *db, *h_;
	static u8 zeroes[8] = { 0 };
	unsigned int pos;

	if (err)
		goto out;

	err = -EINVAL;
	if (!digest_info)
		goto out;

	em = req_ctx->out_buf;
	em_len = ctx->key_size;
	em_bits = mpi_get_nbits(pkey->n) - 1;
	if ((em_bits & 0x7) == 0) {
		em_len--;
		em++;
	}

	h_len = req->dst_len;
	s_len = ictx->salt_len;

	if (em_len < h_len + s_len + 2)
		goto out;

	if (em[em_len - 1] != 0xbc)
		goto out;

	maskeddb = em;
	maskeddb_len = em_len - h_len - 1;
	h = em + maskeddb_len;

	if (em[0] & ~((u8) 0xff >> (8 * em_len - em_bits)))
		goto out;

	db_mask = kzalloc(maskeddb_len, GFP_KERNEL);
	if (!db_mask) {
		err = -ENOMEM;
		goto out;
	}

	err = psspad_setup_shash(&hash_tfm, &desc, ictx->mgf_hash_algo);
	if (err < 0)
		goto out_db_mask;

	err = pkcs1_mgf1(h, h_len, desc, db_mask, maskeddb_len);
	if (err < 0)
		goto out_shash;

	for (pos = 0; pos < maskeddb_len; pos++)
		maskeddb[pos] ^= db_mask[pos];
	db = maskeddb;

	db[0] &= ((u8) 0xff >> (8 * em_len - em_bits));

	err = -EINVAL;
	for (pos = 0; pos < em_len - h_len - s_len - 2; pos++) {
		if (db[pos] != 0)
			goto out_shash;
	}
	if (db[pos] != 0x01)
		goto out_shash;

	salt = db + (maskeddb_len - s_len);

	m_hash = req_ctx->out_buf + ctx->key_size;
	sg_pcopy_to_buffer(req->src,
			   sg_nents_for_len(req->src, req->src_len + req->dst_len),
			   m_hash,
			   req->dst_len, ctx->key_size);

	if (strcmp(ictx->mgf_hash_algo, digest_info->name) != 0) {
		psspad_free_shash(hash_tfm, desc);
		err = psspad_setup_shash(&hash_tfm, &desc, digest_info->name);
		if (err < 0)
			goto out_db_mask;
	}

	err = crypto_shash_init(desc);
	if (!err)
		err = crypto_shash_update(desc, zeroes, 8);
	if (!err)
		err = crypto_shash_update(desc, m_hash, h_len);
	if (!err)
		err = crypto_shash_finup(desc, salt, s_len, m_hash);
	if (err < 0)
		goto out_shash;

	h_ = m_hash;

	if (memcmp(h_, h, h_len) != 0)
		err = -EKEYREJECTED;

out_shash:
	psspad_free_shash(hash_tfm, desc);
out_db_mask:
	kfree(db_mask);
out:
	kfree_sensitive(req_ctx->out_buf);
	return err;
}

static void psspad_verify_complete_cb(struct crypto_async_request *child_async_req,
				      int err)
{
	rsapad_akcipher_req_complete(child_async_req, err,
				     psspad_verify_complete);
}

static int psspad_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsapad_akciper_req_ctx *req_ctx = akcipher_request_ctx(req);
	int err;

	if (WARN_ON(req->dst) ||
	    WARN_ON(!req->dst_len) ||
	    !ctx->key_size || req->src_len < ctx->key_size)
		return -EINVAL;

	req_ctx->out_buf = kmalloc(ctx->key_size + req->dst_len, GFP_KERNEL);
	if (!req_ctx->out_buf)
		return -ENOMEM;

	rsapad_akcipher_sg_set_buf(req_ctx->out_sg, req_ctx->out_buf,
			    ctx->key_size, NULL);

	/* Reuse input buffer, output to a new buffer */
	rsapad_akcipher_setup_child(req, req->src, req_ctx->out_sg,
				    req->src_len, ctx->key_size,
				    psspad_verify_complete_cb);

	err = crypto_akcipher_encrypt(&req_ctx->child_req);
	if (err != -EINPROGRESS && err != -EBUSY)
		return psspad_verify_complete(req, err);

	return err;
}

static int psspad_s_e_d(struct akcipher_request *req)
{
	return -EOPNOTSUPP;
}

static struct akcipher_alg psspad_alg = {
	.init = rsapad_akcipher_init_tfm,
	.exit = rsapad_akcipher_exit_tfm,

	.encrypt = psspad_s_e_d,
	.decrypt = psspad_s_e_d,
	.sign = psspad_s_e_d,
	.verify = psspad_verify,
	.set_pub_key = rsapad_set_pub_key,
	.set_priv_key = rsapad_set_priv_key,
	.max_size = rsapad_get_max_size,
	.set_sig_params = psspad_set_sig_params
};

static int psspad_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	return rsapad_akcipher_create(tmpl, tb, &psspad_alg);
}

struct crypto_template rsa_psspad_tmpl = {
	.name = "psspad",
	.create = psspad_create,
	.module = THIS_MODULE,
};
