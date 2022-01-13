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
#include <linux/rwsem.h>
#include <linux/xattr.h>

#include <linux/kernfs.h>
#include <linux/fs_context.h>

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/cache.h>

#ifdef CONFIG_SMP
#define NR_KERNFS_LOCK_BITS (2 * (ilog2(NR_CPUS < 32 ? NR_CPUS : 32)))
#else
#define NR_KERNFS_LOCK_BITS     1
#endif

#define NR_KERNFS_LOCKS     (1 << NR_KERNFS_LOCK_BITS)

struct kernfs_open_node_lock {
	spinlock_t lock;
} ____cacheline_aligned_in_smp;

struct kernfs_open_file_mutex {
	struct mutex lock;
} ____cacheline_aligned_in_smp;

struct kernfs_iattr_rwsem {
	struct rw_semaphore rwsem;
} ____cacheline_aligned_in_smp;

/*
 * There's one kernfs_open_file for each open file and one kernfs_open_node
 * for each kernfs_node with one or more open files.
 *
 * kernfs_node->attr.open points to kernfs_open_node.  attr.open is
 * protected by open_node_locks[i].lock.
 *
 * filp->private_data points to seq_file whose ->private points to
 * kernfs_open_file.  kernfs_open_files are chained at
 * kernfs_open_node->files, which is protected by open_file_mutex[i].lock.
 *
 * kernfs_node->iattr points to kernfs_node's attributes  and is
 * protected by iattr_rwsem[i].rwsem
 * To reduce possible contention in sysfs access, arising due to single
 * locks, use an array of locks and use kernfs_node object address as
 * hash keys to get the index of these locks.
 */

struct kernfs_global_locks {
	struct kernfs_open_node_lock open_node_locks[NR_KERNFS_LOCKS];
	struct kernfs_open_file_mutex open_file_mutex[NR_KERNFS_LOCKS];
	struct kernfs_iattr_rwsem iattr_rwsem[NR_KERNFS_LOCKS];
};

static struct kernfs_global_locks kernfs_global_locks;

static inline spinlock_t *open_node_lock_ptr(struct kernfs_node *kn)
{
	int index = hash_ptr(kn, NR_KERNFS_LOCK_BITS);

	return &kernfs_global_locks.open_node_locks[index].lock;
}

static inline struct mutex *open_file_mutex_ptr(struct kernfs_node *kn)
{
	int index = hash_ptr(kn, NR_KERNFS_LOCK_BITS);

	return &kernfs_global_locks.open_file_mutex[index].lock;
}

static inline struct rw_semaphore *iattr_rwsem_ptr(struct kernfs_node *kn)
{
	int index = hash_ptr(kn, NR_KERNFS_LOCK_BITS);

	return &kernfs_global_locks.iattr_rwsem[index].rwsem;
}

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

	/* anchored at kernfs_root->supers, protected by kernfs_rwsem */
	struct list_head	node;
};
#define kernfs_info(SB) ((struct kernfs_super_info *)(SB->s_fs_info))

static inline struct kernfs_node *kernfs_dentry_node(struct dentry *dentry)
{
	if (d_really_is_negative(dentry))
		return NULL;
	return d_inode(dentry)->i_private;
}

static inline void kernfs_set_rev(struct kernfs_node *parent,
				  struct dentry *dentry)
{
	dentry->d_time = parent->dir.rev;
}

static inline void kernfs_inc_rev(struct kernfs_node *parent)
{
	parent->dir.rev++;
}

static inline bool kernfs_dir_changed(struct kernfs_node *parent,
				      struct dentry *dentry)
{
	if (parent->dir.rev != dentry->d_time)
		return true;
	return false;
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
extern struct rw_semaphore kernfs_rwsem;
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

#endif	/* __KERNFS_INTERNAL_H */
