// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RSASSA-PSS signature scheme.
 *
 * Copyright (C) 2021, SUSE
 * Authors: Varad Gautam <varad.gautam@suse.com>
 */

#include <crypto/hash.h>
#include <crypto/internal/akcipher.h>
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

static int psspad_s_v_e_d(struct akcipher_request *req)
{
	return -EOPNOTSUPP;
}

static struct akcipher_alg psspad_alg = {
	.init = rsapad_akcipher_init_tfm,
	.exit = rsapad_akcipher_exit_tfm,

	.encrypt = psspad_s_v_e_d,
	.decrypt = psspad_s_v_e_d,
	.sign = psspad_s_v_e_d,
	.verify = psspad_s_v_e_d,
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
