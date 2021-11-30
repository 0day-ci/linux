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

static struct vfsmount *securityfs_mount;
static int securityfs_mount_count;

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
	int error;

	error = simple_fill_super(sb, SECURITYFS_MAGIC, files);
	if (error)
		return error;

	sb->s_op = &securityfs_super_operations;

	return 0;
}

static int securityfs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, securityfs_fill_super);
}

static const struct fs_context_operations securityfs_context_ops = {
	.get_tree	= securityfs_get_tree,
};

static int securityfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &securityfs_context_ops;
	return 0;
}

static struct file_system_type securityfs_type = {
	.owner =	THIS_MODULE,
	.name =		"securityfs",
	.init_fs_context = securityfs_init_fs_context,
	.kill_sb =	kill_litter_super,
};

static int securityfs_ns_fill_super(struct super_block *sb, struct fs_context *fc)
{
	static const struct tree_descr files[] = {{""}};
	int error;

	error = simple_fill_super(sb, SECURITYFS_NS_MAGIC, files);
	if (error)
		return error;

	sb->s_op = &securityfs_super_operations;

	return 0;
}

static int securityfs_ns_get_tree(struct fs_context *fc)
{
	return get_tree_keyed(fc, securityfs_ns_fill_super, fc->user_ns);
}

static const struct fs_context_operations securityfs_ns_context_ops = {
	.get_tree	= securityfs_ns_get_tree,
};

static int securityfs_ns_init_fs_context(struct fs_context *fc)
{
	fc->ops = &securityfs_ns_context_ops;
	return 0;
}

static struct file_system_type securityfs_ns_type = {
	.owner			= THIS_MODULE,
	.name			= "securityfs_ns",
	.init_fs_context	= securityfs_ns_init_fs_context,
	.kill_sb		= kill_litter_super,
	.fs_flags		= FS_USERNS_MOUNT,
};

struct vfsmount *securityfs_ns_create_mount(struct user_namespace *user_ns)
{
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(&securityfs_ns_type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(user_ns);

	mnt = fc_mount(fc);
	put_fs_context(fc);
	return mnt;
}


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
 * @mount: a pointer to a pointer for existing vfsmount to use or for
 *         one to create
 * @mount_count: pointer to integer for mount_count that goes along with
 *               @mount
 *
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
					const struct inode_operations *iops,
					struct file_system_type *fs_type,
					struct vfsmount **mount, int *mount_count)
{
	struct dentry *dentry;
	struct inode *dir, *inode;
	int error;

	if (!(mode & S_IFMT))
		mode = (mode & S_IALLUGO) | S_IFREG;

	pr_debug("securityfs: creating file '%s'\n",name);

	error = simple_pin_fs(fs_type, mount, mount_count);
	if (error)
		return ERR_PTR(error);

	if (!parent)
		parent = (*mount)->mnt_root;

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
		inode->i_op = iops ? iops : &simple_dir_inode_operations;
		inode->i_fop = fops ? fops : &simple_dir_operations;
		inc_nlink(inode);
		inc_nlink(dir);
	} else if (S_ISLNK(mode)) {
		inode->i_op = iops ? iops : &simple_symlink_inode_operations;
		inode->i_link = data;
	} else {
		inode->i_fop = fops;
	}
	d_instantiate(dentry, inode);
	dget(dentry);
	inode_unlock(dir);
	return dentry;

out1:
	dput(dentry);
	dentry = ERR_PTR(error);
out:
	inode_unlock(dir);
	simple_release_fs(mount, mount_count);
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
	return securityfs_create_dentry(name, mode, parent, data, fops, NULL,
					&securityfs_type, &securityfs_mount,
					&securityfs_mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_create_file);

/**
 * securityfs_ns_create_file - create a file in the securityfs_ns filesystem
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the securityfs_ns filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 * @mount: Pointer to a pointer of a an existing vfsmount
 * @mount_count: The mount_count that goes along with the @mount
 *
 * This function creates a file in securityfs_ns with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_ns_remove() function when the file
 * is to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 */
struct dentry *securityfs_ns_create_file(const char *name, umode_t mode,
					 struct dentry *parent, void *data,
					 const struct file_operations *fops,
					 const struct inode_operations *iops,
					 struct vfsmount **mount, int *mount_count)
{
	return securityfs_create_dentry(name, mode, parent, data, fops, iops,
					&securityfs_ns_type, mount, mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_ns_create_file);

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
 * securityfs_ns_create_dir - create a directory in the securityfs_ns filesystem
 *
 * @name: a pointer to a string containing the name of the directory to
 *        create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          directory will be created in the root of the securityfs_ns filesystem.
 * @mount: Pointer to a pointer of a an existing vfsmount
 * @mount_count: The mount_count that goes along with the @mount
 *
 * This function creates a directory in securityfs_ns with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_ns_remove() function when the file
 * is to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 */
struct dentry *securityfs_ns_create_dir(const char *name, struct dentry *parent,
					const struct inode_operations *iops,
					struct vfsmount **mount, int *mount_count)
{
	return securityfs_ns_create_file(name, S_IFDIR | 0755, parent, NULL, NULL,
					 iops, mount, mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_ns_create_dir);

struct dentry *_securityfs_create_symlink(const char *name,
					  struct dentry *parent,
					  const char *target,
					  const struct inode_operations *iops,
					  struct file_system_type *fs_type,
					  struct vfsmount **mount, int *mount_count)
{
	struct dentry *dent;
	char *link = NULL;

	if (target) {
		link = kstrdup(target, GFP_KERNEL);
		if (!link)
			return ERR_PTR(-ENOMEM);
	}
	dent = securityfs_create_dentry(name, S_IFLNK | 0444, parent,
					link, NULL, iops, fs_type,
					mount, mount_count);
	if (IS_ERR(dent))
		kfree(link);

	return dent;
}

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
	return _securityfs_create_symlink(name, parent, target, iops,
					  &securityfs_type, &securityfs_mount,
					  &securityfs_mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_create_symlink);

/**
 * securityfs_ns_create_symlink - create a symlink in the securityfs_ns filesystem
 *
 * @name: a pointer to a string containing the name of the symlink to
 *        create.
 * @parent: a pointer to the parent dentry for the symlink.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          directory will be created in the root of the securityfs_ns filesystem.
 * @target: a pointer to a string containing the name of the symlink's target.
 *          If this parameter is %NULL, then the @iops parameter needs to be
 *          setup to handle .readlink and .get_link inode_operations.
 * @iops: a pointer to the struct inode_operations to use for the symlink. If
 *        this parameter is %NULL, then the default simple_symlink_inode
 *        operations will be used.
 * @mount: Pointer to a pointer of a an existing vfsmount
 * @mount_count: The mount_count that goes along with the @mount
 *
 * This function creates a symlink in securityfs_ns with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_ns_remove() function when the file
 * is to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 */
struct dentry *securityfs_ns_create_symlink(const char *name,
					    struct dentry *parent,
					    const char *target,
					    const struct inode_operations *iops,
					    struct vfsmount **mount, int *mount_count)
{
	return _securityfs_create_symlink(name, parent, target, iops,
					  &securityfs_ns_type, mount, mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_ns_create_symlink);

void _securityfs_remove(struct dentry *dentry, struct vfsmount **mount, int *mount_count)
{
	struct inode *dir;

	if (!dentry || IS_ERR(dentry))
		return;

	dir = d_inode(dentry->d_parent);
	inode_lock(dir);
	if (simple_positive(dentry)) {
		if (d_is_dir(dentry))
			simple_rmdir(dir, dentry);
		else
			simple_unlink(dir, dentry);
		dput(dentry);
	}
	inode_unlock(dir);
	simple_release_fs(mount, mount_count);
}

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
	_securityfs_remove(dentry, &securityfs_mount, &securityfs_mount_count);
}

EXPORT_SYMBOL_GPL(securityfs_remove);

/**
 * securityfs_ns_remove - removes a file or directory from the securityfs_ns filesystem
 *
 * @dentry: a pointer to a the dentry of the file or directory to be removed.
 * @mount: Pointer to a pointer of a an existing vfsmount
 * @mount_count: The mount_count that goes along with the @mount
 *
 * This function removes a file or directory in securityfs_ns that was previously
 * created with a call to another securityfs_ns function (like
 * securityfs_ns_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed. No automatic cleanup of files will happen when a module is
 * removed; you are responsible here.
 */
void securityfs_ns_remove(struct dentry *dentry, struct vfsmount **mount, int *mount_count)
{
	_securityfs_remove(dentry, mount, mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_ns_remove);

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

	retval = register_filesystem(&securityfs_type);
	if (retval)
		goto remove_mount;
	retval = register_filesystem(&securityfs_ns_type);
	if (retval)
		goto unregister_filesystem;
#ifdef CONFIG_SECURITY
	lsm_dentry = securityfs_create_file("lsm", 0444, NULL, NULL,
						&lsm_ops);
#endif
	return 0;

unregister_filesystem:
	unregister_filesystem(&securityfs_type);
remove_mount:
	sysfs_remove_mount_point(kernel_kobj, "security");

	return retval;
}
core_initcall(securityfs_init);
