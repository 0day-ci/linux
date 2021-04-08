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

#endif
