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
int verify_appended_signature(const void *data, unsigned long *len,
			      struct key *trusted_keys,
			      enum key_being_used_for purpose)
{
	const unsigned long markerlen = sizeof(MODULE_SIG_STRING) - 1;
	struct module_signature *ms;
	unsigned long sig_len, modlen = *len;
	int ret;

	pr_devel("==>%s %s(,%lu)\n", __func__, key_being_used_for[purpose], modlen);

	if (markerlen > modlen)
		return -ENODATA;

	if (memcmp(data + modlen - markerlen, MODULE_SIG_STRING,
		   markerlen))
		return -ENODATA;
	modlen -= markerlen;

	if (modlen <= sizeof(*ms))
		return -EBADMSG;

	ms = data + modlen - sizeof(*ms);

	ret = mod_check_sig(ms, modlen, key_being_used_for[purpose]);
	if (ret)
		return ret;

	sig_len = be32_to_cpu(ms->sig_len);
	modlen -= sig_len + sizeof(*ms);
	*len = modlen;

	return verify_pkcs7_signature(data, modlen, data + modlen, sig_len,
				      trusted_keys,
				      purpose,
				      NULL, NULL);
}
