/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_POOL_H
#define RXE_POOL_H

enum rxe_pool_flags {
	RXE_POOL_INDEX		= BIT(1),
	RXE_POOL_KEY		= BIT(2),
	RXE_POOL_ALLOC		= BIT(4),
};

enum rxe_elem_type {
	RXE_TYPE_UC,
	RXE_TYPE_PD,
	RXE_TYPE_AH,
	RXE_TYPE_SRQ,
	RXE_TYPE_QP,
	RXE_TYPE_CQ,
	RXE_TYPE_MR,
	RXE_TYPE_MW,
	RXE_TYPE_MC_GRP,
	RXE_TYPE_MC_ELEM,
	RXE_NUM_TYPES,		/* keep me last */
};

struct rxe_pool_elem {
	struct rxe_pool		*pool;
	void			*obj;
	struct kref		ref_cnt;
	struct list_head	list;

	/* only used if keyed */
	struct rb_node		key_node;

	/* only used if indexed */
	u32			index;
};

struct rxe_pool {
	struct rxe_dev		*rxe;
	const char		*name;
	int			(*init)(struct rxe_pool_elem *elem);
	void			(*cleanup)(struct rxe_pool_elem *elem);
	enum rxe_pool_flags	flags;
	enum rxe_elem_type	type;

	unsigned int		max_elem;
	atomic_t		num_elem;
	size_t			elem_size;
	size_t			elem_offset;

	/* only used if indexed */
	struct {
		struct xarray		xa;
		struct xa_limit		limit;
		u32			next;
	} xarray;

	/* only used if keyed */
	struct {
		struct rb_root		tree;
		size_t			key_offset;
		size_t			key_size;
	} key;
};

#define rxe_pool_lock_bh(pool) xa_lock_bh(&pool->xarray.xa)
#define rxe_pool_unlock_bh(pool) xa_unlock_bh(&pool->xarray.xa)

void rxe_pool_init(struct rxe_dev *rxe, struct rxe_pool *pool,
		   enum rxe_elem_type type, u32 max_elem);

void rxe_pool_cleanup(struct rxe_pool *pool);

void *rxe_alloc(struct rxe_pool *pool);

int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem);
#define rxe_add_to_pool(pool, obj) __rxe_add_to_pool(pool, &(obj)->elem)

void *rxe_pool_get_index(struct rxe_pool *pool, u32 index);

void *rxe_pool_get_key(struct rxe_pool *pool, void *key);

void *rxe_pool_add_key(struct rxe_pool *pool, void *key);

static inline bool __rxe_add_ref(struct rxe_pool_elem *elem)
{
	return kref_get_unless_zero(&elem->ref_cnt);
}
#define rxe_add_ref(obj) __rxe_add_ref(&(obj)->elem)

void rxe_elem_release(struct kref *kref);

static inline bool __rxe_drop_ref(struct rxe_pool_elem *elem)
{
	bool ret;

	rxe_pool_lock_bh(elem->pool);
	ret = kref_put(&elem->ref_cnt, rxe_elem_release);
	rxe_pool_unlock_bh(elem->pool);

	return ret;
}
#define rxe_drop_ref(obj) __rxe_drop_ref(&(obj)->elem)

#endif /* RXE_POOL_H */
