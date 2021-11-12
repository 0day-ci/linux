// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unwrap a PEM-encoded asymmetric key. This implementation unwraps
 * the interoperable text encoding format specified in RFC 7468.
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2021, Oracle and/or its affiliates.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/key-type.h>

#include "pem.h"

/* Encapsulation boundaries */
#define PEM_EB_MARKER		"-----"
#define PEM_BEGIN_MARKER	PEM_EB_MARKER "BEGIN"
#define PEM_END_MARKER		PEM_EB_MARKER "END"

/*
 * Unremarkable table-driven base64 decoder based on the public domain
 * implementation provided at:
 *   https://en.wikibooks.org/wiki/Algorithm_Implementation/Miscellaneous/Base64
 *
 * XXX: Replace this implementation with one that handles EBCDIC input properly.
 */

#define WHITESPACE 253
#define EQUALS     254
#define INVALID    255

static const u8 alphabet[] = {
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, WHITESPACE, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, 62,      INVALID, INVALID, INVALID, 63,
	52,      53,      54,      55,      56,      57,      58,      59,
	60,      61,      INVALID, INVALID, INVALID, EQUALS,  INVALID, INVALID,
	INVALID, 0,       1,       2,       3,       4,       5,       6,
	7,       8,       9,       10,      11,      12,      13,      14,
	15,      16,      17,      18,      19,      20,      21,      22,
	23,      24,      25,      INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, 26,      27,      28,      29,      30,      31,      32,
	33,      34,      35,      36,      37,      38,      39,      40,
	41,      42,      43,      44,      45,      46,      47,      48,
	49,      50,      51,      INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
	INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID, INVALID,
};

static bool base64decode(unsigned char *in, size_t inLen,
			 unsigned char *out, size_t *outLen)
{
	unsigned char *end = in + inLen;
	char iter = 0;
	uint32_t buf = 0;
	size_t len = 0;

	while (in < end) {
		u8 c = alphabet[*in++];

		switch (c) {
		case WHITESPACE:
			continue;
		case INVALID:
			return false;
		case EQUALS:
			in = end;
			continue;
		default:
			buf = buf << 6 | c;
			iter++;

			if (iter == 4) {
				if ((len += 3) > *outLen)
					return false;
				*(out++) = (buf >> 16) & 255;
				*(out++) = (buf >> 8) & 255;
				*(out++) = buf & 255;
				buf = 0;
				iter = 0;
			}
		}
	}

	if (iter == 3) {
		if ((len += 2) > *outLen)
			return false;
		*(out++) = (buf >> 10) & 255;
		*(out++) = (buf >> 2) & 255;
	} else if (iter == 2) {
		if (++len > *outLen)
			return false;
		*(out++) = (buf >> 4) & 255;
	}

	*outLen = len;
	return true;
}

/**
 * pem_decode - Attempt to decode a PEM-encoded data blob.
 * @prep: Data content to examine
 *
 * Assumptions:
 * - The input data buffer is not more than a few pages in size.
 * - The input data buffer has already been vetted for proper
 *   kernel read access.
 * - The input data buffer might not be NUL-terminated.
 *
 * PEM type labels are ignored. Subsequent parsing of the
 * decoded message adequately identifies its content.
 *
 * On success, a pointer to a dynamically-allocated buffer
 * containing the decoded content is returned. This buffer is
 * vfree'd in the .free_preparse method.
 *
 * Return values:
 *   %1: @prep.decoded points to the decoded message
 *   %0: @prep did not contain a PEM-encoded message
 *
 * A negative errno is returned if an unexpected error has
 * occurred (eg, memory exhaustion).
 */
int pem_decode(struct key_preparsed_payload *prep)
{
	const unsigned char *in = prep->data;
	unsigned char *begin, *end, *out;
	size_t outlen;

	prep->decoded = NULL;
	prep->decoded_len = 0;

	/* Find the beginning encapsulation boundary */
	begin = strnstr(in, PEM_BEGIN_MARKER, prep->datalen);
	if (!begin)
		goto out_not_pem;
	begin = strnstr(begin, PEM_EB_MARKER, begin - in);
	if (!begin)
		goto out_not_pem;
	begin += sizeof(PEM_EB_MARKER);

	/* Find the ending encapsulation boundary */
	end = strnstr(begin, PEM_END_MARKER, begin - in);
	if (!end)
		goto out_not_pem;
	if (!strnstr(end, PEM_EB_MARKER, end - in))
		goto out_not_pem;
	end--;

	/* Attempt to decode */
	out = vmalloc(end - begin);
	if (!out)
		return -ENOMEM;
	if (!base64decode(begin, end - begin, out, &outlen)) {
		vfree(out);
		goto out_not_pem;
	}

	prep->decoded = out;
	prep->decoded_len = outlen;
	return 1;

out_not_pem:
	return 0;
}
