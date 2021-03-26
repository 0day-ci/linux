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

static const union fscrypt_policy *ceph_get_dummy_policy(struct super_block *sb)
{
	return ceph_sb_to_client(sb)->dummy_enc_policy.policy;
}

static struct fscrypt_operations ceph_fscrypt_ops = {
	.get_context		= ceph_crypt_get_context,
	.set_context		= ceph_crypt_set_context,
	.get_dummy_policy	= ceph_get_dummy_policy,
	.empty_dir		= ceph_crypt_empty_dir,
	.max_namelen		= NAME_MAX,
};

void ceph_fscrypt_set_ops(struct super_block *sb)
{
	fscrypt_set_ops(sb, &ceph_fscrypt_ops);
}

int ceph_fscrypt_prepare_context(struct inode *dir, struct inode *inode,
				 struct ceph_acl_sec_ctx *as)
{
	int ret, ctxsize;
	size_t name_len;
	char *name;
	struct ceph_pagelist *pagelist = as->pagelist;
	bool encrypted = false;

	ret = fscrypt_prepare_new_inode(dir, inode, &encrypted);
	if (ret)
		return ret;
	if (!encrypted)
		return 0;

	inode->i_flags |= S_ENCRYPTED;

	ctxsize = fscrypt_context_for_new_inode(&as->fscrypt, inode);
	if (ctxsize < 0)
		return ctxsize;

	/* marshal it in page array */
	if (!pagelist) {
		pagelist = ceph_pagelist_alloc(GFP_KERNEL);
		if (!pagelist)
			return -ENOMEM;
		ret = ceph_pagelist_reserve(pagelist, PAGE_SIZE);
		if (ret)
			goto out;
		ceph_pagelist_encode_32(pagelist, 1);
	}

	name = CEPH_XATTR_NAME_ENCRYPTION_CONTEXT;
	name_len = strlen(name);
	ret = ceph_pagelist_reserve(pagelist, 4 * 2 + name_len + ctxsize);
	if (ret)
		goto out;

	if (as->pagelist) {
		BUG_ON(pagelist->length <= sizeof(__le32));
		if (list_is_singular(&pagelist->head)) {
			le32_add_cpu((__le32*)pagelist->mapped_tail, 1);
		} else {
			struct page *page = list_first_entry(&pagelist->head,
							     struct page, lru);
			void *addr = kmap_atomic(page);
			le32_add_cpu((__le32*)addr, 1);
			kunmap_atomic(addr);
		}
	}

	ceph_pagelist_encode_32(pagelist, name_len);
	ceph_pagelist_append(pagelist, name, name_len);
	ceph_pagelist_encode_32(pagelist, ctxsize);
	ceph_pagelist_append(pagelist, as->fscrypt, ctxsize);
out:
	if (pagelist && !as->pagelist)
		ceph_pagelist_release(pagelist);
	return ret;
}
