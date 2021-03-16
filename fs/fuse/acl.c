/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2016 Canonical Ltd. <seth.forshee@canonical.com>
 *
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
 */

#include "fuse_i.h"

#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>

struct posix_acl *fuse_get_acl(struct inode *inode, int type)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	int size;
	const char *name;
	void *value = NULL;
	struct posix_acl *acl;

	if (fuse_is_bad(inode))
		return ERR_PTR(-EIO);

	if (!fc->posix_acl || fc->no_getxattr)
		return NULL;

	if (type == ACL_TYPE_ACCESS)
		name = XATTR_NAME_POSIX_ACL_ACCESS;
	else if (type == ACL_TYPE_DEFAULT)
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
	else
		return ERR_PTR(-EOPNOTSUPP);

	value = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!value)
		return ERR_PTR(-ENOMEM);
	size = fuse_getxattr(inode, name, value, PAGE_SIZE);
	if (size > 0)
		acl = posix_acl_from_xattr(fc->user_ns, value, size);
	else if ((size == 0) || (size == -ENODATA) ||
		 (size == -EOPNOTSUPP && fc->no_getxattr))
		acl = NULL;
	else if (size == -ERANGE)
		acl = ERR_PTR(-E2BIG);
	else
		acl = ERR_PTR(size);

	kfree(value);
	return acl;
}

static int fuse_acl_mode_setattr(struct inode *inode, umode_t mode)
{
	struct fuse_mount *fm = get_fuse_mount(inode);
	FUSE_ARGS(args);
	struct fuse_setattr_in inarg;
	struct fuse_attr_out outarg;

	memset(&inarg, 0, sizeof(inarg));
	memset(&outarg, 0, sizeof(outarg));

	inarg.valid = FATTR_MODE;
	inarg.mode = mode;
	fuse_setattr_fill(fm->fc, &args, inode, &inarg, &outarg);

	return fuse_simple_request(fm, &args);
}

int fuse_set_acl(struct user_namespace *mnt_userns, struct inode *inode,
		 struct posix_acl *acl, int type)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	umode_t new_mode;
	bool update_mode = false;
	size_t size = 0;
	void *value =  NULL;
	const char *name;
	int ret;

	if (fuse_is_bad(inode))
		return -EIO;

	if (!fc->posix_acl || fc->no_setxattr)
		return -EOPNOTSUPP;

	if (type == ACL_TYPE_ACCESS) {
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		if (acl && fc->posix_acl_update_mode) {
			/*
			 * Setting access ACL might clear SGID.
			 * Refresh inode->i_mode before making a decision.
			 */
			ret = fuse_do_getattr(inode, NULL, NULL);
			if (ret)
				return ret;
			ret = posix_acl_update_mode(&init_user_ns, inode,
						    &new_mode, &acl);
			if (ret)
				return ret;
			if (new_mode != inode->i_mode)
				update_mode = true;
		}
	} else if (type == ACL_TYPE_DEFAULT)
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
	else
		return -EINVAL;

	if (acl) {
		/*
		 * Fuse userspace is responsible for updating access
		 * permissions in the inode, if needed. fuse_setxattr
		 * invalidates the inode attributes, which will force
		 * them to be refreshed the next time they are used,
		 * and it also updates i_ctime.
		 */
		size = posix_acl_xattr_size(acl->a_count);

		if (size > PAGE_SIZE)
			return -E2BIG;

		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;

		ret = posix_acl_to_xattr(fc->user_ns, acl, value, size);
		if (ret < 0) {
			kfree(value);
			return ret;
		}
	}

	if (update_mode) {
		ret = fuse_acl_mode_setattr(inode, new_mode);
		if (ret < 0) {
			kfree(value);
			return ret;
		}
	}

	if (acl) {
		ret = fuse_setxattr(inode, name, value, size, 0);
		/* TODO: If setxattr failed, should we restore mode ? */
		kfree(value);
	} else {
		ret = fuse_removexattr(inode, name);
	}
	forget_all_cached_acls(inode);
	fuse_invalidate_attr(inode);

	return ret;
}
