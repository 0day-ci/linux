// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>
#include <linux/xattr.h>
#include <linux/fscrypt.h>

#include "super.h"
#include "crypto.h"

static int ceph_crypt_get_context(struct inode *inode, void *ctx, size_t len)
{
	return __ceph_getxattr(inode, CEPH_XATTR_NAME_ENCRYPTION_CONTEXT, ctx, len);
}

static int ceph_crypt_set_context(struct inode *inode, const void *ctx, size_t len, void *fs_data)
{
	int ret;

	WARN_ON_ONCE(fs_data);
	ret = __ceph_setxattr(inode, CEPH_XATTR_NAME_ENCRYPTION_CONTEXT, ctx, len, XATTR_CREATE);
	if (ret == 0)
		inode_set_flags(inode, S_ENCRYPTED, S_ENCRYPTED);
	return ret;
}

static bool ceph_crypt_empty_dir(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	return ci->i_rsubdirs + ci->i_rfiles == 1;
}

static struct fscrypt_operations ceph_fscrypt_ops = {
	.get_context		= ceph_crypt_get_context,
	.set_context		= ceph_crypt_set_context,
	.empty_dir		= ceph_crypt_empty_dir,
	.max_namelen		= NAME_MAX,
};

void ceph_fscrypt_set_ops(struct super_block *sb)
{
	fscrypt_set_ops(sb, &ceph_fscrypt_ops);
}
