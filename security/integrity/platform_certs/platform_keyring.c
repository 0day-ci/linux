// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform keyring for firmware/platform keys
 *
 * Copyright IBM Corporation, 2018
 * Author(s): Nayna Jain <nayna@linux.ibm.com>
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <keys/system_keyring.h>
#include "../integrity.h"

extern __initconst const u8 platform_certificate_list[];
extern __initconst const unsigned long platform_certificate_list_size;

/**
 * add_to_platform_keyring - Add to platform keyring without validation.
 * @source: Source of key
 * @data: The blob holding the key
 * @len: The length of the data blob
 *
 * Add a key to the platform keyring without checking its trust chain.  This
 * is available only during kernel initialisation.
 */
void __init add_to_platform_keyring(const char *source, const void *data,
				    size_t len)
{
	key_perm_t perm;
	int rc;

	perm = (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_VIEW;

	rc = integrity_load_cert(INTEGRITY_KEYRING_PLATFORM, source, data, len,
				 perm);
	if (rc)
		pr_info("Error adding keys to platform keyring %s\n", source);
}

static __init int load_platform_certificate_list(void)
{
	const u8 *p;
	unsigned long size;
	int rc;
	struct key *keyring;

	p = platform_certificate_list;
	size = platform_certificate_list_size;

	keyring = integrity_keyring_from_id(INTEGRITY_KEYRING_PLATFORM);
	if (IS_ERR(keyring))
		return PTR_ERR(keyring);

	rc = load_certificate_list(p, size, keyring);
	if (rc)
		pr_info("Error adding keys to platform keyring %d\n", rc);

	return rc;
}
late_initcall(load_platform_certificate_list);

/*
 * Create the trusted keyrings.
 */
static __init int platform_keyring_init(void)
{
	int rc;

	rc = integrity_init_keyring(INTEGRITY_KEYRING_PLATFORM);
	if (rc)
		return rc;

	pr_notice("Platform Keyring initialized\n");
	return 0;
}

/*
 * Must be initialised before we try and load the keys into the keyring.
 */
device_initcall(platform_keyring_init);
