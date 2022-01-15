// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/refcount.h>
#include "rxe.h"

#define RXE_POOL_ALIGN		(16)

static const struct rxe_type_info {
	const char *name;
	size_t size;
	size_t elem_offset;
	void (*cleanup)(struct rxe_pool_elem *obj);
	enum rxe_pool_flags flags;
	u32 min_index;
	u32 max_index;
	size_t key_offset;
	size_t key_size;
} rxe_type_info[RXE_NUM_TYPES] = {
	[RXE_TYPE_UC] = {
		.name		= "rxe-uc",
		.size		= sizeof(struct rxe_ucontext),
		.elem_offset	= offsetof(struct rxe_ucontext, elem),
	},
	[RXE_TYPE_PD] = {
		.name		= "rxe-pd",
		.size		= sizeof(struct rxe_pd),
		.elem_offset	= offsetof(struct rxe_pd, elem),
	},
	[RXE_TYPE_AH] = {
		.name		= "rxe-ah",
		.size		= sizeof(struct rxe_ah),
		.elem_offset	= offsetof(struct rxe_ah, elem),
		.flags		= RXE_POOL_INDEX,
		.min_index	= RXE_MIN_AH_INDEX,
		.max_index	= RXE_MAX_AH_INDEX,
	},
	[RXE_TYPE_SRQ] = {
		.name		= "rxe-srq",
		.size		= sizeof(struct rxe_srq),
		.elem_offset	= offsetof(struct rxe_srq, elem),
		.flags		= RXE_POOL_INDEX,
		.min_index	= RXE_MIN_SRQ_INDEX,
		.max_index	= RXE_MAX_SRQ_INDEX,
	},
	[RXE_TYPE_QP] = {
		.name		= "rxe-qp",
		.size		= sizeof(struct rxe_qp),
		.elem_offset	= offsetof(struct rxe_qp, elem),
		.cleanup	= rxe_qp_cleanup,
		.flags		= RXE_POOL_INDEX,
		.min_index	= RXE_MIN_QP_INDEX,
		.max_index	= RXE_MAX_QP_INDEX,
	},
	[RXE_TYPE_CQ] = {
		.name		= "rxe-cq",
		.size		= sizeof(struct rxe_cq),
		.elem_offset	= offsetof(struct rxe_cq, elem),
		.cleanup	= rxe_cq_cleanup,
	},
	[RXE_TYPE_MR] = {
		.name		= "rxe-mr",
		.size		= sizeof(struct rxe_mr),
		.elem_offset	= offsetof(struct rxe_mr, elem),
		.cleanup	= rxe_mr_cleanup,
		.flags		= RXE_POOL_INDEX | RXE_POOL_ALLOC,
		.min_index	= RXE_MIN_MR_INDEX,
		.max_index	= RXE_MAX_MR_INDEX,
	},
	[RXE_TYPE_MW] = {
		.name		= "rxe-mw",
		.size		= sizeof(struct rxe_mw),
		.elem_offset	= offsetof(struct rxe_mw, elem),
		.cleanup	= rxe_mw_cleanup,
		.flags		= RXE_POOL_INDEX,
		.min_index	= RXE_MIN_MW_INDEX,
		.max_index	= RXE_MAX_MW_INDEX,
	},
};

void rxe_pool_init(struct rxe_dev *rxe, struct rxe_pool *pool,
		   enum rxe_elem_type type, unsigned int max_elem)
{
	const struct rxe_type_info *info = &rxe_type_info[type];

	memset(pool, 0, sizeof(*pool));

	pool->rxe		= rxe;
	pool->name		= info->name;
	pool->type		= type;
	pool->max_elem		= max_elem;
	pool->elem_size		= ALIGN(info->size, RXE_POOL_ALIGN);
	pool->elem_offset	= info->elem_offset;
	pool->flags		= info->flags;
	pool->cleanup		= info->cleanup;

	atomic_set(&pool->num_elem, 0);

	/* used for pools with RXE_POOL_INDEX and
	 * the xa spinlock for other pools
	 */
	xa_init_flags(&pool->xa, XA_FLAGS_ALLOC);
	pool->limit.max = info->max_index;
	pool->limit.min = info->min_index;
}

void rxe_pool_cleanup(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;

	if (atomic_read(&pool->num_elem) > 0)
		pr_warn("%s pool destroyed with unfree'd elem\n",
			pool->name);

	if (pool->flags & RXE_POOL_INDEX) {
		unsigned long index = 0;
		unsigned long max = ULONG_MAX;
		unsigned int elem_count = 0;
		unsigned int free_count = 0;

		do {
			elem = xa_find(&pool->xa, &index, max, XA_PRESENT);
			if (elem) {
				elem_count++;
				xa_erase(&pool->xa, index);
				if (pool->flags & RXE_POOL_ALLOC) {
					kfree(elem->obj);
					free_count++;
				}
			}

		} while (elem);

		if (elem_count || free_count)
			pr_warn("Freed %d indices, %d objects\n",
				elem_count, free_count);
	}

	xa_destroy(&pool->xa);
}

/**
 * rxe_alloc() - create a new rxe object
 * @pool: rxe object pool
 *
 * Adds a new object to object pool allocating the storage here.
 * If object pool has an index add elem to xarray.
 *
 * Returns: the object on success else NULL
 */
void *rxe_alloc(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
	void *obj;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto err_cnt;

	obj = kzalloc(pool->elem_size, GFP_KERNEL);
	if (!obj)
		goto err_cnt;

	elem = (struct rxe_pool_elem *)((u8 *)obj + pool->elem_offset);

	elem->pool = pool;
	elem->obj = obj;
	kref_init(&elem->ref_cnt);

	if (pool->flags & RXE_POOL_INDEX) {
		int err = xa_alloc_cyclic_bh(&pool->xa, &elem->index,
					     elem, pool->limit,
					     &pool->next, GFP_KERNEL);
		if (err)
			goto err_free;
	}

	return obj;

err_free:
	kfree(obj);
err_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

/**
 * __rxe_add_to_pool() - add pool element to object pool
 * @pool: rxe object pool
 * @elem: a pool element embedded in a rxe object
 *
 * Adds a rxe pool element to object pool when the storage is
 * allocated by rdma/core before calling the verb that creates
 * the object. If object pool has an index add elem to xarray.
 *
 * The rxe_add_to_pool() macro converts the 2nd argument from
 * an object to a pool element embedded in the object.
 *
 * Returns: 0 on success else an error
 */
int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem)
{
	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto err_cnt;

	elem->pool = pool;
	elem->obj = (u8 *)elem - pool->elem_offset;
	kref_init(&elem->ref_cnt);

	if (pool->flags & RXE_POOL_INDEX) {
		int err = xa_alloc_cyclic_bh(&pool->xa, &elem->index,
					     elem, pool->limit,
					     &pool->next, GFP_KERNEL);
		if (err)
			goto err_cnt;
	}

	return 0;

err_cnt:
	atomic_dec(&pool->num_elem);
	return -EINVAL;
}

/**
 * rxe_pool_get_index - lookup object from index
 * @pool: the object pool
 * @index: the index of the object
 *
 * Acquire the xa spinlock to make looking up the object from
 * its index atomic with the call to kref_get_unless_zero() to avoid
 * a race condition with a second thread deleting the object
 * before we can acquire the reference.
 *
 * Returns: the object if the index exists in the pool
 * and the reference count on the object is positive
 * else NULL
 */
void *rxe_pool_get_index(struct rxe_pool *pool, u32 index)
{
	struct rxe_pool_elem *elem;
	void *obj;

	xa_lock_bh(&pool->xa);
	elem = xa_load(&pool->xa, index);
	if (elem && kref_get_unless_zero(&elem->ref_cnt))
		obj = elem->obj;
	else
		obj = NULL;
	xa_unlock_bh(&pool->xa);

	return obj;
}

/**
 * rxe_elem_release() - cleanup object
 * @kref: pointer to kref embedded in pool element
 *
 * The kref_put_lock() call in rxe_drop_ref() takes the
 * xa spinlock if the ref count goes to zero which is then
 * released here after removing the xarray entry to prevent
 * overlapping with rxe_get_index().
 */
static void rxe_elem_release(struct kref *kref)
	__releases(&pool->xa.xa_lock)
{
	struct rxe_pool_elem *elem =
		container_of(kref, struct rxe_pool_elem, ref_cnt);
	struct rxe_pool *pool = elem->pool;
	void *obj;

	if (pool->flags & RXE_POOL_INDEX)
		__xa_erase(&pool->xa, elem->index);

	xa_unlock_bh(&pool->xa);

	if (pool->cleanup)
		pool->cleanup(elem);

	if (pool->flags & RXE_POOL_ALLOC) {
		obj = elem->obj;
		kfree(obj);
	}

	atomic_dec(&pool->num_elem);
}

/**
 * __rxe_add_ref() - takes a ref on pool element
 * @elem: pool element
 *
 * Takes a ref on pool element if count is not zero
 *
 * The rxe_add_ref() macro converts argument from object to pool element
 *
 * Returns 1 if successful else 0
 */
int __rxe_add_ref(struct rxe_pool_elem *elem)
{
	return kref_get_unless_zero(&elem->ref_cnt);
}

static bool refcount_dec_and_lock_bh(refcount_t *r, spinlock_t *lock)
	__acquires(lock) __releases(lock)
{
	if (refcount_dec_not_one(r))
		return false;

	spin_lock_bh(lock);
	if (!refcount_dec_and_test(r)) {
		spin_unlock_bh(lock);
		return false;
	}

	return true;
}

static int kref_put_lock_bh(struct kref *kref,
				void (*release)(struct kref *kref),
				spinlock_t *lock)
{
	if (refcount_dec_and_lock_bh(&kref->refcount, lock)) {
		release(kref);
		return 1;
	}
	return 0;
}

/**
 * __rxe_drop_ref() - drops a ref on pool element
 * @elem: pool element
 *
 * Drops a ref on pool element and if count goes to zero atomically
 * acquires the xa lock and then calls rxe_elem_release() holding the lock
 *
 * The rxe_drop_ref() macro converts argument from object to pool element
 *
 * Returns 1 if rxe_elem_release called else 0
 */
int __rxe_drop_ref(struct rxe_pool_elem *elem)
{
	return kref_put_lock_bh(&elem->ref_cnt, rxe_elem_release,
			    &elem->pool->xa.xa_lock);
}
