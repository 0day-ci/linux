/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ceph fscrypt functionality
 */

#ifndef _CEPH_CRYPTO_H
#define _CEPH_CRYPTO_H

#include <crypto/sha2.h>
#include <linux/fscrypt.h>

#define	CEPH_XATTR_NAME_ENCRYPTION_CONTEXT	"encryption.ctx"

#ifdef CONFIG_FS_ENCRYPTION

/*
 * We want to encrypt filenames when creating them, but the encrypted
 * versions of those names may have illegal characters in them. To mitigate
 * that, we base64 encode them, but that gives us a result that can exceed
 * NAME_MAX.
 *
 * Follow a similar scheme to fscrypt itself, and cap the filename to a
 * smaller size. If the cleartext name is longer than the value below, then
 * sha256 hash the remaining bytes.
 *
 * 189 bytes => 252 bytes base64-encoded, which is <= NAME_MAX (255)
 */
#define CEPH_NOHASH_NAME_MAX (189 - SHA256_DIGEST_SIZE)

void ceph_fscrypt_set_ops(struct super_block *sb);

static inline void ceph_fscrypt_free_dummy_policy(struct ceph_fs_client *fsc)
{
	fscrypt_free_dummy_policy(&fsc->dummy_enc_policy);
}

int ceph_fscrypt_prepare_context(struct inode *dir, struct inode *inode,
				 struct ceph_acl_sec_ctx *as);

#else /* CONFIG_FS_ENCRYPTION */

static inline void ceph_fscrypt_set_ops(struct super_block *sb)
{
}

static inline void ceph_fscrypt_free_dummy_policy(struct ceph_fs_client *fsc)
{
}

static inline int ceph_fscrypt_prepare_context(struct inode *dir, struct inode *inode,
						struct ceph_acl_sec_ctx *as)
{
	if (IS_ENCRYPTED(dir))
		return -EOPNOTSUPP;
	return 0;
}

#endif /* CONFIG_FS_ENCRYPTION */

#endif
