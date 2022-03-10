/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKEYS_COMMON_H
#define _ASM_X86_PKEYS_COMMON_H

#define PKS_NUM_PKEYS 16
#define PKS_ALL_AD (0x55555555UL)

#define PKR_AD_BIT 0x1u
#define PKR_WD_BIT 0x2u
#define PKR_BITS_PER_PKEY 2

#define PKR_PKEY_SHIFT(pkey)	(pkey * PKR_BITS_PER_PKEY)

#define PKR_RW_MASK(pkey)	(0          << PKR_PKEY_SHIFT(pkey))
#define PKR_AD_MASK(pkey)	(PKR_AD_BIT << PKR_PKEY_SHIFT(pkey))
#define PKR_WD_MASK(pkey)	(PKR_WD_BIT << PKR_PKEY_SHIFT(pkey))

#endif /*_ASM_X86_PKEYS_COMMON_H */
