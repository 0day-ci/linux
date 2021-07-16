// SPDX-License-Identifier: GPL-2.0
/*
 * base64.c - RFC4648-compliant base64 encoding
 *
 * Copyright (c) 2020 Hannes Reinecke, SUSE
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/base64.h>

static const char lookup_table[65] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * base64_encode() - base64-encode some bytes
 * @src: the bytes to encode
 * @len: number of bytes to encode
 * @dst: (output) the base64-encoded string.  Not NUL-terminated.
 *
 * Encodes the input string using characters from the set [A-Za-z0-9+,].
 * The encoded string is roughly 4/3 times the size of the input string.
 *
 * Return: length of the encoded string
 */
int base64_encode(const u8 *src, int len, char *dst)
{
	int i, bits = 0;
	u32 ac = 0;
	char *cp = dst;

	for (i = 0; i < len; i++) {
		ac = (ac << 8) | src[i];
		bits += 8;
		if (bits < 24)
			continue;
		do {
			bits -= 6;
			*cp++ = lookup_table[(ac >> bits) & 0x3f];
		} while (bits);
		ac = 0;
	}
	if (bits) {
		int more = 0;

		if (bits < 16)
			more = 2;
		ac = (ac << (2 + more));
		bits += (2 + more);
		do {
			bits -= 6;
			*cp++ = lookup_table[(ac >> bits) & 0x3f];
		} while (bits);
		*cp++ = '=';
		if (more)
			*cp++ = '=';
	}

	return cp - dst;
}
EXPORT_SYMBOL_GPL(base64_encode);

/**
 * base64_decode() - base64-decode some bytes
 * @src: the base64-encoded string to decode
 * @len: number of bytes to decode
 * @dst: (output) the decoded bytes.
 *
 * Decodes the base64-encoded bytes @src according to RFC 4648.
 *
 * Return: number of decoded bytes
 */
int base64_decode(const char *src, int len, u8 *dst)
{
        int i, bits = 0, pad = 0;
        u32 ac = 0;
        size_t dst_len = 0;

        for (i = 0; i < len; i++) {
                int c, p = -1;

                if (src[i] == '=') {
                        pad++;
                        if (i + 1 < len && src[i + 1] == '=')
                                pad++;
                        break;
                }
                for (c = 0; c < strlen(lookup_table); c++) {
                        if (src[i] == lookup_table[c]) {
                                p = c;
                                break;
                        }
                }
                if (p < 0)
                        break;
                ac = (ac << 6) | p;
                bits += 6;
                if (bits < 24)
                        continue;
                while (bits) {
                        bits -= 8;
                        dst[dst_len++] = (ac >> bits) & 0xff;
                }
                ac = 0;
        }
        dst_len -= pad;
        return dst_len;
}
EXPORT_SYMBOL_GPL(base64_decode);
