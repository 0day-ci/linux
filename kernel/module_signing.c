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
 * verify_appended_signature - Verify the signature on a module with the
 * signature marker stripped.
 * @data: The data to be verified
 * @len: Size of @data.
 * @trusted_keys: Keyring to use for verification
 * @what: Informational string for log messages
 */
int verify_appended_signature(const void *data, size_t *len,
			      struct key *trusted_keys, const char *what)
{
	struct module_signature ms;
	size_t sig_len, modlen = *len;
	int ret;

	pr_devel("==>%s(,%zu)\n", __func__, modlen);

	if (modlen <= sizeof(ms))
		return -EBADMSG;

	memcpy(&ms, data + (modlen - sizeof(ms)), sizeof(ms));

	ret = mod_check_sig(&ms, modlen, what);
	if (ret)
		return ret;

	sig_len = be32_to_cpu(ms.sig_len);
	modlen -= sig_len + sizeof(ms);
	*len = modlen;

	return verify_pkcs7_signature(data, modlen, data + modlen, sig_len,
				      trusted_keys,
				      VERIFYING_MODULE_SIGNATURE,
				      NULL, NULL);
}
