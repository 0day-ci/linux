/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2015  Intel Corporation
 * Copyright (C) 2021 SUSE
 *
 */
#ifndef _RSA_COMMON_
#define _RSA_COMMON_

#include <crypto/algapi.h>
#include <crypto/internal/akcipher.h>
#include <linux/scatterlist.h>

struct rsa_asn1_template {
	const char	*name;
	const u8	*data;
	size_t		size;
};
const struct rsa_asn1_template *rsa_lookup_asn1(const char *name);

struct rsapad_tfm_ctx {
	struct crypto_akcipher *child;
	unsigned int key_size;
};

struct rsapad_inst_ctx {
	struct crypto_akcipher_spawn spawn;
	const struct rsa_asn1_template *digest_info;
};

struct rsapad_akciper_req_ctx {
	struct scatterlist in_sg[2], out_sg[1];
	uint8_t *in_buf, *out_buf;
	struct akcipher_request child_req;
};

int rsapad_set_pub_key(struct crypto_akcipher *tfm, const void *key,
		       unsigned int keylen);
int rsapad_set_priv_key(struct crypto_akcipher *tfm, const void *key,
			unsigned int keylen);
unsigned int rsapad_get_max_size(struct crypto_akcipher *tfm);
void rsapad_akcipher_sg_set_buf(struct scatterlist *sg, void *buf,
				size_t len, struct scatterlist *next);
int rsapad_akcipher_init_tfm(struct crypto_akcipher *tfm);
void rsapad_akcipher_exit_tfm(struct crypto_akcipher *tfm);
void rsapad_akcipher_free(struct akcipher_instance *inst);
int rsapad_akcipher_create(struct crypto_template *tmpl, struct rtattr **tb,
			   struct akcipher_alg *alg);

#endif
