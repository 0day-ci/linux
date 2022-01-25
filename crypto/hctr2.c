// SPDX-License-Identifier: GPL-2.0
/*
 * HCTR2 length-preserving encryption mode
 *
 * Copyright 2021 Google LLC
 */


/*
 * HCTR2 is a length-preserving encryption mode that is efficient on
 * processors with instructions to accelerate aes and carryless
 * multiplication, e.g. x86 processors with AES-NI and CLMUL, and ARM
 * processors with the ARMv8 crypto extensions.
 *
 * Length-preserving encryption with HCTR2
 *	(https://eprint.iacr.org/2021/1441.pdf)
 *
 *	HCTR2 has a strict set of requirements for the hash function. For this
 *	purpose we only allow POLYVAL. To avoid misuse, XCTR is required as
 *	specified in the HCTR2 paper, though theoretically there is a larger class
 *	of algorithms that could be used.
 */

#include <crypto/internal/cipher.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/polyval.h>
#include <crypto/scatterwalk.h>
#include <linux/module.h>

#define BLOCKCIPHER_BLOCK_SIZE		16

/*
 * The specification allows variable-length tweaks, but Linux's crypto API
 * currently only allows algorithms to support a single length.  The "natural"
 * tweak length for HCTR2 is 16, since that fits into one POLYVAL block for
 * the best performance.  But longer tweaks are useful for fscrypt, to avoid
 * needing to derive per-file keys.  So instead we use two blocks, or 32 bytes.
 */
#define TWEAK_SIZE		32

struct hctr2_instance_ctx {
	struct crypto_cipher_spawn blockcipher_spawn;
	struct crypto_skcipher_spawn streamcipher_spawn;
	struct crypto_shash_spawn hash_spawn;
};

struct hctr2_tfm_ctx {
	struct crypto_cipher *blockcipher;
	struct crypto_skcipher *streamcipher;
	struct crypto_shash *hash;
	u8 L[BLOCKCIPHER_BLOCK_SIZE];
};

struct hctr2_request_ctx {
	u8 first_block[BLOCKCIPHER_BLOCK_SIZE];
	struct scatterlist *bulk_part_dst;
	struct scatterlist *bulk_part_src;
	struct scatterlist sg_src[2];
	struct scatterlist sg_dst[2];
	/* Sub-requests, must be last */
	union {
		struct shash_desc hash_desc;
		struct skcipher_request streamcipher_req;
	} u;
};

static int hctr2_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen)
{
	struct hctr2_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	u8 hbar[BLOCKCIPHER_BLOCK_SIZE];
	int err;

	crypto_cipher_clear_flags(tctx->blockcipher, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(tctx->blockcipher,
				crypto_skcipher_get_flags(tfm) &
				CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(tctx->blockcipher, key, keylen);
	if (err)
		return err;

	crypto_skcipher_clear_flags(tctx->streamcipher, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(tctx->streamcipher,
				  crypto_skcipher_get_flags(tfm) &
				  CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(tctx->streamcipher, key, keylen);
	if (err)
		return err;

	memset(tctx->L, 0, sizeof(tctx->L));
	memset(hbar, 0, sizeof(hbar));
	tctx->L[0] = 0x01;
	crypto_cipher_encrypt_one(tctx->blockcipher, tctx->L, tctx->L);
	crypto_cipher_encrypt_one(tctx->blockcipher, hbar, hbar);

	crypto_shash_clear_flags(tctx->hash, CRYPTO_TFM_REQ_MASK);
	crypto_shash_set_flags(tctx->hash, crypto_skcipher_get_flags(tfm) &
			       CRYPTO_TFM_REQ_MASK);
	err = crypto_shash_setkey(tctx->hash, hbar, BLOCKCIPHER_BLOCK_SIZE);
	return err;
}

static int hctr2_hash_tweak(struct skcipher_request *req, u8 *iv)
{
	u64 tweak_length_part[2];
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct hctr2_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct hctr2_request_ctx *rctx = skcipher_request_ctx(req);
	struct shash_desc *hash_desc = &rctx->u.hash_desc;
	int err;

	memset(tweak_length_part, 0, sizeof(tweak_length_part));
	if (req->cryptlen % POLYVAL_BLOCK_SIZE == 0)
		tweak_length_part[0] = cpu_to_le64(TWEAK_SIZE * 8 * 2 + 2);
	else
		tweak_length_part[0] = cpu_to_le64(TWEAK_SIZE * 8 * 2 + 3);

	hash_desc->tfm = tctx->hash;
	err = crypto_shash_init(hash_desc);
	if (err)
		return err;

	err = crypto_shash_update(hash_desc, (u8 *)tweak_length_part, sizeof(tweak_length_part));
	if (err)
		return err;
	err = crypto_shash_update(hash_desc, iv, TWEAK_SIZE);
	return err;
}

static int hctr2_hash_message(struct skcipher_request *req,
			      struct scatterlist *sgl,
			      u8 digest[POLYVAL_DIGEST_SIZE])
{
	u8 padding[BLOCKCIPHER_BLOCK_SIZE];
	struct hctr2_request_ctx *rctx = skcipher_request_ctx(req);
	struct shash_desc *hash_desc = &rctx->u.hash_desc;
	const unsigned int bulk_len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	struct sg_mapping_iter miter;
	unsigned int remainder = bulk_len % BLOCKCIPHER_BLOCK_SIZE;
	int err;

	sg_miter_start(&miter, sgl, sg_nents(sgl),
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);
	while (sg_miter_next(&miter)) {
		err = crypto_shash_update(hash_desc, miter.addr, miter.length);
		if (err)
			break;
	}
	sg_miter_stop(&miter);
	if (err)
		return err;

	if (remainder) {
		memset(padding, 0, BLOCKCIPHER_BLOCK_SIZE);
		padding[0] = 0x01;
		err = crypto_shash_update(hash_desc, padding, BLOCKCIPHER_BLOCK_SIZE - remainder);
		if (err)
			return err;
	}
	return crypto_shash_final(hash_desc, digest);
}

static int hctr2_finish(struct skcipher_request *req)
{
	struct hctr2_request_ctx *rctx = skcipher_request_ctx(req);
	u8 digest[POLYVAL_DIGEST_SIZE];
	int err;

	err = hctr2_hash_tweak(req, req->iv);
	if (err)
		return err;
	err = hctr2_hash_message(req, rctx->bulk_part_dst, digest);
	if (err)
		return err;
	crypto_xor(rctx->first_block, digest, BLOCKCIPHER_BLOCK_SIZE);

	scatterwalk_map_and_copy(rctx->first_block, req->dst,
				 0, BLOCKCIPHER_BLOCK_SIZE, 1);
	return 0;
}

static void hctr2_streamcipher_done(struct crypto_async_request *areq,
				    int err)
{
	struct skcipher_request *req = areq->data;

	if (!err)
		err = hctr2_finish(req);

	skcipher_request_complete(req, err);
}

static int hctr2_crypt(struct skcipher_request *req, bool enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct hctr2_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct hctr2_request_ctx *rctx = skcipher_request_ctx(req);
	u8 digest[POLYVAL_DIGEST_SIZE];
	int bulk_len = req->cryptlen - BLOCKCIPHER_BLOCK_SIZE;
	int err;

	// Requests must be at least one block
	if (req->cryptlen < BLOCKCIPHER_BLOCK_SIZE)
		return -EINVAL;

	scatterwalk_map_and_copy(rctx->first_block, req->src,
				 0, BLOCKCIPHER_BLOCK_SIZE, 0);
	rctx->bulk_part_src = scatterwalk_ffwd(rctx->sg_src, req->src, BLOCKCIPHER_BLOCK_SIZE);
	rctx->bulk_part_dst = scatterwalk_ffwd(rctx->sg_dst, req->dst, BLOCKCIPHER_BLOCK_SIZE);

	err = hctr2_hash_tweak(req, req->iv);
	if (err)
		return err;
	err = hctr2_hash_message(req, rctx->bulk_part_src, digest);
	if (err)
		return err;
	crypto_xor(digest, rctx->first_block, BLOCKCIPHER_BLOCK_SIZE);

	if (enc)
		crypto_cipher_encrypt_one(tctx->blockcipher, rctx->first_block, digest);
	else
		crypto_cipher_decrypt_one(tctx->blockcipher, rctx->first_block, digest);

	crypto_xor(digest, rctx->first_block, BLOCKCIPHER_BLOCK_SIZE);
	crypto_xor(digest, tctx->L, BLOCKCIPHER_BLOCK_SIZE);

	skcipher_request_set_tfm(&rctx->u.streamcipher_req, tctx->streamcipher);
	skcipher_request_set_crypt(&rctx->u.streamcipher_req, rctx->bulk_part_src,
				   rctx->bulk_part_dst, bulk_len, digest);
	skcipher_request_set_callback(&rctx->u.streamcipher_req,
				      req->base.flags,
				      hctr2_streamcipher_done, req);
	return crypto_skcipher_encrypt(&rctx->u.streamcipher_req) ?:
		hctr2_finish(req);
}

static int hctr2_encrypt(struct skcipher_request *req)
{
	return hctr2_crypt(req, true);
}

static int hctr2_decrypt(struct skcipher_request *req)
{
	return hctr2_crypt(req, false);
}

static int hctr2_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct hctr2_instance_ctx *ictx = skcipher_instance_ctx(inst);
	struct hctr2_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *streamcipher;
	struct crypto_cipher *blockcipher;
	struct crypto_shash *hash;
	unsigned int subreq_size;
	int err;

	streamcipher = crypto_spawn_skcipher(&ictx->streamcipher_spawn);
	if (IS_ERR(streamcipher))
		return PTR_ERR(streamcipher);

	blockcipher = crypto_spawn_cipher(&ictx->blockcipher_spawn);
	if (IS_ERR(blockcipher)) {
		err = PTR_ERR(blockcipher);
		goto err_free_streamcipher;
	}

	hash = crypto_spawn_shash(&ictx->hash_spawn);
	if (IS_ERR(hash)) {
		err = PTR_ERR(hash);
		goto err_free_blockcipher;
	}

	tctx->streamcipher = streamcipher;
	tctx->blockcipher = blockcipher;
	tctx->hash = hash;

	BUILD_BUG_ON(offsetofend(struct hctr2_request_ctx, u) !=
				 sizeof(struct hctr2_request_ctx));
	subreq_size = max(sizeof_field(struct hctr2_request_ctx, u.hash_desc) +
			  crypto_shash_descsize(hash), sizeof_field(struct
			  hctr2_request_ctx, u.streamcipher_req) +
			  crypto_skcipher_reqsize(streamcipher));

	crypto_skcipher_set_reqsize(tfm, offsetof(struct hctr2_request_ctx, u) +
				    subreq_size);
	return 0;

err_free_blockcipher:
	crypto_free_cipher(blockcipher);
err_free_streamcipher:
	crypto_free_skcipher(streamcipher);
	return err;
}

static void hctr2_exit_tfm(struct crypto_skcipher *tfm)
{
	struct hctr2_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);

	crypto_free_cipher(tctx->blockcipher);
	crypto_free_skcipher(tctx->streamcipher);
	crypto_free_shash(tctx->hash);
}

static void hctr2_free_instance(struct skcipher_instance *inst)
{
	struct hctr2_instance_ctx *ictx = skcipher_instance_ctx(inst);

	crypto_drop_cipher(&ictx->blockcipher_spawn);
	crypto_drop_skcipher(&ictx->streamcipher_spawn);
	crypto_drop_shash(&ictx->hash_spawn);
	kfree(inst);
}

/*
 * Check for a supported set of inner algorithms.
 * See the comment at the beginning of this file.
 */
static bool hctr2_supported_algorithms(struct skcipher_alg *streamcipher_alg,
				       struct crypto_alg *blockcipher_alg,
				       struct shash_alg *hash_alg)
{
	if (strncmp(streamcipher_alg->base.cra_name, "xctr(", 4) != 0)
		return false;

	if (blockcipher_alg->cra_blocksize != BLOCKCIPHER_BLOCK_SIZE)
		return false;

	if (strcmp(hash_alg->base.cra_name, "polyval") != 0)
		return false;

	return true;
}

static int hctr2_create_common(struct crypto_template *tmpl,
			       struct rtattr **tb,
			       const char *blockcipher_name,
			       const char *streamcipher_name,
			       const char *polyval_name)
{
	u32 mask;
	struct skcipher_instance *inst;
	struct hctr2_instance_ctx *ictx;
	struct skcipher_alg *streamcipher_alg;
	struct crypto_alg *blockcipher_alg;
	struct shash_alg *hash_alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SKCIPHER, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	ictx = skcipher_instance_ctx(inst);

	/* Stream cipher, xctr(block_cipher) */
	err = crypto_grab_skcipher(&ictx->streamcipher_spawn,
				   skcipher_crypto_instance(inst),
				   streamcipher_name, 0, mask);
	if (err)
		goto err_free_inst;
	streamcipher_alg = crypto_spawn_skcipher_alg(&ictx->streamcipher_spawn);

	/* Block cipher, e.g. "aes" */
	err = crypto_grab_cipher(&ictx->blockcipher_spawn,
				 skcipher_crypto_instance(inst),
				 blockcipher_name, 0, mask);
	if (err)
		goto err_free_inst;
	blockcipher_alg = crypto_spawn_cipher_alg(&ictx->blockcipher_spawn);

	/* Polyval ε-∆U hash function */
	err = crypto_grab_shash(&ictx->hash_spawn,
				skcipher_crypto_instance(inst),
				polyval_name, 0, mask);
	if (err)
		goto err_free_inst;
	hash_alg = crypto_spawn_shash_alg(&ictx->hash_spawn);

	/* Check the set of algorithms */
	if (!hctr2_supported_algorithms(streamcipher_alg, blockcipher_alg,
					hash_alg)) {
		pr_warn("Unsupported HCTR2 instantiation: (%s,%s,%s)\n",
			streamcipher_alg->base.cra_name,
			blockcipher_alg->cra_name, hash_alg->base.cra_name);
		err = -EINVAL;
		goto err_free_inst;
	}

	/* Instance fields */

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
				 "hctr2(%s)", blockcipher_alg->cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "hctr2(%s,%s,%s)",
		     blockcipher_alg->cra_driver_name,
		     streamcipher_alg->base.cra_driver_name,
		     hash_alg->base.cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_blocksize = BLOCKCIPHER_BLOCK_SIZE;
	inst->alg.base.cra_ctxsize = sizeof(struct hctr2_tfm_ctx);
	inst->alg.base.cra_alignmask = streamcipher_alg->base.cra_alignmask |
				       hash_alg->base.cra_alignmask;
	/*
	 * The hash function is called twice, so it is weighted higher than the
	 * streamcipher and blockcipher.
	 */
	inst->alg.base.cra_priority = (2 * streamcipher_alg->base.cra_priority +
				       4 * hash_alg->base.cra_priority +
				       blockcipher_alg->cra_priority) / 7;

	inst->alg.setkey = hctr2_setkey;
	inst->alg.encrypt = hctr2_encrypt;
	inst->alg.decrypt = hctr2_decrypt;
	inst->alg.init = hctr2_init_tfm;
	inst->alg.exit = hctr2_exit_tfm;
	inst->alg.min_keysize = crypto_skcipher_alg_min_keysize(streamcipher_alg);
	inst->alg.max_keysize = crypto_skcipher_alg_max_keysize(streamcipher_alg);
	inst->alg.ivsize = TWEAK_SIZE;

	inst->free = hctr2_free_instance;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		hctr2_free_instance(inst);
	}
	return err;
}

static int hctr2_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	const char *blockcipher_name;
	char streamcipher_name[CRYPTO_MAX_ALG_NAME];

	blockcipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(blockcipher_name))
		return PTR_ERR(blockcipher_name);

	if (snprintf(streamcipher_name, CRYPTO_MAX_ALG_NAME, "xctr(%s)",
		    blockcipher_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;
	return hctr2_create_common(tmpl, tb, blockcipher_name, streamcipher_name, "polyval");
}

/* hctr2(blockcipher_name [, xctr(blockcipher_name)] [, polyval_name]) */
static struct crypto_template hctr2_tmpl = {
	.name = "hctr2",
	.create = hctr2_create,
	.module = THIS_MODULE,
};

static int __init hctr2_module_init(void)
{
	return crypto_register_template(&hctr2_tmpl);
}

static void __exit hctr2_module_exit(void)
{
	crypto_unregister_template(&hctr2_tmpl);
}

subsys_initcall(hctr2_module_init);
module_exit(hctr2_module_exit);

MODULE_DESCRIPTION("HCTR2 length-preserving encryption mode");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("hctr2");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
