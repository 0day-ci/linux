/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for the Polyval hash algorithm
 *
 * Copyright 2021 Google LLC
 */

#ifndef _CRYPTO_POLYVAL_H
#define _CRYPTO_POLYVAL_H

#include <linux/types.h>
#include <linux/crypto.h>

#define POLYVAL_BLOCK_SIZE	16
#define POLYVAL_DIGEST_SIZE	16

struct polyval_desc_ctx {
	u8 buffer[POLYVAL_BLOCK_SIZE];
	u32 bytes;
};

#endif
