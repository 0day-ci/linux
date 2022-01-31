// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"
#include "rxe_loc.h"

static int rxe_mcast_add(struct rxe_dev *rxe, union ib_gid *mgid)
{
	unsigned char ll_addr[ETH_ALEN];

	ipv6_eth_mc_map((struct in6_addr *)mgid->raw, ll_addr);

	return dev_mc_add(rxe->ndev, ll_addr);
}

static int rxe_mcast_delete(struct rxe_dev *rxe, union ib_gid *mgid)
{
	unsigned char ll_addr[ETH_ALEN];

	ipv6_eth_mc_map((struct in6_addr *)mgid->raw, ll_addr);

	return dev_mc_del(rxe->ndev, ll_addr);
}

/* caller should hold mc_grp_pool->pool_lock */
static int __rxe_create_mcg(struct rxe_dev *rxe, struct rxe_pool *pool,
			    union ib_gid *mgid, struct rxe_mcg **mcg_p)
{
	int err;
	struct rxe_mcg *mcg;

	mcg = rxe_alloc_locked(&rxe->mc_grp_pool);
	if (!mcg)
		return -ENOMEM;

	err = rxe_mcast_add(rxe, mgid);
	if (unlikely(err)) {
		rxe_drop_ref(mcg);
		return err;
	}

	INIT_LIST_HEAD(&mcg->qp_list);
	spin_lock_init(&mcg->mcg_lock);
	mcg->rxe = rxe;

	rxe_add_ref(mcg);
	rxe_add_key_locked(mcg, mgid);

	*mcg_p = mcg;
	return 0;
}

/* caller is holding a ref from lookup and mcg->mcg_lock*/
void __rxe_destroy_mcg(struct rxe_mcg *mcg)
{
	rxe_drop_key(mcg);
	rxe_drop_ref(mcg);

	rxe_mcast_delete(mcg->rxe, &mcg->mgid);
}

static int rxe_mcast_get_mcg(struct rxe_dev *rxe, union ib_gid *mgid,
			     struct rxe_mcg **mcg_p)
{
	int err;
	struct rxe_mcg *mcg;
	struct rxe_pool *pool = &rxe->mc_grp_pool;

	if (rxe->attr.max_mcast_qp_attach == 0)
		return -EINVAL;

	write_lock_bh(&pool->pool_lock);

	mcg = rxe_pool_get_key_locked(pool, mgid);
	if (mcg)
		goto done;

	err = __rxe_create_mcg(rxe, pool, mgid, &mcg);
	if (err) {
		write_unlock_bh(&pool->pool_lock);
		return err;
	}

done:
	write_unlock_bh(&pool->pool_lock);
	*mcg_p = mcg;
	return 0;
}

static int rxe_mcast_add_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			   struct rxe_mcg *mcg)
{
	int err;
	struct rxe_mca *mca, *new_mca;

	/* check to see if the qp is already a member of the group */
	spin_lock_bh(&mcg->mcg_lock);
	list_for_each_entry(mca, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			spin_unlock_bh(&mcg->mcg_lock);
			return 0;
		}
	}
	spin_unlock_bh(&mcg->mcg_lock);

	/* speculative alloc new mca without using GFP_ATOMIC */
	new_mca = kzalloc(sizeof(*mca), GFP_KERNEL);
	if (!new_mca)
		return -ENOMEM;

	spin_lock_bh(&mcg->mcg_lock);
	/* re-check to see if someone else just attached qp */
	list_for_each_entry(mca, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			kfree(new_mca);
			err = 0;
			goto out;
		}
	}
	mca = new_mca;

	if (atomic_read(&mcg->qp_num) >= rxe->attr.max_mcast_qp_attach) {
		err = -ENOMEM;
		goto out;
	}

	atomic_inc(&mcg->qp_num);
	mca->qp = qp;
	atomic_inc(&qp->mcg_num);

	list_add_tail(&mca->qp_list, &mcg->qp_list);

	err = 0;
out:
	spin_unlock_bh(&mcg->mcg_lock);
	return err;
}

static int rxe_mcast_drop_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
				   union ib_gid *mgid)
{
	struct rxe_mcg *mcg;
	struct rxe_mca *mca, *tmp;

	mcg = rxe_pool_get_key(&rxe->mc_grp_pool, mgid);
	if (!mcg)
		goto err1;

	spin_lock_bh(&mcg->mcg_lock);

	list_for_each_entry_safe(mca, tmp, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			list_del(&mca->qp_list);
			if (atomic_dec_return(&mcg->qp_num) <= 0)
				__rxe_destroy_mcg(mcg);
			atomic_dec(&qp->mcg_num);

			spin_unlock_bh(&mcg->mcg_lock);
			rxe_drop_ref(mcg);
			kfree(mca);
			return 0;
		}
	}

	spin_unlock_bh(&mcg->mcg_lock);
	rxe_drop_ref(mcg);
err1:
	return -EINVAL;
}

void rxe_mc_cleanup(struct rxe_pool_elem *elem)
{
	/* nothing left to do */
}

int rxe_attach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	int err;
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);
	struct rxe_mcg *mcg;

	err = rxe_mcast_get_mcg(rxe, mgid, &mcg);
	if (err)
		return err;

	err = rxe_mcast_add_grp_elem(rxe, qp, mcg);

	if (atomic_read(&mcg->qp_num) == 0)
		__rxe_destroy_mcg(mcg);

	rxe_drop_ref(mcg);
	return err;
}

int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);

	return rxe_mcast_drop_grp_elem(rxe, qp, mgid);
}
