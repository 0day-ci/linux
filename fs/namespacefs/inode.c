// SPDX-License-Identifier: GPL-2.0-only
/*
 * inode.c - part of namespacefs, pseudo filesystem for examining namespaces.
 *
 * Copyright 2021 VMware Inc, Yordan Karadzhov (VMware) <y.karadz@gmail.com>
 */

#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/namei.h>
#include <linux/fsnotify.h>
#include <linux/magic.h>
#include <linux/idr.h>
#include <linux/proc_ns.h>
#include <linux/seq_file.h>
#include <linux/pid_namespace.h>
#include <linux/utsname.h>

static struct vfsmount *namespacefs_mount;
static int namespacefs_mount_count;

static const struct super_operations namespacefs_super_operations = {
	.statfs		= simple_statfs,
};

#define S_IRALL (S_IRUSR | S_IRGRP | S_IROTH)
#define S_IXALL (S_IXUSR | S_IXGRP | S_IXOTH)

static int fill_super(struct super_block *sb, void *data, int silent)
{
	static const struct tree_descr files[] = {{""}};
	int err;

	err = simple_fill_super(sb, NAMESPACEFS_MAGIC, files);
	if (err)
		return err;

	sb->s_op = &namespacefs_super_operations;
	sb->s_root->d_inode->i_mode |= S_IRALL;

	return 0;
}

static struct dentry *ns_mount(struct file_system_type *fs_type,
			    int flags, const char *dev_name,
			    void *data)
{
	return mount_single(fs_type, flags, data, fill_super);
}

static struct file_system_type namespacefs_fs_type = {
	.name		= "namespacefs",
	.mount		= ns_mount,
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

static inline void release_namespacefs(void)
{
	simple_release_fs(&namespacefs_mount, &namespacefs_mount_count);
}

static inline struct inode *parent_inode(struct dentry *dentry)
{
	return dentry->d_parent->d_inode;
}

static struct inode *get_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	}
	return inode;
}

static inline void set_file_inode(struct inode *inode,
				   const struct file_operations *fops,
				   void *data)
{
	inode->i_fop = fops;
	inode->i_private = data;
	inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP;
}

static inline void set_dir_inode(struct inode *inode)
{
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_mode = S_IFDIR | S_IXALL | S_IRALL;
}

static inline int pin_fs(void)
{
	return simple_pin_fs(&namespacefs_fs_type,
			     &namespacefs_mount,
			     &namespacefs_mount_count);
}

static struct dentry *create(const char *name, struct dentry *parent,
			     const struct user_namespace *user_ns,
			     const struct file_operations *fops,
			     void *data)
{
	struct dentry *dentry = NULL;
	struct inode *inode;

	if (pin_fs())
		return ERR_PTR(-ESTALE);

	/*
	 * If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent)
		parent = namespacefs_mount->mnt_root;

	inode_lock(parent->d_inode);
	if (unlikely(IS_DEADDIR(parent->d_inode)))
		return ERR_PTR(-ESTALE);

	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry) || (!IS_ERR(dentry) && dentry->d_inode))
		goto fail;

	inode = get_inode(dentry->d_sb);
	if (unlikely(!inode))
		goto fail;

	inode->i_uid = user_ns->owner;
	inode->i_gid = user_ns->group;

	if (fops) {
		/* Create a file. */
		set_file_inode(inode, fops, data);
		d_instantiate(dentry, inode);
		fsnotify_create(parent_inode(dentry), dentry);
	} else {
		/* Create a directory. */
		set_dir_inode(inode);
		d_instantiate(dentry, inode);
		set_nlink(inode, 2);
		inc_nlink(parent_inode(dentry));
		fsnotify_mkdir(parent_inode(dentry), dentry);
	}

	inode_unlock(parent_inode(dentry));
	return dentry;

 fail:
	if(!IS_ERR_OR_NULL(dentry))
		dput(dentry);

	inode_unlock(parent->d_inode);
	release_namespacefs();

	return ERR_PTR(-ESTALE);
}

struct dentry *
namespacefs_create_file(const char *name, struct dentry *parent,
			const struct user_namespace *user_ns,
			const struct file_operations *fops,
			void *data)
{
	return create(name, parent, user_ns, fops, data);
}

struct dentry *
namespacefs_create_dir(const char *name, struct dentry *parent,
		       const struct user_namespace *user_ns)
{
	return create(name, parent, user_ns, NULL, NULL);
}

static void remove_one(struct dentry *d)
{
	release_namespacefs();
}

void namespacefs_remove_dir(struct dentry *dentry)
{
	if (IS_ERR_OR_NULL(dentry))
		return;

	if (pin_fs())
		return;

	simple_recursive_removal(dentry, remove_one);
	release_namespacefs();
}

struct idr_seq_context {
	struct idr	*idr;
	int		index;
};

static void *idr_seq_get_next(struct idr_seq_context *idr_ctx, loff_t *pos)
{
	void *next = idr_get_next(idr_ctx->idr, &idr_ctx->index);

	*pos = ++idr_ctx->index;
	return next;
}

static void *idr_seq_start(struct seq_file *m, loff_t *pos)
{
	struct idr_seq_context *idr_ctx = m->private;

	idr_lock(idr_ctx->idr);
	idr_ctx->index = *pos;
	return idr_seq_get_next(idr_ctx, pos);
}

static void *idr_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return idr_seq_get_next(m->private, pos);
}

static void idr_seq_stop(struct seq_file *m, void *p)
{
	struct idr_seq_context *idr_ctx = m->private;

	idr_unlock(idr_ctx->idr);
}

static int idr_seq_open(struct file *file, struct idr *idr,
			const struct seq_operations *ops)
{
	struct idr_seq_context *idr_ctx;

	idr_ctx = __seq_open_private(file, ops, sizeof(*idr_ctx));
	if (!idr_ctx)
		return -ENOMEM;

	idr_ctx->idr = idr;

	return 0;
}

static inline int pid_seq_show(struct seq_file *m, void *v)
{
	struct pid *pid = v;

	seq_printf(m, "%d\n", pid_nr(pid));
	return 0;
}

static const struct seq_operations pid_seq_ops = {
	.start		= idr_seq_start,
	.next		= idr_seq_next,
	.stop		= idr_seq_stop,
	.show		= pid_seq_show,
};

static int pid_seq_open(struct inode *inode, struct file *file)
{
	struct idr *idr = inode->i_private;

	return idr_seq_open(file, idr, &pid_seq_ops);
}

static const struct file_operations tasks_fops = {
	.open		= pid_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

static int create_inode_dir(struct ns_common *ns, struct dentry *parent_dentry,
			    const struct user_namespace *user_ns)
{
	char *dir = kasprintf(GFP_KERNEL, "%u", ns->inum);

	if (!dir)
		return -ENOMEM;

	ns->dentry = namespacefs_create_dir(dir, parent_dentry, user_ns);
	kfree(dir);
	if (IS_ERR(ns->dentry))
		return PTR_ERR(ns->dentry);

	return 0;
}

int namespacefs_create_pid_ns_dir(struct pid_namespace *ns)
{
	struct dentry *dentry;
	int err;

	err = create_inode_dir(&ns->ns, ns->parent->ns.dentry, ns->user_ns);
	if (err)
		return err;

	dentry = namespacefs_create_file("tasks", ns->ns.dentry, ns->user_ns,
					 &tasks_fops, &ns->idr);
	if (IS_ERR(dentry)) {
		dput(ns->ns.dentry);
		return PTR_ERR(dentry);
	}

	return 0;
}

void namespacefs_remove_pid_ns_dir(struct pid_namespace *ns)
{
	namespacefs_remove_dir(ns->ns.dentry);
}

#define _UNAME_N_FIELDS		6
#define _UNAME_MAX_LEN		((__NEW_UTS_LEN + 1) * _UNAME_N_FIELDS)

static ssize_t uts_ns_read(struct file *file, char __user *ubuf,
			   size_t count, loff_t *pos)
{
	struct new_utsname *name = file->private_data;
	char buff[_UNAME_MAX_LEN + 1];
	int n;

	n = snprintf(buff, _UNAME_MAX_LEN + 1,
		     "%s %s %s %s %s %s\n",
		     name->sysname,
		     name->nodename,
		     name->release,
		     name->version,
		     name->machine,
		     name->domainname);

	return simple_read_from_buffer(ubuf, count, pos, buff, n);
}

static const struct file_operations uts_fops = {
	.open = simple_open,
	.read = uts_ns_read,
	.llseek = default_llseek,
};

int namespacefs_create_uts_ns_dir(struct uts_namespace *ns)
{
	struct dentry *dentry;
	int err;

	err = create_inode_dir(&ns->ns, init_uts_ns.ns.dentry, ns->user_ns);
	if (err)
		return err;

	dentry = namespacefs_create_file("uname", ns->ns.dentry, ns->user_ns,
					 &uts_fops, &ns->name);
	if (IS_ERR(dentry)) {
		dput(ns->ns.dentry);
		return PTR_ERR(dentry);
	}

	return 0;
}

void namespacefs_remove_uts_ns_dir(struct uts_namespace *ns)
{
	namespacefs_remove_dir(ns->ns.dentry);
}

static int add_ns_dentry(struct ns_common *ns)
{
	struct dentry *dentry =
		namespacefs_create_dir(ns->ops->name, NULL, &init_user_ns);

	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	ns->dentry = dentry;

	return 0;
}

#define _NS_MOUNT_DIR	"namespaces"

static int __init namespacefs_init(void)
{
	int err;

	err = sysfs_create_mount_point(fs_kobj, _NS_MOUNT_DIR);
	if (err)
		goto fail;

	err = register_filesystem(&namespacefs_fs_type);
	if (err)
		goto rm_mount;

	err = add_ns_dentry(&init_pid_ns.ns);
	if (err)
		goto unreg;

	err = add_ns_dentry(&init_uts_ns.ns);
	if (err)
		goto unreg;

	return 0;

 unreg:
	unregister_filesystem(&namespacefs_fs_type);
 rm_mount:
	sysfs_remove_mount_point(fs_kobj, _NS_MOUNT_DIR);
 fail:
	return err;
}

fs_initcall(namespacefs_init);
