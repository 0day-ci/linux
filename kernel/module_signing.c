// SPDX-License-Identifier: GPL-2.0-or-later
/* Module signature checker
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/module_signature.h>
#include <linux/string.h>
#include <linux/verification.h>
#include <crypto/public_key.h>
#include "module-internal.h"

/**
 * verify_appended_signature - Verify the signature on a module
 * @data: The data to be verified
 * @len: Size of @data.
 * @trusted_keys: Keyring to use for verification
 * @purpose: The use to which the key is being put
 */
int verify_appended_signature(const void *data, size_t *len,
			      struct key *trusted_keys,
			      enum key_being_used_for purpose)
{
	struct module_signature ms;
	size_t sig_len;
	int ret;

	pr_devel("==>%s %s(,%zu)\n", __func__, key_being_used_for[purpose], *len);

	ret = mod_parse_sig(data, len, &sig_len, key_being_used_for[purpose]);
	if (ret)
		return ret;

	return verify_pkcs7_signature(data, *len, data + *len, sig_len,
				      trusted_keys,
				      purpose,
				      NULL, NULL);
}
