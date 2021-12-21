// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>

#include "dlb_main.h"

/*
 * dlb tracks its memory mappings so it can revoke them when an FLR is
 * requested and user-space cannot be allowed to access the device. To achieve
 * that, the driver creates a single inode through which all driver-created
 * files can share a struct address_space, and unmaps the inode's address space
 * during the reset preparation phase. Since the anon inode layer shares its
 * inode with multiple kernel components, we cannot use that here.
 *
 * Doing so requires a custom pseudo-filesystem to allocate the inode. The FS
 * and the inode are allocated on demand when a file is created, and both are
 * freed when the last such file is closed.
 *
 * This is inspired by other drivers (cxl, dax, mem) and the anon inode layer.
 */
static int dlb_fs_cnt;
static struct vfsmount *dlb_vfs_mount;

#define DLBFS_MAGIC 0x444C4232 /* ASCII for DLB */
static int dlb_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, DLBFS_MAGIC) ? 0 : -ENOMEM;
}

static struct file_system_type dlb_fs_type = {
	.name	 = "dlb",
	.owner	 = THIS_MODULE,
	.init_fs_context = dlb_init_fs_context,
	.kill_sb = kill_anon_super,
};

/* Allocate an anonymous inode. Must hold the resource mutex while calling. */
static struct inode *dlb_alloc_inode(struct dlb *dlb)
{
	struct inode *inode;
	int ret;

	/* Increment the pseudo-FS's refcnt and (if not already) mount it. */
	ret = simple_pin_fs(&dlb_fs_type, &dlb_vfs_mount, &dlb_fs_cnt);
	if (ret < 0) {
		dev_err(dlb->dev,
			"[%s()] Cannot mount pseudo filesystem: %d\n",
			__func__, ret);
		return ERR_PTR(ret);
	}

	dlb->inode_cnt++;

	if (dlb->inode_cnt > 1) {
		/*
		 * Return the previously allocated inode. In this case, there
		 * is guaranteed >= 1 reference and so ihold() is safe to call.
		 */
		ihold(dlb->inode);
		return dlb->inode;
	}

	inode = alloc_anon_inode(dlb_vfs_mount->mnt_sb);
	if (IS_ERR(inode)) {
		dev_err(dlb->dev,
			"[%s()] Cannot allocate inode: %ld\n",
			__func__, PTR_ERR(inode));
		dlb->inode_cnt = 0;
		simple_release_fs(&dlb_vfs_mount, &dlb_fs_cnt);
	}

	dlb->inode = inode;

	return inode;
}

/*
 * Decrement the inode reference count and release the FS. Intended for
 * unwinding dlb_alloc_inode(). Must hold the resource mutex while calling.
 */
static void dlb_free_inode(struct inode *inode)
{
	iput(inode);
	simple_release_fs(&dlb_vfs_mount, &dlb_fs_cnt);
}

/*
 * Release the FS. Intended for use in a file_operations release callback,
 * which decrements the inode reference count separately. Must hold the
 * resource mutex while calling.
 */
void dlb_release_fs(struct dlb *dlb)
{
	mutex_lock(&dlb_driver_mutex);

	simple_release_fs(&dlb_vfs_mount, &dlb_fs_cnt);

	dlb->inode_cnt--;

	/* When the fs refcnt reaches zero, the inode has been freed */
	if (dlb->inode_cnt == 0)
		dlb->inode = NULL;

	mutex_unlock(&dlb_driver_mutex);
}

/*
 * Allocate a file with the requested flags, file operations, and name that
 * uses the device's shared inode. Must hold the resource mutex while calling.
 *
 * Caller must separately allocate an fd and install the file in that fd.
 */
struct file *dlb_getfile(struct dlb *dlb,
			 int flags,
			 const struct file_operations *fops,
			 const char *name)
{
	struct inode *inode;
	struct file *f;

	if (!try_module_get(THIS_MODULE))
		return ERR_PTR(-ENOENT);

	mutex_lock(&dlb_driver_mutex);

	inode = dlb_alloc_inode(dlb);
	if (IS_ERR(inode)) {
		mutex_unlock(&dlb_driver_mutex);
		module_put(THIS_MODULE);
		return ERR_CAST(inode);
	}

	f = alloc_file_pseudo(inode, dlb_vfs_mount, name, flags, fops);
	if (IS_ERR(f)) {
		dlb_free_inode(inode);
		mutex_unlock(&dlb_driver_mutex);
		module_put(THIS_MODULE);
		return f;
	}

	mutex_unlock(&dlb_driver_mutex);

	return f;
}
