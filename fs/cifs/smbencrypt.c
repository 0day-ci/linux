// SPDX-License-Identifier: GPL-2.0-or-later
/*
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-2000
   Copyright (C) Luke Kenneth Casson Leighton 1996-2000
   Modified by Jeremy Allison 1995.
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2002-2003
   Modified by Steve French (sfrench@us.ibm.com) 2002-2003

*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fips.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <crypto/des.h>
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "cifsproto.h"

#ifndef false
#define false 0
#endif
#ifndef true
#define true 1
#endif

/* following came from the other byteorder.h to avoid include conflicts */
#define CVAL(buf,pos) (((unsigned char *)(buf))[pos])
#define SSVALX(buf,pos,val) (CVAL(buf,pos)=(val)&0xFF,CVAL(buf,pos+1)=(val)>>8)
#define SSVAL(buf,pos,val) SSVALX((buf),(pos),((__u16)(val)))

#define MD4_DIGEST_SIZE		16
#define MD4_HMAC_BLOCK_SIZE	64
#define MD4_BLOCK_WORDS		16
#define MD4_HASH_WORDS		4

struct md4_ctx {
	u32 hash[MD4_HASH_WORDS];
	u32 block[MD4_BLOCK_WORDS];
	u64 byte_count;
};

static inline u32 lshift(u32 x, unsigned int s)
{
	x &= 0xFFFFFFFF;
	return ((x << s) & 0xFFFFFFFF) | (x >> (32 - s));
}

static inline u32 F(u32 x, u32 y, u32 z)
{
	return (x & y) | ((~x) & z);
}

static inline u32 G(u32 x, u32 y, u32 z)
{
	return (x & y) | (x & z) | (y & z);
}

static inline u32 H(u32 x, u32 y, u32 z)
{
	return x ^ y ^ z;
}

#define ROUND1(a,b,c,d,k,s) (a = lshift(a + F(b,c,d) + k, s))
#define ROUND2(a,b,c,d,k,s) (a = lshift(a + G(b,c,d) + k + (u32)0x5A827999,s))
#define ROUND3(a,b,c,d,k,s) (a = lshift(a + H(b,c,d) + k + (u32)0x6ED9EBA1,s))

static void md4_transform(u32 *hash, u32 const *in)
{
	u32 a, b, c, d;

	a = hash[0];
	b = hash[1];
	c = hash[2];
	d = hash[3];

	ROUND1(a, b, c, d, in[0], 3);
	ROUND1(d, a, b, c, in[1], 7);
	ROUND1(c, d, a, b, in[2], 11);
	ROUND1(b, c, d, a, in[3], 19);
	ROUND1(a, b, c, d, in[4], 3);
	ROUND1(d, a, b, c, in[5], 7);
	ROUND1(c, d, a, b, in[6], 11);
	ROUND1(b, c, d, a, in[7], 19);
	ROUND1(a, b, c, d, in[8], 3);
	ROUND1(d, a, b, c, in[9], 7);
	ROUND1(c, d, a, b, in[10], 11);
	ROUND1(b, c, d, a, in[11], 19);
	ROUND1(a, b, c, d, in[12], 3);
	ROUND1(d, a, b, c, in[13], 7);
	ROUND1(c, d, a, b, in[14], 11);
	ROUND1(b, c, d, a, in[15], 19);

	ROUND2(a, b, c, d,in[ 0], 3);
	ROUND2(d, a, b, c, in[4], 5);
	ROUND2(c, d, a, b, in[8], 9);
	ROUND2(b, c, d, a, in[12], 13);
	ROUND2(a, b, c, d, in[1], 3);
	ROUND2(d, a, b, c, in[5], 5);
	ROUND2(c, d, a, b, in[9], 9);
	ROUND2(b, c, d, a, in[13], 13);
	ROUND2(a, b, c, d, in[2], 3);
	ROUND2(d, a, b, c, in[6], 5);
	ROUND2(c, d, a, b, in[10], 9);
	ROUND2(b, c, d, a, in[14], 13);
	ROUND2(a, b, c, d, in[3], 3);
	ROUND2(d, a, b, c, in[7], 5);
	ROUND2(c, d, a, b, in[11], 9);
	ROUND2(b, c, d, a, in[15], 13);

	ROUND3(a, b, c, d,in[ 0], 3);
	ROUND3(d, a, b, c, in[8], 9);
	ROUND3(c, d, a, b, in[4], 11);
	ROUND3(b, c, d, a, in[12], 15);
	ROUND3(a, b, c, d, in[2], 3);
	ROUND3(d, a, b, c, in[10], 9);
	ROUND3(c, d, a, b, in[6], 11);
	ROUND3(b, c, d, a, in[14], 15);
	ROUND3(a, b, c, d, in[1], 3);
	ROUND3(d, a, b, c, in[9], 9);
	ROUND3(c, d, a, b, in[5], 11);
	ROUND3(b, c, d, a, in[13], 15);
	ROUND3(a, b, c, d, in[3], 3);
	ROUND3(d, a, b, c, in[11], 9);
	ROUND3(c, d, a, b, in[7], 11);
	ROUND3(b, c, d, a, in[15], 15);

	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
}

static inline void md4_transform_helper(struct md4_ctx *ctx)
{
	le32_to_cpu_array(ctx->block, ARRAY_SIZE(ctx->block));
	md4_transform(ctx->hash, ctx->block);
}

static void md4_init(struct md4_ctx *mctx)
{
	mctx->hash[0] = 0x67452301;
	mctx->hash[1] = 0xefcdab89;
	mctx->hash[2] = 0x98badcfe;
	mctx->hash[3] = 0x10325476;
	mctx->byte_count = 0;
}

static void md4_update(struct md4_ctx *mctx, const u8 *data, unsigned int len)
{
	const u32 avail = sizeof(mctx->block) - (mctx->byte_count & 0x3f);

	mctx->byte_count += len;

	if (avail > len) {
		memcpy((char *)mctx->block + (sizeof(mctx->block) - avail),
		       data, len);
		return;
	}

	memcpy((char *)mctx->block + (sizeof(mctx->block) - avail),
	       data, avail);

	md4_transform_helper(mctx);
	data += avail;
	len -= avail;

	while (len >= sizeof(mctx->block)) {
		memcpy(mctx->block, data, sizeof(mctx->block));
		md4_transform_helper(mctx);
		data += sizeof(mctx->block);
		len -= sizeof(mctx->block);
	}

	memcpy(mctx->block, data, len);
}

static void md4_final(struct md4_ctx *mctx, u8 *out)
{
	const unsigned int offset = mctx->byte_count & 0x3f;
	char *p = (char *)mctx->block + offset;
	int padding = 56 - (offset + 1);

	*p++ = 0x80;
	if (padding < 0) {
		memset(p, 0x00, padding + sizeof (u64));
		md4_transform_helper(mctx);
		p = (char *)mctx->block;
		padding = 56;
	}

	memset(p, 0, padding);
	mctx->block[14] = mctx->byte_count << 3;
	mctx->block[15] = mctx->byte_count >> 29;
	le32_to_cpu_array(mctx->block, (sizeof(mctx->block) -
	                  sizeof(u64)) / sizeof(u32));
	md4_transform(mctx->hash, mctx->block);
	cpu_to_le32_array(mctx->hash, ARRAY_SIZE(mctx->hash));
	memcpy(out, mctx->hash, sizeof(mctx->hash));
	memset(mctx, 0, sizeof(*mctx));
}

static void
str_to_key(unsigned char *str, unsigned char *key)
{
	int i;

	key[0] = str[0] >> 1;
	key[1] = ((str[0] & 0x01) << 6) | (str[1] >> 2);
	key[2] = ((str[1] & 0x03) << 5) | (str[2] >> 3);
	key[3] = ((str[2] & 0x07) << 4) | (str[3] >> 4);
	key[4] = ((str[3] & 0x0F) << 3) | (str[4] >> 5);
	key[5] = ((str[4] & 0x1F) << 2) | (str[5] >> 6);
	key[6] = ((str[5] & 0x3F) << 1) | (str[6] >> 7);
	key[7] = str[6] & 0x7F;
	for (i = 0; i < 8; i++)
		key[i] = (key[i] << 1);
}

static int
smbhash(unsigned char *out, const unsigned char *in, unsigned char *key)
{
	unsigned char key2[8];
	struct des_ctx ctx;

	str_to_key(key, key2);

	if (fips_enabled) {
		cifs_dbg(VFS, "FIPS compliance enabled: DES not permitted\n");
		return -ENOENT;
	}

	des_expand_key(&ctx, key2, DES_KEY_SIZE);
	des_encrypt(&ctx, out, in);
	memzero_explicit(&ctx, sizeof(ctx));

	return 0;
}

static int
E_P16(unsigned char *p14, unsigned char *p16)
{
	int rc;
	unsigned char sp8[8] =
	    { 0x4b, 0x47, 0x53, 0x21, 0x40, 0x23, 0x24, 0x25 };

	rc = smbhash(p16, sp8, p14);
	if (rc)
		return rc;
	rc = smbhash(p16 + 8, sp8, p14 + 7);
	return rc;
}

static int
E_P24(unsigned char *p21, const unsigned char *c8, unsigned char *p24)
{
	int rc;

	rc = smbhash(p24, c8, p21);
	if (rc)
		return rc;
	rc = smbhash(p24 + 8, c8, p21 + 7);
	if (rc)
		return rc;
	rc = smbhash(p24 + 16, c8, p21 + 14);
	return rc;
}

/* produce a md4 message digest from data of length n bytes */
int
mdfour(unsigned char *md4_hash, unsigned char *link_str, int link_len)
{
	struct md4_ctx md4;

	md4_init(&md4);
	md4_update(&md4, link_str, link_len);
	md4_final(&md4, md4_hash);

	memzero_explicit(&md4, sizeof(md4));
	return 0;
}

/*
   This implements the X/Open SMB password encryption
   It takes a password, a 8 byte "crypt key" and puts 24 bytes of
   encrypted password into p24 */
/* Note that password must be uppercased and null terminated */
int
SMBencrypt(unsigned char *passwd, const unsigned char *c8, unsigned char *p24)
{
	int rc;
	unsigned char p14[14], p16[16], p21[21];

	memset(p14, '\0', 14);
	memset(p16, '\0', 16);
	memset(p21, '\0', 21);

	memcpy(p14, passwd, 14);
	rc = E_P16(p14, p16);
	if (rc)
		return rc;

	memcpy(p21, p16, 16);
	rc = E_P24(p21, c8, p24);

	return rc;
}

/*
 * Creates the MD4 Hash of the users password in NT UNICODE.
 */

int
E_md4hash(const unsigned char *passwd, unsigned char *p16,
	const struct nls_table *codepage)
{
	int rc;
	int len;
	__le16 wpwd[129];

	/* Password cannot be longer than 128 characters */
	if (passwd) /* Password must be converted to NT unicode */
		len = cifs_strtoUTF16(wpwd, passwd, 128, codepage);
	else {
		len = 0;
		*wpwd = 0; /* Ensure string is null terminated */
	}

	rc = mdfour(p16, (unsigned char *) wpwd, len * sizeof(__le16));
	memzero_explicit(wpwd, sizeof(wpwd));

	return rc;
}

/* Does the NT MD4 hash then des encryption. */
int
SMBNTencrypt(unsigned char *passwd, unsigned char *c8, unsigned char *p24,
		const struct nls_table *codepage)
{
	int rc;
	unsigned char p16[16], p21[21];

	memset(p16, '\0', 16);
	memset(p21, '\0', 21);

	rc = E_md4hash(passwd, p16, codepage);
	if (rc) {
		cifs_dbg(FYI, "%s Can't generate NT hash, error: %d\n",
			 __func__, rc);
		return rc;
	}
	memcpy(p21, p16, 16);
	rc = E_P24(p21, c8, p24);
	return rc;
}
