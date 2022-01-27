/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKEYS_COMMON_H
#define _ASM_X86_PKEYS_COMMON_H

#define PKR_AD_BIT 0x1u
#define PKR_WD_BIT 0x2u
#define PKR_BITS_PER_PKEY 2

#define PKR_PKEY_SHIFT(pkey)	(pkey * PKR_BITS_PER_PKEY)

#define PKR_RW_KEY(pkey)	(0          << PKR_PKEY_SHIFT(pkey))
#define PKR_AD_KEY(pkey)	(PKR_AD_BIT << PKR_PKEY_SHIFT(pkey))
#define PKR_WD_KEY(pkey)	(PKR_WD_BIT << PKR_PKEY_SHIFT(pkey))

#endif /*_ASM_X86_PKEYS_COMMON_H */
