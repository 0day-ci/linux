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

/**
 * __rxe_insert_mcg - insert an mcg into red-black tree (rxe->mcg_tree)
 * @mcg: mcast group object with an embedded red-black tree node
 *
 * Context: caller must hold a reference to mcg and rxe->mcg_lock and
 * is responsible to avoid adding the same mcg twice to the tree.
 */
static void __rxe_insert_mcg(struct rxe_mcg *mcg)
{
	struct rb_root *tree = &mcg->rxe->mcg_tree;
	struct rb_node **link = &tree->rb_node;
	struct rb_node *node = NULL;
	struct rxe_mcg *tmp;
	int cmp;

	while (*link) {
		node = *link;
		tmp = rb_entry(node, struct rxe_mcg, node);

		cmp = memcmp(&tmp->mgid, &mcg->mgid, sizeof(mcg->mgid));
		if (cmp > 0)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&mcg->node, node, link);
	rb_insert_color(&mcg->node, tree);
}

/**
 * __rxe_remove_mcg - remove an mcg from red-black tree holding lock
 * @mcg: mcast group object with an embedded red-black tree node
 *
 * Context: caller must hold a reference to mcg and rxe->mcg_lock
 */
static void __rxe_remove_mcg(struct rxe_mcg *mcg)
{
	rb_erase(&mcg->node, &mcg->rxe->mcg_tree);
}

/**
 * __rxe_lookup_mcg - lookup mcg in rxe->mcg_tree while holding lock
 * @rxe: rxe device object
 * @mgid: multicast IP address
 *
 * Context: caller must hold rxe->mcg_lock
 * Returns: mcg on success and takes a ref to mcg else NULL
 */
static struct rxe_mcg *__rxe_lookup_mcg(struct rxe_dev *rxe,
					union ib_gid *mgid)
{
	struct rb_root *tree = &rxe->mcg_tree;
	struct rxe_mcg *mcg;
	struct rb_node *node;
	int cmp;

	node = tree->rb_node;

	while (node) {
		mcg = rb_entry(node, struct rxe_mcg, node);

		cmp = memcmp(&mcg->mgid, mgid, sizeof(*mgid));

		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			break;
	}

	if (node) {
		rxe_add_ref(mcg);
		return mcg;
	}

	return NULL;
}

/**
 * rxe_lookup_mcg - lookup up mcg in red-back tree
 * @rxe: rxe device object
 * @mgid: multicast IP address
 *
 * Returns: mcg if found else NULL
 */
struct rxe_mcg *rxe_lookup_mcg(struct rxe_dev *rxe, union ib_gid *mgid)
{
	struct rxe_mcg *mcg;

	spin_lock_bh(&rxe->mcg_lock);
	mcg = __rxe_lookup_mcg(rxe, mgid);
	spin_unlock_bh(&rxe->mcg_lock);

	return mcg;
}

/**
 * rxe_get_mcg - lookup or allocate a mcg
 * @rxe: rxe device object
 * @mgid: multicast IP address
 * @mcgp: address of returned mcg value
 *
 * Adds one ref if mcg already exists else add a second reference
 * which is dropped when qp_num goes to zero.
 *
 * Returns: 0 and sets *mcgp to mcg on success else an error
 */
static int rxe_get_mcg(struct rxe_dev *rxe, union ib_gid *mgid,
		       struct rxe_mcg **mcgp)
{
	struct rxe_mcg *mcg, *tmp;
	int ret;
	struct rxe_pool *pool = &rxe->mc_grp_pool;

	if (rxe->attr.max_mcast_grp == 0)
		return -EINVAL;

	/* check to see if mcg already exists */
	mcg = rxe_lookup_mcg(rxe, mgid);
	if (mcg) {
		*mcgp = mcg;
		return 0;
	}

	/* speculative alloc of mcg without using GFP_ATOMIC */
	mcg = rxe_alloc(pool);
	if (!mcg)
		return -ENOMEM;

	spin_lock_bh(&rxe->mcg_lock);
	/* re-check to see if someone else just added it */
	tmp = __rxe_lookup_mcg(rxe, mgid);
	if (tmp) {
		spin_unlock_bh(&rxe->mcg_lock);
		rxe_drop_ref(mcg);
		mcg = tmp;
		goto out;
	}

	if (atomic_inc_return(&rxe->mcg_num) > rxe->attr.max_mcast_grp)
		goto err_dec;

	ret = rxe_mcast_add(rxe, mgid);
	if (ret)
		goto err_out;

	rxe_add_ref(mcg);
	mcg->rxe = rxe;
	memcpy(&mcg->mgid, mgid, sizeof(*mgid));
	INIT_LIST_HEAD(&mcg->qp_list);
	atomic_inc(&rxe->mcg_num);
	__rxe_insert_mcg(mcg);
	spin_unlock_bh(&rxe->mcg_lock);
out:
	*mcgp = mcg;
	return 0;

err_dec:
	atomic_dec(&rxe->mcg_num);
	ret = -ENOMEM;
err_out:
	spin_unlock_bh(&rxe->mcg_lock);
	rxe_drop_ref(mcg);
	return ret;
}

static int rxe_mcast_add_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			   struct rxe_mcg *mcg)
{
	int err;
	struct rxe_mca *mca, *new_mca;

	/* check to see if the qp is already a member of the group */
	spin_lock_bh(&rxe->mcg_lock);
	list_for_each_entry(mca, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			spin_unlock_bh(&rxe->mcg_lock);
			return 0;
		}
	}
	spin_unlock_bh(&rxe->mcg_lock);

	/* speculative alloc new mca without using GFP_ATOMIC */
	new_mca = kzalloc(sizeof(*mca), GFP_KERNEL);
	if (!new_mca)
		return -ENOMEM;

	spin_lock_bh(&rxe->mcg_lock);
	/* re-check to see if someone else just attached qp */
	list_for_each_entry(mca, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			kfree(new_mca);
			err = 0;
			goto out;
		}
	}

	if (atomic_read(&mcg->qp_num) >= rxe->attr.max_mcast_qp_attach) {
		err = -ENOMEM;
		goto out;
	}

	atomic_inc(&mcg->qp_num);
	new_mca->qp = qp;
	atomic_inc(&qp->mcg_num);

	list_add_tail(&new_mca->qp_list, &mcg->qp_list);

	err = 0;
out:
	spin_unlock_bh(&rxe->mcg_lock);
	return err;
}

static int rxe_mcast_drop_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
				   union ib_gid *mgid)
{
	struct rxe_mcg *mcg;
	struct rxe_mca *mca, *tmp;
	int n;

	mcg = rxe_lookup_mcg(rxe, mgid);
	if (!mcg)
		goto err1;

	spin_lock_bh(&rxe->mcg_lock);

	list_for_each_entry_safe(mca, tmp, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			list_del(&mca->qp_list);
			n = atomic_dec_return(&mcg->qp_num);
			if (n <= 0)
				rxe_drop_ref(mcg);
			atomic_dec(&qp->mcg_num);

			spin_unlock_bh(&rxe->mcg_lock);
			rxe_drop_ref(mcg);
			kfree(mca);
			return 0;
		}
	}

	spin_unlock_bh(&rxe->mcg_lock);
	rxe_drop_ref(mcg);
err1:
	return -EINVAL;
}

void rxe_mc_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_mcg *mcg = container_of(elem, typeof(*mcg), elem);
	struct rxe_dev *rxe = mcg->rxe;

	spin_lock_bh(&rxe->mcg_lock);
	__rxe_remove_mcg(mcg);
	spin_unlock_bh(&rxe->mcg_lock);

	rxe_mcast_delete(rxe, &mcg->mgid);
}

int rxe_attach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	int err;
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);
	struct rxe_mcg *mcg;

	/* takes a ref on mcg if successful */
	err = rxe_get_mcg(rxe, mgid, &mcg);
	if (err)
		return err;

	err = rxe_mcast_add_grp_elem(rxe, qp, mcg);

	rxe_drop_ref(mcg);
	return err;
}

int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);

	return rxe_mcast_drop_grp_elem(rxe, qp, mgid);
}
