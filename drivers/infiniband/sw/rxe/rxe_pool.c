// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

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
		.flags          = RXE_POOL_NO_ALLOC,
	},
	[RXE_TYPE_PD] = {
		.name		= "rxe-pd",
		.size		= sizeof(struct rxe_pd),
		.elem_offset	= offsetof(struct rxe_pd, elem),
		.flags		= RXE_POOL_NO_ALLOC,
	},
	[RXE_TYPE_AH] = {
		.name		= "rxe-ah",
		.size		= sizeof(struct rxe_ah),
		.elem_offset	= offsetof(struct rxe_ah, elem),
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_AH_INDEX,
		.max_index	= RXE_MAX_AH_INDEX,
	},
	[RXE_TYPE_SRQ] = {
		.name		= "rxe-srq",
		.size		= sizeof(struct rxe_srq),
		.elem_offset	= offsetof(struct rxe_srq, elem),
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_SRQ_INDEX,
		.max_index	= RXE_MAX_SRQ_INDEX,
	},
	[RXE_TYPE_QP] = {
		.name		= "rxe-qp",
		.size		= sizeof(struct rxe_qp),
		.elem_offset	= offsetof(struct rxe_qp, elem),
		.cleanup	= rxe_qp_cleanup,
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_QP_INDEX,
		.max_index	= RXE_MAX_QP_INDEX,
	},
	[RXE_TYPE_CQ] = {
		.name		= "rxe-cq",
		.size		= sizeof(struct rxe_cq),
		.elem_offset	= offsetof(struct rxe_cq, elem),
		.flags          = RXE_POOL_NO_ALLOC,
		.cleanup	= rxe_cq_cleanup,
	},
	[RXE_TYPE_MR] = {
		.name		= "rxe-mr",
		.size		= sizeof(struct rxe_mr),
		.elem_offset	= offsetof(struct rxe_mr, elem),
		.cleanup	= rxe_mr_cleanup,
		.flags		= RXE_POOL_INDEX,
		.min_index	= RXE_MIN_MR_INDEX,
		.max_index	= RXE_MAX_MR_INDEX,
	},
	[RXE_TYPE_MW] = {
		.name		= "rxe-mw",
		.size		= sizeof(struct rxe_mw),
		.elem_offset	= offsetof(struct rxe_mw, elem),
		.cleanup	= rxe_mw_cleanup,
		.flags		= RXE_POOL_INDEX | RXE_POOL_NO_ALLOC,
		.min_index	= RXE_MIN_MW_INDEX,
		.max_index	= RXE_MAX_MW_INDEX,
	},
	[RXE_TYPE_MC_GRP] = {
		.name		= "rxe-mc_grp",
		.size		= sizeof(struct rxe_mc_grp),
		.elem_offset	= offsetof(struct rxe_mc_grp, elem),
		.cleanup	= rxe_mc_cleanup,
		.flags		= RXE_POOL_KEY,
		.key_offset	= offsetof(struct rxe_mc_grp, mgid),
		.key_size	= sizeof(union ib_gid),
	},
	[RXE_TYPE_MC_ELEM] = {
		.name		= "rxe-mc_elem",
		.size		= sizeof(struct rxe_mc_elem),
		.elem_offset	= offsetof(struct rxe_mc_elem, elem),
	},
};

void rxe_pool_init(
	struct rxe_dev		*rxe,
	struct rxe_pool		*pool,
	enum rxe_elem_type	type,
	unsigned int		max_elem)
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

	if (pool->flags & RXE_POOL_INDEX) {
		xa_init_flags(&pool->xarray.xa, XA_FLAGS_ALLOC);
		pool->xarray.limit.max = info->max_index;
		pool->xarray.limit.min = info->min_index;
	} else {
		/* if pool not indexed just use xa spin_lock */
		spin_lock_init(&pool->xarray.xa.xa_lock);
	}

	if (pool->flags & RXE_POOL_KEY) {
		pool->key.tree = RB_ROOT;
		pool->key.key_offset = info->key_offset;
		pool->key.key_size = info->key_size;
	}
}

void rxe_pool_cleanup(struct rxe_pool *pool)
{
	if (atomic_read(&pool->num_elem) > 0)
		pr_warn("%s pool destroyed with unfree'd elem\n",
			pool->name);
}

static int rxe_insert_key(struct rxe_pool *pool, struct rxe_pool_elem *new)
{
	struct rb_node **link = &pool->key.tree.rb_node;
	struct rb_node *parent = NULL;
	struct rxe_pool_elem *elem;
	int cmp;

	while (*link) {
		parent = *link;
		elem = rb_entry(parent, struct rxe_pool_elem, key_node);

		cmp = memcmp((u8 *)elem + pool->key.key_offset,
			     (u8 *)new + pool->key.key_offset,
			     pool->key.key_size);

		if (cmp == 0) {
			pr_warn("key already exists!\n");
			return -EINVAL;
		}

		if (cmp > 0)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&new->key_node, parent, link);
	rb_insert_color(&new->key_node, &pool->key.tree);

	return 0;
}

int __rxe_add_key_locked(struct rxe_pool_elem *elem, void *key)
{
	struct rxe_pool *pool = elem->pool;
	int err;

	memcpy((u8 *)elem + pool->key.key_offset, key, pool->key.key_size);
	err = rxe_insert_key(pool, elem);

	return err;
}

int __rxe_add_key(struct rxe_pool_elem *elem, void *key)
{
	struct rxe_pool *pool = elem->pool;
	int err;

	rxe_pool_lock_bh(pool);
	err = __rxe_add_key_locked(elem, key);
	rxe_pool_unlock_bh(pool);

	return err;
}

void __rxe_drop_key_locked(struct rxe_pool_elem *elem)
{
	struct rxe_pool *pool = elem->pool;

	rb_erase(&elem->key_node, &pool->key.tree);
}

void __rxe_drop_key(struct rxe_pool_elem *elem)
{
	struct rxe_pool *pool = elem->pool;

	rxe_pool_lock_bh(pool);
	__rxe_drop_key_locked(elem);
	rxe_pool_unlock_bh(pool);
}

void *rxe_alloc_locked(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
	void *obj;
	int err;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	obj = kzalloc(pool->elem_size, GFP_ATOMIC);
	if (!obj)
		goto out_cnt;

	elem = (struct rxe_pool_elem *)((u8 *)obj + pool->elem_offset);

	elem->pool = pool;
	elem->obj = obj;
	kref_init(&elem->ref_cnt);

	if (pool->flags & RXE_POOL_INDEX) {
		err = xa_alloc_cyclic_bh(&pool->xarray.xa, &elem->index, elem,
					 pool->xarray.limit,
					 &pool->xarray.next, GFP_ATOMIC);
		if (err)
			goto out_free;
	}

	return obj;

out_free:
	kfree(obj);
out_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

void *rxe_alloc(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
	void *obj;
	int err;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	obj = kzalloc(pool->elem_size, GFP_KERNEL);
	if (!obj)
		goto out_cnt;

	elem = (struct rxe_pool_elem *)((u8 *)obj + pool->elem_offset);

	elem->pool = pool;
	elem->obj = obj;
	kref_init(&elem->ref_cnt);

	if (pool->flags & RXE_POOL_INDEX) {
		err = xa_alloc_cyclic_bh(&pool->xarray.xa, &elem->index, elem,
					 pool->xarray.limit,
					 &pool->xarray.next, GFP_KERNEL);
		if (err)
			goto out_free;
	}

	return obj;

out_free:
	kfree(obj);
out_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem)
{
	int err = -EINVAL;

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto out_cnt;

	elem->pool = pool;
	elem->obj = (u8 *)elem - pool->elem_offset;
	kref_init(&elem->ref_cnt);

	if (pool->flags & RXE_POOL_INDEX) {
		err = xa_alloc_cyclic_bh(&pool->xarray.xa, &elem->index, elem,
					 pool->xarray.limit,
					 &pool->xarray.next, GFP_KERNEL);
		if (err)
			goto out_cnt;
	}

	return 0;

out_cnt:
	atomic_dec(&pool->num_elem);
	return err;
}

void rxe_elem_release(struct kref *kref)
{
	struct rxe_pool_elem *elem =
		container_of(kref, struct rxe_pool_elem, ref_cnt);
	struct rxe_pool *pool = elem->pool;
	void *obj;

	if (pool->flags & RXE_POOL_INDEX)
		__xa_erase(&pool->xarray.xa, elem->index);

	if (pool->cleanup)
		pool->cleanup(elem);

	if (!(pool->flags & RXE_POOL_NO_ALLOC)) {
		obj = elem->obj;
		kfree(obj);
	}

	atomic_dec(&pool->num_elem);
}

/**
 * rxe_pool_get_index - lookup object from index
 * @pool: the object pool
 * @index: the index of the obkect
 *
 * Returns: the object if the index exists in the pool
 * and the reference count on the object is positive
 */
void *rxe_pool_get_index(struct rxe_pool *pool, u32 index)
{
	struct rxe_pool_elem *elem;
	void *obj;

	rxe_pool_lock_bh(pool);
	elem = xa_load(&pool->xarray.xa, index);
	if (elem && kref_get_unless_zero(&elem->ref_cnt))
		obj = elem->obj;
	else
		obj = NULL;
	rxe_pool_unlock_bh(pool);

	return obj;
}

void *rxe_pool_get_key_locked(struct rxe_pool *pool, void *key)
{
	struct rb_node *node;
	struct rxe_pool_elem *elem;
	void *obj;
	int cmp;

	node = pool->key.tree.rb_node;

	while (node) {
		elem = rb_entry(node, struct rxe_pool_elem, key_node);

		cmp = memcmp((u8 *)elem + pool->key.key_offset,
			     key, pool->key.key_size);

		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			break;
	}

	if (node) {
		kref_get(&elem->ref_cnt);
		obj = elem->obj;
	} else {
		obj = NULL;
	}

	return obj;
}

void *rxe_pool_get_key(struct rxe_pool *pool, void *key)
{
	void *obj;

	rxe_pool_lock_bh(pool);
	obj = rxe_pool_get_key_locked(pool, key);
	rxe_pool_unlock_bh(pool);

	return obj;
}
