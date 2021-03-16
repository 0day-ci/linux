// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Pengutronix, Ahmad Fatoum <kernel@pengutronix.de>
 */

#include <keys/trusted_caam.h>
#include <keys/trusted-type.h>
#include <linux/build_bug.h>
#include <linux/key-type.h>
#include <soc/fsl/caam-blob.h>

struct caam_blob_priv *blobifier;

#define KEYMOD "kernel:trusted"

static_assert(MAX_KEY_SIZE + CAAM_BLOB_OVERHEAD <= CAAM_BLOB_MAX_LEN);
static_assert(MAX_BLOB_SIZE <= CAAM_BLOB_MAX_LEN);

static int trusted_caam_seal(struct trusted_key_payload *p, char *datablob)
{
	int length = p->key_len + CAAM_BLOB_OVERHEAD;
	int ret;

	ret = caam_encap_blob(blobifier, KEYMOD, p->key, p->blob, length);
	if (ret)
		return ret;

	p->blob_len = length;
	return 0;
}

static int trusted_caam_unseal(struct trusted_key_payload *p, char *datablob)
{
	int length = p->blob_len;
	int ret;

	ret = caam_decap_blob(blobifier, KEYMOD, p->blob, p->key, length);
	if (ret)
		return ret;

	p->key_len = length - CAAM_BLOB_OVERHEAD;
	return 0;
}

static int trusted_caam_init(void)
{
	int ret;

	blobifier = caam_blob_gen_init();
	if (IS_ERR(blobifier)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(blobifier);
	}

	ret = register_key_type(&key_type_trusted);
	if (ret)
		caam_blob_gen_exit(blobifier);

	return ret;
}

static void trusted_caam_exit(void)
{
	unregister_key_type(&key_type_trusted);
	caam_blob_gen_exit(blobifier);
}

struct trusted_key_ops caam_trusted_key_ops = {
	.migratable = 0, /* non-migratable */
	.init = trusted_caam_init,
	.seal = trusted_caam_seal,
	.unseal = trusted_caam_unseal,
	.exit = trusted_caam_exit,
};
