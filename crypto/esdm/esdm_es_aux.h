/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _ESDM_ES_AUX_H
#define _ESDM_ES_AUX_H

#include "esdm_drng_mgr.h"
#include "esdm_es_mgr_cb.h"

u32 esdm_get_digestsize(void);
void esdm_pool_set_entropy(u32 entropy_bits);
int esdm_pool_insert_aux(const u8 *inbuf, u32 inbuflen, u32 entropy_bits);

extern struct esdm_es_cb esdm_es_aux;

/****************************** Helper code ***********************************/

/* Obtain the security strength of the ESDM in bits */
static inline u32 esdm_security_strength(void)
{
	/*
	 * We use a hash to read the entropy in the entropy pool. According to
	 * SP800-90B table 1, the entropy can be at most the digest size.
	 * Considering this together with the last sentence in section 3.1.5.1.2
	 * the security strength of a (approved) hash is equal to its output
	 * size. On the other hand the entropy cannot be larger than the
	 * security strength of the used DRBG.
	 */
	return min_t(u32, ESDM_FULL_SEED_ENTROPY_BITS, esdm_get_digestsize());
}

static inline u32 esdm_get_seed_entropy_osr(bool fully_seeded)
{
	u32 requested_bits = esdm_security_strength();

	/* Apply oversampling during initialization according to SP800-90C */
	if (esdm_sp80090c_compliant() && !fully_seeded)
		requested_bits += CONFIG_CRYPTO_ESDM_SEED_BUFFER_INIT_ADD_BITS;
	return requested_bits;
}

#endif /* _ESDM_ES_AUX_H */
