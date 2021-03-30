// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2015  Intel Corporation
 * Copyright (C) 2021 SUSE
 *
 */

#include <crypto/internal/rsa-common.h>

/*
 * Hash algorithm OIDs plus ASN.1 DER wrappings [RFC4880 sec 5.2.2].
 */
static const u8 rsa_digest_info_md5[] = {
	0x30, 0x20, 0x30, 0x0c, 0x06, 0x08,
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05, /* OID */
	0x05, 0x00, 0x04, 0x10
};

static const u8 rsa_digest_info_sha1[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2b, 0x0e, 0x03, 0x02, 0x1a,
	0x05, 0x00, 0x04, 0x14
};

static const u8 rsa_digest_info_rmd160[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2b, 0x24, 0x03, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x14
};

static const u8 rsa_digest_info_sha224[] = {
	0x30, 0x2d, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04,
	0x05, 0x00, 0x04, 0x1c
};

static const u8 rsa_digest_info_sha256[] = {
	0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x20
};

static const u8 rsa_digest_info_sha384[] = {
	0x30, 0x41, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02,
	0x05, 0x00, 0x04, 0x30
};

static const u8 rsa_digest_info_sha512[] = {
	0x30, 0x51, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03,
	0x05, 0x00, 0x04, 0x40
};

static const struct rsa_asn1_template rsa_asn1_templates[] = {
#define _(X) { #X, rsa_digest_info_##X, sizeof(rsa_digest_info_##X) }
	_(md5),
	_(sha1),
	_(rmd160),
	_(sha256),
	_(sha384),
	_(sha512),
	_(sha224),
	{ NULL }
#undef _
};

const struct rsa_asn1_template *rsa_lookup_asn1(const char *name)
{
	const struct rsa_asn1_template *p;

	for (p = rsa_asn1_templates; p->name; p++)
		if (strcmp(name, p->name) == 0)
			return p;
	return NULL;
}

int rsapad_set_pub_key(struct crypto_akcipher *tfm, const void *key,
		       unsigned int keylen)
{
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
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
	return 0;
}

int rsapad_set_priv_key(struct crypto_akcipher *tfm, const void *key,
			unsigned int keylen)
{
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	int err;

	ctx->key_size = 0;

	err = crypto_akcipher_set_priv_key(ctx->child, key, keylen);
	if (err)
		return err;

	/* Find out new modulus size from rsa implementation */
	err = crypto_akcipher_maxsize(ctx->child);
	if (err > PAGE_SIZE)
		return -EOPNOTSUPP;

	ctx->key_size = err;
	return 0;
}

unsigned int rsapad_get_max_size(struct crypto_akcipher *tfm)
{
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);

	/*
	 * The maximum destination buffer size for the encrypt/sign operations
	 * will be the same as for RSA, even though it's smaller for
	 * decrypt/verify.
	 */

	return ctx->key_size;
}

void rsapad_akcipher_sg_set_buf(struct scatterlist *sg, void *buf,
				size_t len, struct scatterlist *next)
{
	int nsegs = next ? 2 : 1;

	sg_init_table(sg, nsegs);
	sg_set_buf(sg, buf, len);

	if (next)
		sg_chain(sg, nsegs, next);
}

int rsapad_akcipher_init_tfm(struct crypto_akcipher *tfm)
{
	struct akcipher_instance *inst = akcipher_alg_instance(tfm);
	struct rsapad_inst_ctx *ictx = akcipher_instance_ctx(inst);
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct crypto_akcipher *child_tfm;

	child_tfm = crypto_spawn_akcipher(&ictx->spawn);
	if (IS_ERR(child_tfm))
		return PTR_ERR(child_tfm);

	ctx->child = child_tfm;
	return 0;
}

void rsapad_akcipher_exit_tfm(struct crypto_akcipher *tfm)
{
	struct rsapad_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);

	crypto_free_akcipher(ctx->child);
}

void rsapad_akcipher_free(struct akcipher_instance *inst)
{
	struct rsapad_inst_ctx *ctx = akcipher_instance_ctx(inst);
	struct crypto_akcipher_spawn *spawn = &ctx->spawn;

	crypto_drop_akcipher(spawn);
	kfree(inst);
}

int rsapad_akcipher_create(struct crypto_template *tmpl, struct rtattr **tb,
			   struct akcipher_alg *alg)
{
	u32 mask;
	struct akcipher_instance *inst;
	struct rsapad_inst_ctx *ctx;
	struct akcipher_alg *rsa_alg;
	const char *hash_name;
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
	hash_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(hash_name)) {
		if (snprintf(inst->alg.base.cra_name,
			     CRYPTO_MAX_ALG_NAME, "%s(%s)", tmpl->name,
			     rsa_alg->base.cra_name) >= CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;

		if (snprintf(inst->alg.base.cra_driver_name,
			     CRYPTO_MAX_ALG_NAME, "%s(%s)", tmpl->name,
			     rsa_alg->base.cra_driver_name) >=
			     CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;
	} else {
		ctx->digest_info = rsa_lookup_asn1(hash_name);
		if (!ctx->digest_info) {
			err = -EINVAL;
			goto err_free_inst;
		}

		if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
			     "%s(%s,%s)", tmpl->name, rsa_alg->base.cra_name,
			     hash_name) >= CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;

		if (snprintf(inst->alg.base.cra_driver_name,
			     CRYPTO_MAX_ALG_NAME, "%s(%s,%s)",
			     tmpl->name,
			     rsa_alg->base.cra_driver_name,
			     hash_name) >= CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;
	}

	inst->alg.base.cra_priority = rsa_alg->base.cra_priority;
	inst->alg.base.cra_ctxsize = sizeof(struct rsapad_tfm_ctx);

	inst->alg.init = alg->init;
	inst->alg.exit = alg->exit;

	inst->alg.encrypt = alg->encrypt;
	inst->alg.decrypt = alg->decrypt;
	inst->alg.sign = alg->sign;
	inst->alg.verify = alg->verify;
	inst->alg.set_pub_key = alg->set_pub_key;
	inst->alg.set_priv_key = alg->set_priv_key;
	inst->alg.max_size = alg->max_size;
	inst->alg.reqsize = sizeof(struct rsapad_akciper_req_ctx) + rsa_alg->reqsize;

	inst->free = rsapad_akcipher_free;

	err = akcipher_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		rsapad_akcipher_free(inst);
	}
	return err;
}
