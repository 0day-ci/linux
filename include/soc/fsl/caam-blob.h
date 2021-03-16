/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Pengutronix, Ahmad Fatoum <kernel@pengutronix.de>
 */

#ifndef __CAAM_BLOB_GEN
#define __CAAM_BLOB_GEN

#include <linux/types.h>

#define CAAM_BLOB_KEYMOD_LENGTH		16
#define CAAM_BLOB_OVERHEAD		(32 + 16)
#define CAAM_BLOB_MAX_LEN		4096

struct caam_blob_priv;

/** caam_blob_gen_init - initialize blob generation
 *
 * returns either pointer to new caam_blob_priv instance
 * or error pointer
 */
struct caam_blob_priv *caam_blob_gen_init(void);

/** caam_blob_gen_init - free blob generation resources
 *
 * @priv: instance returned by caam_blob_gen_init
 */
void caam_blob_gen_exit(struct caam_blob_priv *priv);

/** caam_encap_blob - encapsulate blob
 *
 * @priv:   instance returned by caam_blob_gen_init
 * @keymod: string to use as key modifier for blob encapsulation
 * @input:  buffer which CAAM will DMA from
 * @output: buffer which CAAM will DMA to
 * @length: buffer length including blob overhead
 *          CAAM_BLOB_OVERHEAD < length <= CAAM_BLOB_MAX_LEN
 */
int caam_encap_blob(struct caam_blob_priv *priv, const char *keymod,
		    void *input, void *output, size_t length);

/** caam_decap_blob - decapsulate blob
 *
 * @priv:   instance returned by caam_blob_gen_init
 * @keymod: string to use as key modifier for blob decapsulation
 * @input:  buffer which CAAM will DMA from
 * @output: buffer which CAAM will DMA to
 * @length: buffer length including blob overhead
 *          CAAM_BLOB_OVERHEAD < length <= CAAM_BLOB_MAX_LEN
 */
int caam_decap_blob(struct caam_blob_priv *priv, const char *keymod,
		    void *input, void *output, size_t length);

#endif
