/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PKS_KEYS_H
#define _LINUX_PKS_KEYS_H

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

#include <asm/pkeys_common.h>

/**
 * DOC: PKS_KEY_ALLOCATION
 *
 * Users reserve a key value by adding an entry to enum pks_pkey_consumers with
 * a unique value from 1 to 15.  Then replacing that value in the
 * PKS_INIT_VALUE macro using the desired default protection; PKR_RW_KEY(),
 * PKR_WD_KEY(), or PKR_AD_KEY().
 *
 * PKS_KEY_DEFAULT must remain 0 key with a default of read/write to support
 * non-pks protected pages.  Unused keys should be set (Access Disabled
 * PKR_AD_KEY()).
 *
 * For example to configure a key for 'MY_FEATURE' with a default of Write
 * Disabled.
 *
 * .. code-block:: c
 *
 *	enum pks_pkey_consumers
 *	{
 *		PKS_KEY_DEFAULT         = 0,
 *		PKS_KEY_MY_FEATURE      = 1,
 *		PKS_KEY_NR_CONSUMERS    = 2,
 *	}
 *
 *	#define PKS_INIT_VALUE (PKR_RW_KEY(PKS_KEY_DEFAULT)		|
 *				PKR_WD_KEY(PKS_KEY_MY_FEATURE)		|
 *				PKR_AD_KEY(2)	| PKR_AD_KEY(3)		|
 *				PKR_AD_KEY(4)	| PKR_AD_KEY(5)		|
 *				PKR_AD_KEY(6)	| PKR_AD_KEY(7)		|
 *				PKR_AD_KEY(8)	| PKR_AD_KEY(9)		|
 *				PKR_AD_KEY(10)	| PKR_AD_KEY(11)	|
 *				PKR_AD_KEY(12)	| PKR_AD_KEY(13)	|
 *				PKR_AD_KEY(14)	| PKR_AD_KEY(15))
 *
 */
enum pks_pkey_consumers {
	PKS_KEY_DEFAULT			= 0, /* Must be 0 for default PTE values */
	PKS_KEY_TEST			= 1,
	PKS_KEY_PGMAP_PROTECTION	= 2,
	PKS_KEY_NR_CONSUMERS		= 3,
};

#define PKS_INIT_VALUE (PKR_RW_KEY(PKS_KEY_DEFAULT)		| \
			PKR_AD_KEY(PKS_KEY_TEST)	| \
			PKR_AD_KEY(PKS_KEY_PGMAP_PROTECTION)	| \
			PKR_AD_KEY(3)	| \
			PKR_AD_KEY(4)	| PKR_AD_KEY(5)		| \
			PKR_AD_KEY(6)	| PKR_AD_KEY(7)		| \
			PKR_AD_KEY(8)	| PKR_AD_KEY(9)		| \
			PKR_AD_KEY(10)	| PKR_AD_KEY(11)	| \
			PKR_AD_KEY(12)	| PKR_AD_KEY(13)	| \
			PKR_AD_KEY(14)	| PKR_AD_KEY(15))

#endif /* CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

#endif /* _LINUX_PKS_KEYS_H */
