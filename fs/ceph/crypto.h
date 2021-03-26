/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ceph fscrypt functionality
 */

#ifndef _CEPH_CRYPTO_H
#define _CEPH_CRYPTO_H

#include <crypto/sha2.h>
#include <linux/fscrypt.h>

#define	CEPH_XATTR_NAME_ENCRYPTION_CONTEXT	"encryption.ctx"

struct ceph_fname {
	struct inode	*dir;
	char 		*name;		// b64 encoded, possibly hashed
	unsigned char	*ctext;		// binary crypttext (if any)
	u32		name_len;	// length of name buffer
	u32		ctext_len;	// length of crypttext
};

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

static inline int ceph_fname_alloc_buffer(struct inode *parent, struct fscrypt_str *fname)
{
	if (!IS_ENCRYPTED(parent))
		return 0;
	return fscrypt_fname_alloc_buffer(NAME_MAX, fname);
}

static inline void ceph_fname_free_buffer(struct inode *parent, struct fscrypt_str *fname)
{
	if (IS_ENCRYPTED(parent))
		fscrypt_fname_free_buffer(fname);
}

int ceph_fname_to_usr(const struct ceph_fname *fname, struct fscrypt_str *tname,
			struct fscrypt_str *oname, bool *is_nokey);

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

static inline int ceph_fname_alloc_buffer(struct inode *parent, struct fscrypt_str *fname)
{
	return 0;
}

static inline void ceph_fname_free_buffer(struct inode *parent, struct fscrypt_str *fname)
{
}

static inline int ceph_fname_to_usr(const struct ceph_fname *fname, struct fscrypt_str *tname,
				    struct fscrypt_str *oname, bool *is_nokey)
{
	oname->name = fname->name;
	oname->len = fname->name_len;
	return 0;
}

#endif /* CONFIG_FS_ENCRYPTION */

#endif
