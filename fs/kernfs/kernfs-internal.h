/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fs/kernfs/kernfs-internal.h - kernfs internal header file
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <teheo@suse.de>
 */

#ifndef __KERNFS_INTERNAL_H
#define __KERNFS_INTERNAL_H

#include <linux/lockdep.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/xattr.h>

#include <linux/kernfs.h>
#include <linux/fs_context.h>
#include <linux/stringify.h>

struct kernfs_iattrs {
	kuid_t			ia_uid;
	kgid_t			ia_gid;
	struct timespec64	ia_atime;
	struct timespec64	ia_mtime;
	struct timespec64	ia_ctime;

	struct simple_xattrs	xattrs;
	atomic_t		nr_user_xattrs;
	atomic_t		user_xattr_size;
};

/* +1 to avoid triggering overflow warning when negating it */
#define KN_DEACTIVATED_BIAS		(INT_MIN + 1)

/* KERNFS_TYPE_MASK and types are defined in include/linux/kernfs.h */

/**
 * kernfs_root - find out the kernfs_root a kernfs_node belongs to
 * @kn: kernfs_node of interest
 *
 * Return the kernfs_root @kn belongs to.
 */
static inline struct kernfs_root *kernfs_root(struct kernfs_node *kn)
{
	/* if parent exists, it's always a dir; otherwise, @sd is a dir */
	if (kn->parent)
		kn = kn->parent;
	return kn->dir.root;
}

/*
 * mount.c
 */
struct kernfs_super_info {
	struct super_block	*sb;

	/*
	 * The root associated with this super_block.  Each super_block is
	 * identified by the root and ns it's associated with.
	 */
	struct kernfs_root	*root;

	/*
	 * Each sb is associated with one namespace tag, currently the
	 * network namespace of the task which mounted this kernfs
	 * instance.  If multiple tags become necessary, make the following
	 * an array and compare kernfs_node tag against every entry.
	 */
	const void		*ns;

	/* anchored at kernfs_root->supers, protected by kernfs_mutex */
	struct list_head	node;
};
#define kernfs_info(SB) ((struct kernfs_super_info *)(SB->s_fs_info))

static inline struct kernfs_node *kernfs_dentry_node(struct dentry *dentry)
{
	if (d_really_is_negative(dentry))
		return NULL;
	return d_inode(dentry)->i_private;
}

extern const struct super_operations kernfs_sops;
extern struct kmem_cache *kernfs_node_cache, *kernfs_iattrs_cache;

/*
 * inode.c
 */
extern const struct xattr_handler *kernfs_xattr_handlers[];
void kernfs_evict_inode(struct inode *inode);
int kernfs_iop_permission(struct user_namespace *mnt_userns,
			  struct inode *inode, int mask);
int kernfs_iop_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		       struct iattr *iattr);
int kernfs_iop_getattr(struct user_namespace *mnt_userns,
		       const struct path *path, struct kstat *stat,
		       u32 request_mask, unsigned int query_flags);
ssize_t kernfs_iop_listxattr(struct dentry *dentry, char *buf, size_t size);
int __kernfs_setattr(struct kernfs_node *kn, const struct iattr *iattr);

/*
 * dir.c
 */
extern struct mutex kernfs_mutex;
extern const struct dentry_operations kernfs_dops;
extern const struct file_operations kernfs_dir_fops;
extern const struct inode_operations kernfs_dir_iops;

struct kernfs_node *kernfs_get_active(struct kernfs_node *kn);
void kernfs_put_active(struct kernfs_node *kn);
int kernfs_add_one(struct kernfs_node *kn);
struct kernfs_node *kernfs_new_node(struct kernfs_node *parent,
				    const char *name, umode_t mode,
				    kuid_t uid, kgid_t gid,
				    unsigned flags);

/*
 * file.c
 */
extern const struct file_operations kernfs_file_fops;

void kernfs_drain_open_files(struct kernfs_node *kn);

/*
 * symlink.c
 */
extern const struct inode_operations kernfs_symlink_iops;

/*
 * failure-injection.c
 */
#ifdef CONFIG_FAIL_KERNFS_KNOBS

/**
 * struct kernfs_fop_write_iter_fail - how kernfs_fop_write_iter_fail fails
 *
 * This lets you configure what part of kernfs_fop_write_iter() should behave
 * in a specific way to allow userspace to capture possible failures in
 * kernfs. The wait knobs are allowed to let you design capture possible
 * race conditions which would otherwise be difficult to reproduce. A
 * secondary driver would tell kernfs's wait completion when it is done.
 *
 * The point to the wait completion failure injection tests are to confirm
 * that the kernfs active refcount suffice to ensure other objects in other
 * layers are also gauranteed to exist, even they are opaque to kernfs. This
 * includes kobjects, devices, and other objects built on top of this, like
 * the block layer when using sysfs block device attributes.
 *
 * @wait_at_start: waits for completion from a third party at the start of
 *	the routine.
 * @wait_before_mutex: waits for completion from a third party before we
 *	are allowed to continue before the of->mutex is held.
 * @wait_after_mutex: waits for completion from a third party after we
 *	have held the of->mutex.
 * @wait_after_active: waits for completion from a thid party after we
 *	have refcounted the struct kernfs_node.
 */
struct kernfs_fop_write_iter_fail {
	bool wait_at_start;
	bool wait_before_mutex;
	bool wait_after_mutex;
	bool wait_after_active;
};

/**
 * struct kernfs_config_fail - kernfs configuration for failure injection
 *
 * You can kernfs failure injection on boot, and in particular we currently
 * only support failures for kernfs_fop_write_iter(). However, we don't
 * want to always enable errors on this call when failure injection is enabled
 * as this routine is used by many parts of the kernel for proper functionality.
 * The compromise we make is we let userspace start enabling which parts it
 * wants to fail after boot, if and only if failure injection has been enabled.
 *
 * @kernfs_fop_write_iter_fail: configuration for how we want to allow
 *	for failure injection on kernfs_fop_write_iter()
 * @sleep_after_wait_ms: how many ms to wait after completion is received.
 */
struct kernfs_config_fail {
	struct kernfs_fop_write_iter_fail kernfs_fop_write_iter_fail;
	u32 sleep_after_wait_ms;
};

extern struct kernfs_config_fail kernfs_config_fail;

void kernfs_init_failure_injection(void);
#define __kernfs_config_wait_var(func, when) \
	(kernfs_config_fail.  func  ## _fail.wait_  ## when)
#define __kernfs_debug_should_wait_func_name(func) __kernfs_debug_should_wait_## func

#define kernfs_debug_should_wait(func, when) \
	__kernfs_debug_should_wait_func_name(func)(__kernfs_config_wait_var(func, when))
int __kernfs_debug_should_wait_kernfs_fop_write_iter(bool evaluate);
void kernfs_debug_wait(void);
#else
static inline void kernfs_init_failure_injection(void) {}
#define kernfs_debug_should_wait(when) (false)
static inline void kernfs_debug_wait(void) {}
#endif

#endif	/* __KERNFS_INTERNAL_H */
