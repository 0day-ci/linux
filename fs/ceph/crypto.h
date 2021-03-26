/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ceph fscrypt functionality
 */

#ifndef _CEPH_CRYPTO_H
#define _CEPH_CRYPTO_H

#include <linux/fscrypt.h>

#define	CEPH_XATTR_NAME_ENCRYPTION_CONTEXT	"encryption.ctx"

#ifdef CONFIG_FS_ENCRYPTION
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
