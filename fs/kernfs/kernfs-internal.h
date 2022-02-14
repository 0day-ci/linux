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

/*
 * kernfs_rwsem locking pattern:
 *
 * KERNFS_RWSEM_LOCK_SELF: lock kernfs_node only.
 * KERNFS_RWSEM_LOCK_SELF_AND_PARENT: lock kernfs_node and its parent.
 */
enum kernfs_rwsem_lock_pattern {
	KERNFS_RWSEM_LOCK_SELF,
	KERNFS_RWSEM_LOCK_SELF_AND_PARENT
};

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
 * kernfs locks
 */
extern struct kernfs_global_locks *kernfs_locks;

static inline struct mutex *kernfs_open_file_mutex_ptr(struct kernfs_node *kn)
{
	int idx = hash_ptr(kn, NR_KERNFS_LOCK_BITS);

	return &kernfs_locks->open_file_mutex[idx].lock;
}

static inline struct mutex *kernfs_open_file_mutex_lock(struct kernfs_node *kn)
{
	struct mutex *lock;

	lock = kernfs_open_file_mutex_ptr(kn);

	mutex_lock(lock);

	return lock;
}

static inline spinlock_t *
kernfs_open_node_spinlock_ptr(struct kernfs_node *kn)
{
	int idx = hash_ptr(kn, NR_KERNFS_LOCK_BITS);

	return &kernfs_locks->open_node_locks[idx].lock;
}

static inline spinlock_t *
kernfs_open_node_spinlock(struct kernfs_node *kn)
{
	spinlock_t *lock;

	lock = kernfs_open_node_spinlock_ptr(kn);

	spin_lock_irq(lock);

	return lock;
}

static inline struct rw_semaphore *kernfs_rwsem_ptr(struct kernfs_node *kn)
{
	int idx = hash_ptr(kn, NR_KERNFS_LOCK_BITS);

	return &kernfs_locks->kernfs_rwsem[idx];
}

static inline void kernfs_rwsem_assert_held(struct kernfs_node *kn)
{
	lockdep_assert_held(kernfs_rwsem_ptr(kn));
}

static inline void kernfs_rwsem_assert_held_write(struct kernfs_node *kn)
{
	lockdep_assert_held_write(kernfs_rwsem_ptr(kn));
}

static inline void kernfs_rwsem_assert_held_read(struct kernfs_node *kn)
{
	lockdep_assert_held_read(kernfs_rwsem_ptr(kn));
}

/**
 * down_write_kernfs_rwsem_for_two_nodes() - Acquire hashed rwsem for 2 nodes
 *
 * @kn1: kernfs_node for which hashed rwsem needs to be taken
 * @kn2: kernfs_node for which hashed rwsem needs to be taken
 *
 * In certain cases we need to acquire hashed rwsem for 2 nodes that don't have a
 * parent child relationship. This is one of the cases of nested locking involving
 * hashed rwsem and rwsem with lower address is acquired first.
 */
static inline void down_write_kernfs_rwsem_for_two_nodes(struct kernfs_node *kn1,
							 struct kernfs_node *kn2)
{
	struct rw_semaphore *rwsem1 = kernfs_rwsem_ptr(kn1);
	struct rw_semaphore *rwsem2 = kernfs_rwsem_ptr(kn2);

	if (rwsem1 == rwsem2)
		down_write_nested(rwsem1, 0);
	else {
		if (rwsem1 < rwsem2) {
			down_write_nested(rwsem1, 0);
			down_write_nested(rwsem2, 1);
		} else {
			down_write_nested(rwsem2, 0);
			down_write_nested(rwsem1, 1);
		}
	}
	kernfs_get(kn1);
	kernfs_get(kn2);
}

/**
 * up_write_kernfs_rwsem_for_two_nodes() - Release hashed rwsem for 2 nodes
 *
 * @kn1: kernfs_node for which hashed rwsem needs to be released
 * @kn2: kernfs_node for which hashed rwsem needs to be released
 *
 * In case of nested locking, rwsem with higher address is released first.
 */
static inline void up_write_kernfs_rwsem_for_two_nodes(struct kernfs_node *kn1,
						       struct kernfs_node *kn2)
{
	struct rw_semaphore *rwsem1 = kernfs_rwsem_ptr(kn1);
	struct rw_semaphore *rwsem2 = kernfs_rwsem_ptr(kn2);

	if (rwsem1 == rwsem2)
		up_write(rwsem1);
	else {
		if (rwsem1 > rwsem2) {
			up_write(rwsem1);
			up_write(rwsem2);
		} else {
			up_write(rwsem2);
			up_write(rwsem1);
		}
	}

	kernfs_put(kn1);
	kernfs_put(kn2);
}

/**
 * down_read_kernfs_rwsem_for_two_nodes() - Acquire hashed rwsem for 2 nodes
 *
 * @kn1: kernfs_node for which hashed rwsem needs to be taken
 * @kn2: kernfs_node for which hashed rwsem needs to be taken
 *
 * In certain cases we need to acquire hashed rwsem for 2 nodes that don't have a
 * parent child relationship. This is one of the cases of nested locking involving
 * hashed rwsem and rwsem with lower address is acquired first.
 */
static inline void down_read_kernfs_rwsem_for_two_nodes(struct kernfs_node *kn1,
							struct kernfs_node *kn2)
{
	struct rw_semaphore *rwsem1 = kernfs_rwsem_ptr(kn1);
	struct rw_semaphore *rwsem2 = kernfs_rwsem_ptr(kn2);

	if (rwsem1 == rwsem2)
		down_read_nested(rwsem1, 0);
	else {
		if (rwsem1 < rwsem2) {
			down_read_nested(rwsem1, 0);
			down_read_nested(rwsem2, 1);
		} else {
			down_read_nested(rwsem2, 0);
			down_read_nested(rwsem1, 1);
		}
	}
	kernfs_get(kn1);
	kernfs_get(kn2);
}

/**
 * up_read_kernfs_rwsem_for_two_nodes() - Release hashed rwsem for 2 nodes
 *
 * @kn1: kernfs_node for which hashed rwsem needs to be released
 * @kn2: kernfs_node for which hashed rwsem needs to be released
 *
 * In case of nested locking, rwsem with higher address is released first.
 */
static inline void up_read_kernfs_rwsem_for_two_nodes(struct kernfs_node *kn1,
						       struct kernfs_node *kn2)
{
	struct rw_semaphore *rwsem1 = kernfs_rwsem_ptr(kn1);
	struct rw_semaphore *rwsem2 = kernfs_rwsem_ptr(kn2);

	if (rwsem1 == rwsem2)
		up_read(rwsem1);
	else {
		if (rwsem1 > rwsem2) {
			up_read(rwsem1);
			up_read(rwsem2);
		} else {
			up_read(rwsem2);
			up_read(rwsem1);
		}
	}

	kernfs_put(kn1);
	kernfs_put(kn2);
}

/**
 * down_write_kernfs_rwsem() - Acquire hashed rwsem
 *
 * @kn: kernfs_node for which hashed rwsem needs to be taken
 * @ptrn: locking pattern i.e whether to lock only given node or to lock
 *	  node and its parent as well
 *
 * In case of nested locking, rwsem with lower address is acquired first.
 *
 * Return: void
 */
static inline void down_write_kernfs_rwsem(struct kernfs_node *kn,
				      enum kernfs_rwsem_lock_pattern ptrn)
{
	struct rw_semaphore *p_rwsem = NULL;
	struct rw_semaphore *rwsem = kernfs_rwsem_ptr(kn);
	int lock_parent = 0;

	if (ptrn == KERNFS_RWSEM_LOCK_SELF_AND_PARENT && kn->parent)
		lock_parent = 1;

	if (lock_parent)
		p_rwsem = kernfs_rwsem_ptr(kn->parent);

	if (!lock_parent || rwsem == p_rwsem) {
		down_write_nested(rwsem, 0);
		kernfs_get(kn);
		kn->unlock_parent = 0;
	} else {
		/**
		 * In case of nested locking, locks are taken in order of their
		 * addresses. lock with lower address is taken first, followed
		 * by lock with higher address.
		 */
		if (rwsem < p_rwsem) {
			down_write_nested(rwsem, 0);
			down_write_nested(p_rwsem, 1);
		} else {
			down_write_nested(p_rwsem, 0);
			down_write_nested(rwsem, 1);
		}
		kernfs_get(kn);
		kernfs_get(kn->parent);
		kn->unlock_parent = 1;
	}
}

/**
 * up_write_kernfs_rwsem() - Release hashed rwsem
 *
 * @kn: kernfs_node for which hashed rwsem was taken
 *
 * In case of nested locking, rwsem with higher address is released first.
 *
 * Return: void
 */
static inline void up_write_kernfs_rwsem(struct kernfs_node *kn)
{
	struct rw_semaphore *p_rwsem = NULL;
	struct rw_semaphore *rwsem = kernfs_rwsem_ptr(kn);

	if (kn->unlock_parent) {
		kn->unlock_parent = 0;
		p_rwsem = kernfs_rwsem_ptr(kn->parent);
		if (rwsem > p_rwsem) {
			up_write(rwsem);
			up_write(p_rwsem);
		} else {
			up_write(p_rwsem);
			up_write(rwsem);
		}
		kernfs_put(kn->parent);
	} else
		up_write(rwsem);

	kernfs_put(kn);
}

/**
 * down_read_kernfs_rwsem() - Acquire hashed rwsem
 *
 * @kn: kernfs_node for which hashed rwsem needs to be taken
 * @ptrn: locking pattern i.e whether to lock only given node or to lock
 *	  node and its parent as well
 *
 * In case of nested locking, rwsem with lower address is acquired first.
 *
 * Return: void
 */
static inline void down_read_kernfs_rwsem(struct kernfs_node *kn,
				      enum kernfs_rwsem_lock_pattern ptrn)
{
	struct rw_semaphore *p_rwsem = NULL;
	struct rw_semaphore *rwsem = kernfs_rwsem_ptr(kn);
	int lock_parent = 0;

	if (ptrn == KERNFS_RWSEM_LOCK_SELF_AND_PARENT && kn->parent)
		lock_parent = 1;

	if (lock_parent)
		p_rwsem = kernfs_rwsem_ptr(kn->parent);

	if (!lock_parent || rwsem == p_rwsem) {
		down_read_nested(rwsem, 0);
		kernfs_get(kn);
		kn->unlock_parent = 0;
	} else {
		/**
		 * In case of nested locking, locks are taken in order of their
		 * addresses. lock with lower address is taken first, followed
		 * by lock with higher address.
		 */
		if (rwsem < p_rwsem) {
			down_read_nested(rwsem, 0);
			down_read_nested(p_rwsem, 1);
		} else {
			down_read_nested(p_rwsem, 0);
			down_read_nested(rwsem, 1);
		}
		kernfs_get(kn);
		kernfs_get(kn->parent);
		kn->unlock_parent = 1;
	}
}

/**
 * up_read_kernfs_rwsem() - Release hashed rwsem
 *
 * @kn: kernfs_node for which hashed rwsem was taken
 *
 * In case of nested locking, rwsem with higher address is released first.
 *
 * Return: void
 */
static inline void up_read_kernfs_rwsem(struct kernfs_node *kn)
{
	struct rw_semaphore *p_rwsem = NULL;
	struct rw_semaphore *rwsem = kernfs_rwsem_ptr(kn);

	if (kn->unlock_parent) {
		kn->unlock_parent = 0;
		p_rwsem = kernfs_rwsem_ptr(kn->parent);
		if (rwsem > p_rwsem) {
			up_read(rwsem);
			up_read(p_rwsem);
		} else {
			up_read(p_rwsem);
			up_read(rwsem);
		}
		kernfs_put(kn->parent);
	} else
		up_read(rwsem);

	kernfs_put(kn);
}

static inline void kernfs_swap_rwsems(struct rw_semaphore **array, int i, int j)
{
	struct rw_semaphore *tmp;

	tmp = array[i];
	array[i] = array[j];
	array[j] = tmp;
}

static inline void kernfs_sort_rwsems(struct rw_semaphore **array)
{
	if (array[0] > array[1])
		kernfs_swap_rwsems(array, 0, 1);

	if (array[0] > array[2])
		kernfs_swap_rwsems(array, 0, 2);

	if (array[1] > array[2])
		kernfs_swap_rwsems(array, 1, 2);
}

/**
 * down_write_kernfs_rwsem_rename_ns() - take hashed rwsem during
 * rename or similar operations.
 *
 * @kn: kernfs_node of interest
 * @current_parent: current parent of kernfs_node of interest
 * @new_parent: about to become new parent of kernfs_node
 *
 * During rename or similar operations the parent of a node changes,
 * and this means we will see different parents of a kernfs_node at
 * the time of taking and releasing its or its parent's hashed rwsem.
 * This function separately takes locks corresponding to node, and
 * corresponding to its current and future parents (if needed).
 *
 * Return: void
 */
static inline void down_write_kernfs_rwsem_rename_ns(struct kernfs_node *kn,
					struct kernfs_node *current_parent,
					struct kernfs_node *new_parent)
{
	struct rw_semaphore *array[3];

	array[0] = kernfs_rwsem_ptr(kn);
	array[1] = kernfs_rwsem_ptr(current_parent);
	array[2] = kernfs_rwsem_ptr(new_parent);

	if (array[0] == array[1] && array[0] == array[2]) {
		/* All 3 nodes hash to same rwsem */
		down_write_nested(array[0], 0);
	} else {
		/**
		 * All 3 nodes are not hashing to the same rwsem, so sort the
		 * array.
		 */
		kernfs_sort_rwsems(array);

		if (array[0] == array[1] || array[1] == array[2]) {
			/**
			 * Two nodes hash to same rwsem, and these
			 * will occupy consecutive places in array after
			 * sorting.
			 */
			down_write_nested(array[0], 0);
			down_write_nested(array[2], 1);
		} else {
			/* All 3 nodes hashe to different rwsems */
			down_write_nested(array[0], 0);
			down_write_nested(array[1], 1);
			down_write_nested(array[2], 2);
		}
	}

	kernfs_get(kn);
	kernfs_get(current_parent);
	kernfs_get(new_parent);
}

/**
 * up_write_kernfs_rwsem_rename_ns() - release hashed rwsem during
 * rename or similar operations.
 *
 * @kn: kernfs_node of interest
 * @current_parent: current parent of kernfs_node of interest
 * @old_parent: old parent of kernfs_node
 *
 * During rename or similar operations the parent of a node changes,
 * and this means we will see different parents of a kernfs_node at
 * the time of taking and releasing its or its parent's hashed rwsem.
 * This function separately releases locks corresponding to node, and
 * corresponding to its current and old parents (if needed).
 *
 * Return: void
 */
static inline void up_write_kernfs_rwsem_rename_ns(struct kernfs_node *kn,
					struct kernfs_node *current_parent,
					struct kernfs_node *old_parent)
{
	struct rw_semaphore *array[3];

	array[0] = kernfs_rwsem_ptr(kn);
	array[1] = kernfs_rwsem_ptr(current_parent);
	array[2] = kernfs_rwsem_ptr(old_parent);

	if (array[0] == array[1] && array[0] == array[2]) {
		/* All 3 nodes hash to same rwsem */
		up_write(array[0]);
	} else {
		/**
		 * All 3 nodes are not hashing to the same rwsem, so sort the
		 * array.
		 */
		kernfs_sort_rwsems(array);

		if (array[0] == array[1] || array[1] == array[2]) {
			/**
			 * Two nodes hash to same rwsem, and these
			 * will occupy consecutive places in array after
			 * sorting.
			 */
			up_write(array[2]);
			up_write(array[0]);
		} else {
			/* All 3 nodes hashe to different rwsems */
			up_write(array[2]);
			up_write(array[1]);
			up_write(array[0]);
		}
	}

	kernfs_put(old_parent);
	kernfs_put(current_parent);
	kernfs_put(kn);
}

/**
 * down_read_kernfs_rwsem_rename_ns() - take hashed rwsem during
 * rename or similar operations.
 *
 * @kn: kernfs_node of interest
 * @current_parent: current parent of kernfs_node of interest
 * @new_parent: about to become new parent of kernfs_node
 *
 * During rename or similar operations the parent of a node changes,
 * and this means we will see different parents of a kernfs_node at
 * the time of taking and releasing its or its parent's hashed rwsem.
 * This function separately takes locks corresponding to node, and
 * corresponding to its current and future parents (if needed).
 *
 * Return: void
 */
static inline void down_read_kernfs_rwsem_rename_ns(struct kernfs_node *kn,
					struct kernfs_node *current_parent,
					struct kernfs_node *new_parent)
{
	struct rw_semaphore *array[3];

	array[0] = kernfs_rwsem_ptr(kn);
	array[1] = kernfs_rwsem_ptr(current_parent);
	array[2] = kernfs_rwsem_ptr(new_parent);

	if (array[0] == array[1] && array[0] == array[2]) {
		/* All 3 nodes hash to same rwsem */
		down_read_nested(array[0], 0);
	} else {
		/**
		 * All 3 nodes are not hashing to the same rwsem, so sort the
		 * array.
		 */
		kernfs_sort_rwsems(array);

		if (array[0] == array[1] || array[1] == array[2]) {
			/**
			 * Two nodes hash to same rwsem, and these
			 * will occupy consecutive places in array after
			 * sorting.
			 */
			down_read_nested(array[0], 0);
			down_read_nested(array[2], 1);
		} else {
			/* All 3 nodes hashe to different rwsems */
			down_read_nested(array[0], 0);
			down_read_nested(array[1], 1);
			down_read_nested(array[2], 2);
		}
	}

	kernfs_get(kn);
	kernfs_get(current_parent);
	kernfs_get(new_parent);
}

/**
 * up_read_kernfs_rwsem_rename_ns() - release hashed rwsem during
 * rename or similar operations.
 *
 * @kn: kernfs_node of interest
 * @current_parent: current parent of kernfs_node of interest
 * @old_parent: old parent of kernfs_node
 *
 * During rename or similar operations the parent of a node changes,
 * and this means we will see different parents of a kernfs_node at
 * the time of taking and releasing its or its parent's hashed rwsem.
 * This function separately releases locks corresponding to node, and
 * corresponding to its current and old parents (if needed).
 *
 * Return: void
 */
static inline void up_read_kernfs_rwsem_rename_ns(struct kernfs_node *kn,
					struct kernfs_node *current_parent,
					struct kernfs_node *old_parent)
{
	struct rw_semaphore *array[3];

	array[0] = kernfs_rwsem_ptr(kn);
	array[1] = kernfs_rwsem_ptr(current_parent);
	array[2] = kernfs_rwsem_ptr(old_parent);

	if (array[0] == array[1] && array[0] == array[2]) {
		/* All 3 nodes hash to same rwsem */
		up_read(array[0]);
	} else {
		/**
		 * All 3 nodes are not hashing to the same rwsem, so sort the
		 * array.
		 */
		kernfs_sort_rwsems(array);

		if (array[0] == array[1] || array[1] == array[2]) {
			/**
			 * Two nodes hash to same rwsem, and these
			 * will occupy consecutive places in array after
			 * sorting.
			 */
			up_read(array[2]);
			up_read(array[0]);
		} else {
			/* All 3 nodes hashe to different rwsems */
			up_read(array[2]);
			up_read(array[1]);
			up_read(array[0]);
		}
	}

	kernfs_put(old_parent);
	kernfs_put(current_parent);
	kernfs_put(kn);
}

#endif	/* __KERNFS_INTERNAL_H */
