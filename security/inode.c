// SPDX-License-Identifier: GPL-2.0-only
/*
 *  inode.c - securityfs
 *
 *  Copyright (C) 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 *  Based on fs/debugfs/inode.c which had the following copyright notice:
 *    Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *    Copyright (C) 2004 IBM Inc.
 */

/* #define DEBUG */
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/lsm_hooks.h>
#include <linux/magic.h>
#include <linux/user_namespace.h>

static struct vfsmount *init_securityfs_mount;
static int init_securityfs_mount_count;

static int securityfs_permission(struct user_namespace *mnt_userns,
				 struct inode *inode, int mask)
{
	int err;

	err = generic_permission(&init_user_ns, inode, mask);
	if (!err) {
		/* Unless bind-mounted, deny access if current_user_ns() is not
		 * ancestor.
		 */
		if (inode->i_sb->s_user_ns != &init_user_ns &&
		    !in_userns(current_user_ns(), inode->i_sb->s_user_ns))
			err = -EACCES;
	}

	return err;
}

static const struct inode_operations securityfs_dir_inode_operations = {
	.permission	= securityfs_permission,
	.lookup		= simple_lookup,
};

static const struct inode_operations securityfs_file_inode_operations = {
	.permission	= securityfs_permission,
};

static void securityfs_free_inode(struct inode *inode)
{
	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
	free_inode_nonrcu(inode);
}

static const struct super_operations securityfs_super_operations = {
	.statfs		= simple_statfs,
	.free_inode	= securityfs_free_inode,
};

static int securityfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	static const struct tree_descr files[] = {{""}};
	struct user_namespace *ns = fc->user_ns;
	int error;

	if (WARN_ON(ns != current_user_ns()))
		return -EINVAL;

	error = simple_fill_super(sb, SECURITYFS_MAGIC, files);
	if (error)
		return error;

	sb->s_op = &securityfs_super_operations;
	sb->s_root->d_inode->i_op = &securityfs_dir_inode_operations;

	return 0;
}

static int securityfs_get_tree(struct fs_context *fc)
{
	return get_tree_keyed(fc, securityfs_fill_super, fc->user_ns);
}

static const struct fs_context_operations securityfs_context_ops = {
	.get_tree	= securityfs_get_tree,
};

static int securityfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &securityfs_context_ops;
	return 0;
}

static struct file_system_type fs_type = {
	.owner =	THIS_MODULE,
	.name =		"securityfs",
	.init_fs_context = securityfs_init_fs_context,
	.kill_sb =	kill_litter_super,
	.fs_flags =	FS_USERNS_MOUNT,
};

/**
 * securityfs_create_dentry - create a dentry in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the securityfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 * @iops: a point to a struct of inode_operations that should be used for
 *        this file/dir
 *
 * This is the basic "create a file/dir/symlink" function for
 * securityfs.  It allows for a wide range of flexibility in creating
 * a file, or a directory (if you want to create a directory, the
 * securityfs_create_dir() function is recommended to be used
 * instead).
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the
 * file is to be removed (no automatic cleanup happens if your module
 * is unloaded, you are responsible here).  If an error occurs, the
 * function will return the error value (via ERR_PTR).
 *
 * If securityfs is not enabled in the kernel, the value %-ENODEV is
 * returned.
 */
static struct dentry *securityfs_create_dentry(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const struct file_operations *fops,
					const struct inode_operations *iops)
{
	struct user_namespace *ns = current_user_ns();
	struct dentry *dentry;
	struct inode *dir, *inode;
	int error;

	if (!(mode & S_IFMT))
		mode = (mode & S_IALLUGO) | S_IFREG;

	pr_debug("securityfs: creating file '%s'\n",name);

	if (ns == &init_user_ns) {
		error = simple_pin_fs(&fs_type, &init_securityfs_mount,
				      &init_securityfs_mount_count);
		if (error)
			return ERR_PTR(error);
	}

	if (!parent) {
		if (ns == &init_user_ns)
			parent = init_securityfs_mount->mnt_root;
		else
			return ERR_PTR(-EINVAL);
	}

	dir = d_inode(parent);

	inode_lock(dir);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry))
		goto out;

	if (d_really_is_positive(dentry)) {
		error = -EEXIST;
		goto out1;
	}

	inode = new_inode(dir->i_sb);
	if (!inode) {
		error = -ENOMEM;
		goto out1;
	}

	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_private = data;
	if (S_ISDIR(mode)) {
		inode->i_op = &securityfs_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		inc_nlink(inode);
		inc_nlink(dir);
	} else if (S_ISLNK(mode)) {
		inode->i_op = iops ? iops : &simple_symlink_inode_operations;
		inode->i_link = data;
	} else {
		inode->i_op = &securityfs_file_inode_operations;
		inode->i_fop = fops;
	}
	d_instantiate(dentry, inode);
	inode_unlock(dir);
	return dentry;

out1:
	dput(dentry);
	dentry = ERR_PTR(error);
out:
	inode_unlock(dir);
	if (ns == &init_user_ns)
		simple_release_fs(&init_securityfs_mount,
				  &init_securityfs_mount_count);
	return dentry;
}

/**
 * securityfs_create_file - create a file in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the securityfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * This function creates a file in securityfs with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 *
 * If securityfs is not enabled in the kernel, the value %-ENODEV is
 * returned.
 */
struct dentry *securityfs_create_file(const char *name, umode_t mode,
				      struct dentry *parent, void *data,
				      const struct file_operations *fops)
{
	return securityfs_create_dentry(name, mode, parent, data, fops, NULL);
}
EXPORT_SYMBOL_GPL(securityfs_create_file);

/**
 * securityfs_create_dir - create a directory in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the directory to
 *        create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          directory will be created in the root of the securityfs filesystem.
 *
 * This function creates a directory in securityfs with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 *
 * If securityfs is not enabled in the kernel, the value %-ENODEV is
 * returned.
 */
struct dentry *securityfs_create_dir(const char *name, struct dentry *parent)
{
	return securityfs_create_file(name, S_IFDIR | 0755, parent, NULL, NULL);
}
EXPORT_SYMBOL_GPL(securityfs_create_dir);

/**
 * securityfs_create_symlink - create a symlink in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the symlink to
 *        create.
 * @parent: a pointer to the parent dentry for the symlink.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          directory will be created in the root of the securityfs filesystem.
 * @target: a pointer to a string containing the name of the symlink's target.
 *          If this parameter is %NULL, then the @iops parameter needs to be
 *          setup to handle .readlink and .get_link inode_operations.
 * @iops: a pointer to the struct inode_operations to use for the symlink. If
 *        this parameter is %NULL, then the default simple_symlink_inode
 *        operations will be used.
 *
 * This function creates a symlink in securityfs with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 *
 * If securityfs is not enabled in the kernel, the value %-ENODEV is
 * returned.
 */
struct dentry *securityfs_create_symlink(const char *name,
					 struct dentry *parent,
					 const char *target,
					 const struct inode_operations *iops)
{
	struct dentry *dent;
	char *link = NULL;

	if (target) {
		link = kstrdup(target, GFP_KERNEL);
		if (!link)
			return ERR_PTR(-ENOMEM);
	}
	dent = securityfs_create_dentry(name, S_IFLNK | 0444, parent,
					link, NULL, iops);
	if (IS_ERR(dent))
		kfree(link);

	return dent;
}
EXPORT_SYMBOL_GPL(securityfs_create_symlink);

/**
 * securityfs_remove - removes a file or directory from the securityfs filesystem
 *
 * @dentry: a pointer to a the dentry of the file or directory to be removed.
 *
 * This function removes a file or directory in securityfs that was previously
 * created with a call to another securityfs function (like
 * securityfs_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed. No automatic cleanup of files will happen when a module is
 * removed; you are responsible here.
 */
void securityfs_remove(struct dentry *dentry)
{
	struct user_namespace *ns;
	struct inode *dir;

	if (!dentry || IS_ERR(dentry))
		return;

	ns = dentry->d_sb->s_user_ns;

	dir = d_inode(dentry->d_parent);
	inode_lock(dir);
	if (simple_positive(dentry)) {
		dget(dentry);
		if (d_is_dir(dentry))
			simple_rmdir(dir, dentry);
		else
			simple_unlink(dir, dentry);
		d_delete(dentry);
		dput(dentry);
	}
	inode_unlock(dir);
	if (ns == &init_user_ns)
		simple_release_fs(&init_securityfs_mount,
				  &init_securityfs_mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_remove);

#ifdef CONFIG_SECURITY
static struct dentry *lsm_dentry;
static ssize_t lsm_read(struct file *filp, char __user *buf, size_t count,
			loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, lsm_names,
		strlen(lsm_names));
}

static const struct file_operations lsm_ops = {
	.read = lsm_read,
	.llseek = generic_file_llseek,
};
#endif

static int __init securityfs_init(void)
{
	int retval;

	retval = sysfs_create_mount_point(kernel_kobj, "security");
	if (retval)
		return retval;

	retval = register_filesystem(&fs_type);
	if (retval) {
		sysfs_remove_mount_point(kernel_kobj, "security");
		return retval;
	}
#ifdef CONFIG_SECURITY
	lsm_dentry = securityfs_create_file("lsm", 0444, NULL, NULL,
						&lsm_ops);
#endif
	return 0;
}
core_initcall(securityfs_init);
