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

#else /* CONFIG_FS_ENCRYPTION */

static inline void ceph_fscrypt_set_ops(struct super_block *sb)
{
}

#endif /* CONFIG_FS_ENCRYPTION */

#endif
