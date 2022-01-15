/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_POOL_H
#define RXE_POOL_H

#include <linux/xarray.h>

enum rxe_pool_flags {
	RXE_POOL_INDEX		= BIT(1),
	RXE_POOL_ALLOC		= BIT(2),
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
	RXE_NUM_TYPES,		/* keep me last */
};

struct rxe_pool_elem {
	struct rxe_pool		*pool;
	void			*obj;
	struct kref		ref_cnt;
	struct list_head	list;
	u32			index;
};

struct rxe_pool {
	struct rxe_dev		*rxe;
	const char		*name;
	void			(*cleanup)(struct rxe_pool_elem *obj);
	enum rxe_pool_flags	flags;
	enum rxe_elem_type	type;

	unsigned int		max_elem;
	atomic_t		num_elem;
	size_t			elem_size;
	size_t			elem_offset;
	struct xarray		xa;
	struct xa_limit		limit;
	u32			next;
	int			locked;
};

void rxe_pool_init(struct rxe_dev *rxe, struct rxe_pool *pool,
		   enum rxe_elem_type type, u32 max_elem);

void rxe_pool_cleanup(struct rxe_pool *pool);

void *rxe_alloc(struct rxe_pool *pool);

int __rxe_add_to_pool(struct rxe_pool *pool, struct rxe_pool_elem *elem);

#define rxe_add_to_pool(pool, obj) __rxe_add_to_pool(pool, &(obj)->elem)

void *rxe_pool_get_index(struct rxe_pool *pool, u32 index);

int __rxe_add_ref(struct rxe_pool_elem *elem);

#define rxe_add_ref(obj) __rxe_add_ref(&(obj)->elem)

int __rxe_drop_ref(struct rxe_pool_elem *elem);

#define rxe_drop_ref(obj) __rxe_drop_ref(&(obj)->elem)

#endif /* RXE_POOL_H */
