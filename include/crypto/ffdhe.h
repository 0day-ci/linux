/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Finite-Field Diffie-Hellman definition according to RFC 7919
 *
 * Copyright (c) 2021, SUSE Software Products
 * Authors: Hannes Reinecke <hare@suse.de>
 */
#ifndef _CRYPTO_FFDHE_
#define _CRYPTO_FFDHE_

/**
 * crypto_ffdhe_params() - Generate FFDHE params
 * @params: DH params
 * @bits: Bitsize of the FFDHE parameters
 *
 * This functions sets the FFDHE parameter for @bits in @params.
 * Valid bit sizes are 2048, 3072, 4096, 6144, or 8194.
 *
 * Returns: 0 on success, errno on failure.
 */

int crypto_ffdhe_params(struct dh *p, int bits);

#endif /* _CRYPTO_FFDHE_H */
