/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XCTR: XOR Counter mode
 *
 * Copyright 2021 Google LLC
 */

#include <asm/unaligned.h>

#ifndef _CRYPTO_XCTR_H
#define _CRYPTO_XCTR_H

static inline void u32_to_le_block(u8 *a, u32 x, unsigned int size)
{
	memset(a, 0, size);
	put_unaligned(cpu_to_le32(x), (u32 *)a);
}

#endif  /* _CRYPTO_XCTR_H */
