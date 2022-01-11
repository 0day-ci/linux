// SPDX-License-Identifier: GPL-2.0+
/*
 * Module signature checker
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/module_signature.h>
#include <asm/byteorder.h>

/**
 * mod_check_sig_marker - check that the given data has signature marker at the end
 *
 * @data:	Data with appended signature
 * @len:	Length of data. Signature marker length is subtracted on success.
 */
static inline int mod_check_sig_marker(const void *data, unsigned long *len)
{
	const unsigned long markerlen = sizeof(MODULE_SIG_STRING) - 1;

	if (markerlen > *len)
		return -ENODATA;

	if (memcmp(data + *len - markerlen, MODULE_SIG_STRING,
		   markerlen))
		return -ENODATA;

	*len -= markerlen;
	return 0;
}

/**
 * mod_check_sig - check that the given signature is sane
 *
 * @ms:		Signature to check.
 * @file_len:	Size of the file to which @ms is appended (without the marker).
 * @name:	What is being checked. Used for error messages.
 */
int mod_check_sig(const struct module_signature *ms, unsigned long file_len,
		  const char *name)
{
	if (be32_to_cpu(ms->sig_len) >= file_len - sizeof(*ms))
		return -EBADMSG;

	if (ms->id_type != PKEY_ID_PKCS7) {
		pr_err("%s: not signed with expected PKCS#7 message\n",
		       name);
		return -ENOPKG;
	}

	if (ms->algo != 0 ||
	    ms->hash != 0 ||
	    ms->signer_len != 0 ||
	    ms->key_id_len != 0 ||
	    ms->__pad[0] != 0 ||
	    ms->__pad[1] != 0 ||
	    ms->__pad[2] != 0) {
		pr_err("%s: PKCS#7 signature info has unexpected non-zero params\n",
		       name);
		return -EBADMSG;
	}

	return 0;
}

/**
 * mod_parse_sig - check that the given signature is sane and determine signature length
 *
 * @data:	Data with appended signature.
 * @len:	Length of data. Signature and marker length is subtracted on success.
 * @sig_len:	Length of signature. Filled on success.
 * @name:	What is being checked. Used for error messages.
 */
int mod_parse_sig(const void *data, unsigned long *len, unsigned long *sig_len, const char *name)
{
	const struct module_signature *sig;
	int rc;

	rc = mod_check_sig_marker(data, len);
	if (rc)
		return rc;

	if (*len < sizeof(*sig))
		return -EBADMSG;

	sig = data + (*len - sizeof(*sig));

	rc = mod_check_sig(sig, *len, name);
	if (rc)
		return rc;

	*sig_len = be32_to_cpu(sig->sig_len);
	*len -= *sig_len + sizeof(*sig);

	return 0;
}
