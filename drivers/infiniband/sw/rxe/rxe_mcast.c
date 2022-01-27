// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2022 Hewlett Packard Enterprise, Inc. All rights reserved.
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

/*
 * rxe_mcast.c implements driver support for multicast transport.
 * It is based on two data structures struct rxe_mcg ('mcg') and
 * struct rxe_mca ('mca'). An mcg is allocated each time a qp is
 * attached to a new mgid for the first time. These are indexed by
 * a red-black tree using the mgid. This data structure is searched
 * for the mcg when a multicast packet is received and when another
 * qp is attached to the same mgid. It is cleaned up when the last qp
 * is detached from the mcg. Each time a qp is attached to an mcg an
 * mca is created. It holds a pointer to the qp and is added to a list
 * of qp's that are attached to the mcg. The qp_list is used to replicate
 * mcast packets in the rxe receive path.
 *
 * mcg's keep a count of the number of qp's attached and once the count
 * goes to zero it needs to be cleaned up. mcg's also have a reference
 * count. While InfiniBand multicast groups are created and destroyed
 * by explicit MADs, for rxe devices this is more implicit and the mcg
 * is created by the first qp attach and destroyed by the last qp detach.
 * To implement this there is some hysteresis with an extra kref_get when
 * the mcg is created and an extra kref_put when the qp count decreases
 * to zero.
 *
 * The qp list and the red-black tree are protected by a single
 * rxe->mcg_lock per device.
 */

#include "rxe.h"

/**
 * rxe_mcast_add - add multicast address to rxe device
 * @rxe: rxe device object
 * @mgid: multicast address as a gid
 *
 * Returns 0 on success else an error
 */
static int rxe_mcast_add(struct rxe_dev *rxe, union ib_gid *mgid)
{
	unsigned char ll_addr[ETH_ALEN];

	ipv6_eth_mc_map((struct in6_addr *)mgid->raw, ll_addr);

	return dev_mc_add(rxe->ndev, ll_addr);
}

/**
 * rxe_mcast_delete - delete multicast address from rxe device
 * @rxe: rxe device object
 * @mgid: multicast address as a gid
 *
 * Returns 0 on success else an error
 */
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
		kref_get(&mcg->ref_cnt);
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
	int err;

	if (rxe->attr.max_mcast_grp == 0)
		return -EINVAL;

	/* check to see if mcg already exists */
	mcg = rxe_lookup_mcg(rxe, mgid);
	if (mcg) {
		*mcgp = mcg;
		return 0;
	}

	/* speculative alloc of mcg without using GFP_ATOMIC */
	mcg = kzalloc(sizeof(*mcg), GFP_KERNEL);
	if (!mcg)
		return -ENOMEM;

	spin_lock_bh(&rxe->mcg_lock);
	/* re-check to see if someone else just added it */
	tmp = __rxe_lookup_mcg(rxe, mgid);
	if (tmp) {
		spin_unlock_bh(&rxe->mcg_lock);
		kfree(mcg);
		mcg = tmp;
		goto out;
	}

	if (atomic_inc_return(&rxe->mcg_num) > rxe->attr.max_mcast_grp) {
		err = -ENOMEM;
		goto err_dec;
	}

	err = rxe_mcast_add(rxe, mgid);
	if (err)
		goto err_dec;

	kref_init(&mcg->ref_cnt);
	kref_get(&mcg->ref_cnt);
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
	spin_unlock_bh(&rxe->mcg_lock);
	kfree(mcg);
	return err;
}

/**
 * __rxe_cleanup_mcg - cleanup mcg object holding lock
 * @kref: kref embedded in mcg object
 *
 * Context: caller has put all references to mcg
 * caller should hold rxe->mcg_lock
 */
static void __rxe_cleanup_mcg(struct kref *kref)
{
	struct rxe_mcg *mcg = container_of(kref, typeof(*mcg), ref_cnt);
	struct rxe_dev *rxe = mcg->rxe;

	__rxe_remove_mcg(mcg);
	rxe_mcast_delete(rxe, &mcg->mgid);
	atomic_dec(&rxe->mcg_num);

	kfree(mcg);
}

/**
 * rxe_cleanup_mcg - cleanup mcg object
 * @kref: kref embedded in mcg object
 *
 * Context: caller has put all references to mcg and no one should be
 * able to get another one
 */
void rxe_cleanup_mcg(struct kref *kref)
{
	struct rxe_mcg *mcg = container_of(kref, typeof(*mcg), ref_cnt);
	struct rxe_dev *rxe = mcg->rxe;

	spin_lock_bh(&rxe->mcg_lock);
	__rxe_cleanup_mcg(kref);
	spin_unlock_bh(&rxe->mcg_lock);
}

/**
 * __rxe_init_mca - initialize a new mca holding lock
 * @qp: qp object
 * @mcg: mcg object
 * @mca: empty space for new mca
 *
 * Context: caller must hold references on qp and mcg, rxe->mcg_lock
 * and pass memory for new mca
 *
 * Returns: 0 on success else an error
 */
static int __rxe_init_mca(struct rxe_qp *qp, struct rxe_mcg *mcg,
			  struct rxe_mca *mca)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	int n;

	n = atomic_inc_return(&rxe->mcg_attach);
	if (n > rxe->attr.max_total_mcast_qp_attach) {
		atomic_dec(&rxe->mcg_attach);
		return -ENOMEM;
	}

	n = atomic_inc_return(&mcg->qp_num);
	if (n > rxe->attr.max_mcast_qp_attach) {
		atomic_dec(&mcg->qp_num);
		atomic_dec(&rxe->mcg_attach);
		return -ENOMEM;
	}

	atomic_inc(&qp->mcg_num);

	rxe_add_ref(qp);
	mca->qp = qp;

	list_add_tail(&mca->qp_list, &mcg->qp_list);

	return 0;
}

/**
 * rxe_attach_mcg - attach qp to mcg if not already attached
 * @mcg: mcg object
 * @qp: qp object
 *
 * Context: caller must hold reference on qp and mcg.
 * Returns: 0 on success else an error
 */
static int rxe_attach_mcg(struct rxe_mcg *mcg, struct rxe_qp *qp)
{
	struct rxe_dev *rxe = mcg->rxe;
	struct rxe_mca *mca, *new_mca;
	int err;

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
			goto done;
		}
	}

	mca = new_mca;
	err = __rxe_init_mca(qp, mcg, mca);
	if (err)
		kfree(mca);
done:
	spin_unlock_bh(&rxe->mcg_lock);

	return err;
}

/**
 * __rxe_cleanup_mca - cleanup mca object holding lock
 * @mca: mca object
 * @mcg: mcg object
 *
 * Context: caller must hold a reference to mcg and rxe->mcg_lock
 */
static void __rxe_cleanup_mca(struct rxe_mca *mca, struct rxe_mcg *mcg)
{
	list_del(&mca->qp_list);

	atomic_dec(&mcg->qp_num);
	atomic_dec(&mcg->rxe->mcg_attach);
	atomic_dec(&mca->qp->mcg_num);

	rxe_drop_ref(mca->qp);
}

/**
 * rxe_detach_mcg - detach qp from mcg
 * @mcg: mcg object
 * @qp: qp object
 *
 * Returns: 0 on success else an error if qp is not attached.
 */
static int rxe_detach_mcg(struct rxe_mcg *mcg, struct rxe_qp *qp)
{
	struct rxe_dev *rxe = mcg->rxe;
	struct rxe_mca *mca, *tmp;

	spin_lock_bh(&rxe->mcg_lock);
	list_for_each_entry_safe(mca, tmp, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			__rxe_cleanup_mca(mca, mcg);
			if (atomic_read(&mcg->qp_num) <= 0)
				kref_put(&mcg->ref_cnt, __rxe_cleanup_mcg);
			spin_unlock_bh(&rxe->mcg_lock);
			kfree(mca);
			return 0;
		}
	}
	spin_unlock_bh(&rxe->mcg_lock);

	return -EINVAL;
}

/**
 * rxe_attach_mcast - attach qp to multicast group (see IBA-11.3.1)
 * @ibqp: (IB) qp object
 * @mgid: multicast IP address
 * @mlid: multicast LID, ignored for RoCEv2 (see IBA-A17.5.6)
 *
 * Returns: 0 on success else an errno
 */
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

	err = rxe_attach_mcg(mcg, qp);
	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);

	return err;
}

/**
 * rxe_detach_mcast - detach qp from multicast group (see IBA-11.3.2)
 * @ibqp: address of (IB) qp object
 * @mgid: multicast IP address
 * @mlid: multicast LID, ignored for RoCEv2 (see IBA-A17.5.6)
 *
 * Returns: 0 on success else an errno
 */
int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);
	struct rxe_mcg *mcg;
	int err;

	mcg = rxe_lookup_mcg(rxe, mgid);
	if (!mcg)
		return -EINVAL;

	err = rxe_detach_mcg(mcg, qp);
	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);

	return err;
}

/**
 * rxe_cleanup_mcast - cleanup all resources held by mcast
 * @rxe: rxe object
 *
 * Called when rxe device is unloaded. Walk red-black tree to
 * find all mcg's and then walk mcg->qp_list to find all mca's and
 * free them. These should have been freed already if apps are
 * well behaved.
 */
void rxe_cleanup_mcast(struct rxe_dev *rxe)
{
	struct rb_root *root = &rxe->mcg_tree;
	struct rb_node *node, *next;
	struct rxe_mcg *mcg;
	struct rxe_mca *mca, *tmp;

	for (node = rb_first(root); node; node = next) {
		next = rb_next(node);
		mcg = rb_entry(node, typeof(*mcg), node);

		spin_lock_bh(&rxe->mcg_lock);
		list_for_each_entry_safe(mca, tmp, &mcg->qp_list, qp_list)
			kfree(mca);

		__rxe_remove_mcg(mcg);
		spin_unlock_bh(&rxe->mcg_lock);

		kfree(mcg);
	}
}
