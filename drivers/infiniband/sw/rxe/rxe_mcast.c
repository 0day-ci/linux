// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"

static int rxe_mcast_add(struct rxe_dev *rxe, union ib_gid *mgid)
{
	int err;
	unsigned char ll_addr[ETH_ALEN];

	ipv6_eth_mc_map((struct in6_addr *)mgid->raw, ll_addr);
	err = dev_mc_add(rxe->ndev, ll_addr);

	return err;
}

static int rxe_mcast_delete(struct rxe_dev *rxe, union ib_gid *mgid)
{
	int err;
	unsigned char ll_addr[ETH_ALEN];

	ipv6_eth_mc_map((struct in6_addr *)mgid->raw, ll_addr);
	err = dev_mc_del(rxe->ndev, ll_addr);

	return err;
}

static int rxe_mcast_get_grp(struct rxe_dev *rxe, union ib_gid *mgid,
		      struct rxe_mc_grp **grp_p)
{
	struct rxe_pool *pool = &rxe->mc_grp_pool;
	struct rxe_mc_grp *grp;
	unsigned long flags;
	int err = 0;

	/* Perform this while holding the mc_grp_pool lock
	 * to prevent races where two coincident calls fail to lookup the
	 * same group and then both create the same group.
	 */
	write_lock_irqsave(&pool->pool_lock, flags);
	grp = rxe_pool_get_key_locked(pool, mgid);
	if (grp)
		goto done;

	grp = rxe_alloc_with_key_locked(&rxe->mc_grp_pool, mgid);
	if (!grp) {
		err = -ENOMEM;
		goto done;
	}

	INIT_LIST_HEAD(&grp->qp_list);
	spin_lock_init(&grp->mcg_lock);
	grp->rxe = rxe;

	err = rxe_mcast_add(rxe, mgid);
	if (err) {
		rxe_fini_ref_locked(grp);
		grp = NULL;
		goto done;
	}

	/* match the reference taken by get_key */
	rxe_add_ref_locked(grp);
done:
	*grp_p = grp;
	write_unlock_irqrestore(&pool->pool_lock, flags);

	return err;
}

static void rxe_mcast_put_grp(struct rxe_mc_grp *grp)
{
	struct rxe_dev *rxe = grp->rxe;
	struct rxe_pool *pool = &rxe->mc_grp_pool;
	unsigned long flags;

	write_lock_irqsave(&pool->pool_lock, flags);

	rxe_drop_ref_locked(grp);

	if (rxe_read_ref(grp) == 1) {
		rxe_mcast_delete(rxe, &grp->mgid);
		rxe_fini_ref_locked(grp);
	}

	write_unlock_irqrestore(&pool->pool_lock, flags);
}

/**
 * rxe_mcast_add_grp_elem() - Associate a multicast address with a QP
 * @rxe: the rxe device
 * @qp: the QP
 * @mgid: the multicast address
 *
 * Each multicast group can be associated with one or more QPs and
 * each QP can be associated with zero or more multicast groups.
 * Between each multicast group associated with a QP there is a
 * rxe_mc_elem struct which has two list head structs and is joined
 * both to a list of QPs on the multicast group and a list of groups
 * on the QP. The elem has pointers to the group and the QP and
 * takes a reference for each one.
 *
 * Return: 0 on success or an error on failure.
 */
int rxe_mcast_add_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			   union ib_gid *mgid)
{
	struct rxe_mc_elem *elem;
	struct rxe_mc_grp *grp;
	int err;

	if (rxe->attr.max_mcast_qp_attach == 0)
		return -EINVAL;

	/* takes a ref on grp if successful */
	err = rxe_mcast_get_grp(rxe, mgid, &grp);
	if (err)
		return err;

	spin_lock_bh(&qp->grp_lock);
	spin_lock_bh(&grp->mcg_lock);

	/* check to see if the qp is already a member of the group */
	list_for_each_entry(elem, &grp->qp_list, qp_list) {
		if (elem->qp == qp)
			goto drop_ref;
	}

	if (grp->num_qp >= rxe->attr.max_mcast_qp_attach) {
		err = -ENOMEM;
		goto drop_ref;
	}

	if (atomic_read(&rxe->total_mcast_qp_attach) >=
			rxe->attr.max_total_mcast_qp_attach) {
		err = -ENOMEM;
		goto drop_ref;
	}

	elem = kmalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem) {
		err = -ENOMEM;
		goto drop_ref;
	}

	atomic_inc(&rxe->total_mcast_qp_attach);
	grp->num_qp++;
	rxe_add_ref(qp);
	elem->qp = qp;
	/* still holding a ref on grp */
	elem->grp = grp;

	list_add(&elem->qp_list, &grp->qp_list);
	list_add(&elem->grp_list, &qp->grp_list);

	goto done;

drop_ref:
	rxe_drop_ref(grp);

done:
	spin_unlock_bh(&grp->mcg_lock);
	spin_unlock_bh(&qp->grp_lock);

	return err;
}

/**
 * rxe_mcast_drop_grp_elem() - Dissociate multicast address and QP
 * @rxe: the rxe device
 * @qp: the QP
 * @mgid: the multicast group
 *
 * Walk the list of group elements to find one which matches QP
 * Then delete from group and qp lists and free pointers and the elem.
 * Check to see if we have removed the last qp from group and delete
 * it if so.
 *
 * Return: 0 on success else an error on failure
 */
int rxe_mcast_drop_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			    union ib_gid *mgid)
{
	struct rxe_mc_elem *elem, *tmp;
	struct rxe_mc_grp *grp;
	int err = 0;

	grp = rxe_pool_get_key(&rxe->mc_grp_pool, mgid);
	if (!grp)
		return -EINVAL;

	spin_lock_bh(&qp->grp_lock);
	spin_lock_bh(&grp->mcg_lock);

	list_for_each_entry_safe(elem, tmp, &grp->qp_list, qp_list) {
		if (elem->qp == qp) {
			list_del(&elem->qp_list);
			list_del(&elem->grp_list);
			rxe_drop_ref(grp);
			rxe_drop_ref(qp);
			grp->num_qp--;
			kfree(elem);
			atomic_dec(&rxe->total_mcast_qp_attach);
			goto done;
		}
	}

	err = -EINVAL;
done:
	spin_unlock_bh(&grp->mcg_lock);
	spin_unlock_bh(&qp->grp_lock);

	rxe_mcast_put_grp(grp);

	return err;
}

void rxe_drop_all_mcast_groups(struct rxe_qp *qp)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_mc_grp *grp;
	struct rxe_mc_elem *elem;

	while (1) {
		spin_lock_bh(&qp->grp_lock);
		if (list_empty(&qp->grp_list)) {
			spin_unlock_bh(&qp->grp_lock);
			break;
		}
		elem = list_first_entry(&qp->grp_list, struct rxe_mc_elem,
					grp_list);
		list_del(&elem->grp_list);
		spin_unlock_bh(&qp->grp_lock);

		grp = elem->grp;
		spin_lock_bh(&grp->mcg_lock);
		list_del(&elem->qp_list);
		grp->num_qp--;
		spin_unlock_bh(&grp->mcg_lock);

		kfree(elem);
		atomic_dec(&rxe->total_mcast_qp_attach);
		rxe_drop_ref(qp);
		rxe_mcast_put_grp(grp);
	}
}
