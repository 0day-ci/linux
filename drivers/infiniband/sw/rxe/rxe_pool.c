// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2022 Hewlett Packard Enterprise, Inc. All rights reserved.
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"

#define RXE_POOL_ALIGN		(16)

static const struct rxe_type_info {
	const char *name;
	size_t size;
	size_t elem_offset;
	void (*cleanup)(struct rxe_pool_elem *elem);
	enum rxe_pool_flags flags;
	u32 min_index;
	u32 max_index;
} rxe_type_info[RXE_NUM_TYPES] = {
	[RXE_TYPE_UC] = {
		.name		= "rxe-uc",
		.size		= sizeof(struct rxe_ucontext),
		.elem_offset	= offsetof(struct rxe_ucontext, elem),
		.min_index	= 1,
		.max_index	= UINT_MAX,
	},
	[RXE_TYPE_PD] = {
		.name		= "rxe-pd",
		.size		= sizeof(struct rxe_pd),
		.elem_offset	= offsetof(struct rxe_pd, elem),
		.min_index	= 1,
		.max_index	= UINT_MAX,
	},
	[RXE_TYPE_AH] = {
		.name		= "rxe-ah",
		.size		= sizeof(struct rxe_ah),
		.elem_offset	= offsetof(struct rxe_ah, elem),
		.min_index	= RXE_MIN_AH_INDEX,
		.max_index	= RXE_MAX_AH_INDEX,
	},
	[RXE_TYPE_SRQ] = {
		.name		= "rxe-srq",
		.size		= sizeof(struct rxe_srq),
		.elem_offset	= offsetof(struct rxe_srq, elem),
		.min_index	= RXE_MIN_SRQ_INDEX,
		.max_index	= RXE_MAX_SRQ_INDEX,
	},
	[RXE_TYPE_QP] = {
		.name		= "rxe-qp",
		.size		= sizeof(struct rxe_qp),
		.elem_offset	= offsetof(struct rxe_qp, elem),
		.cleanup	= rxe_qp_cleanup,
		.min_index	= RXE_MIN_QP_INDEX,
		.max_index	= RXE_MAX_QP_INDEX,
	},
	[RXE_TYPE_CQ] = {
		.name		= "rxe-cq",
		.size		= sizeof(struct rxe_cq),
		.elem_offset	= offsetof(struct rxe_cq, elem),
		.cleanup	= rxe_cq_cleanup,
		.min_index	= 1,
		.max_index	= UINT_MAX,
	},
	[RXE_TYPE_MR] = {
		.name		= "rxe-mr",
		.size		= sizeof(struct rxe_mr),
		.elem_offset	= offsetof(struct rxe_mr, elem),
		.cleanup	= rxe_mr_cleanup,
		.flags		= RXE_POOL_ALLOC,
		.min_index	= RXE_MIN_MR_INDEX,
		.max_index	= RXE_MAX_MR_INDEX,
	},
	[RXE_TYPE_MW] = {
		.name		= "rxe-mw",
		.size		= sizeof(struct rxe_mw),
		.elem_offset	= offsetof(struct rxe_mw, elem),
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
	spin_lock_init(&pool->xa.xa_lock);

	xa_init_flags(&pool->xa, XA_FLAGS_ALLOC);
	pool->limit.max = info->max_index;
	pool->limit.min = info->min_index;
}

/* runs single threaded at driver shutdown */
void rxe_pool_cleanup(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
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
		pr_warn("Freed %d indices and %d objects from pool %s\n",
				elem_count, free_count, pool->name);
}

void *rxe_alloc(struct rxe_pool *pool)
{
	struct rxe_pool_elem *elem;
	void *obj;
	int err;

	if (!(pool->flags & RXE_POOL_ALLOC)) {
		pr_warn_once("%s: pool %s must call rxe_add_to_pool\n",
				__func__, pool->name);
		return NULL;
	}

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto err_cnt;

	obj = kzalloc(pool->elem_size, GFP_KERNEL);
	if (!obj)
		goto err_cnt;

	elem = (struct rxe_pool_elem *)((u8 *)obj + pool->elem_offset);

	elem->pool = pool;
	elem->obj = obj;
	kref_init(&elem->ref_cnt);

	err = xa_alloc_cyclic_bh(&pool->xa, &elem->index, elem, pool->limit,
			&pool->next, GFP_KERNEL);
	if (err)
		goto err_free;

	return obj;

err_free:
	kfree(obj);
err_cnt:
	atomic_dec(&pool->num_elem);
	return NULL;
}

int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem)
{
	int err;

	if (pool->flags & RXE_POOL_ALLOC) {
		pr_warn_once("%s: pool %s must call rxe_alloc\n",
				__func__, pool->name);
		return -EINVAL;
	}

	if (atomic_inc_return(&pool->num_elem) > pool->max_elem)
		goto err_cnt;

	elem->pool = pool;
	elem->obj = (u8 *)elem - pool->elem_offset;
	kref_init(&elem->ref_cnt);

	err = xa_alloc_cyclic_bh(&pool->xa, &elem->index, elem, pool->limit,
			&pool->next, GFP_KERNEL);
	if (err)
		goto err_cnt;

	return 0;

err_cnt:
	atomic_dec(&pool->num_elem);
	return -EINVAL;
}

void *rxe_pool_get_index(struct rxe_pool *pool, u32 index)
{
	struct rxe_pool_elem *elem;
	void *obj = NULL;

	rcu_read_lock();
	elem = xa_load(&pool->xa, index);
	if (elem && kref_get_unless_zero(&elem->ref_cnt))
		obj = elem->obj;
	rcu_read_unlock();

	return obj;
}

static void rxe_obj_free_rcu(struct rcu_head *rcu)
{
	struct rxe_pool_elem *elem = container_of(rcu, typeof(*elem), rcu);

	kfree(elem->obj);
}

static void __rxe_elem_release_rcu(struct kref *kref)
	__releases(&pool->xa.xa_lock)
{
	struct rxe_pool_elem *elem = container_of(kref,
					struct rxe_pool_elem, ref_cnt);
	struct rxe_pool *pool = elem->pool;

	__xa_erase(&pool->xa, elem->index);

	spin_unlock(&pool->xa.xa_lock);

	if (pool->cleanup)
		pool->cleanup(elem);

	atomic_dec(&pool->num_elem);

	if (pool->flags & RXE_POOL_ALLOC)
		call_rcu(&elem->rcu, rxe_obj_free_rcu);
}

int __rxe_add_ref(struct rxe_pool_elem *elem)
{
	return kref_get_unless_zero(&elem->ref_cnt);
}

int __rxe_drop_ref(struct rxe_pool_elem *elem)
{
	return kref_put_lock(&elem->ref_cnt, __rxe_elem_release_rcu,
			&elem->pool->xa.xa_lock);
}
