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
 * attached to a new mgid for the first time. These are held in a red-black
 * tree and indexed by the mgid. This data structure is searched for
 * the mcast group when a multicast packet is received and when another
 * qp is attached to the same mgid. It is cleaned up when the last qp
 * is detached from the mcg. Each time a qp is attached to an mcg
 * an mca is created to hold pointers to the qp and
 * the mcg and is added to two lists. One is a list of mcg's
 * attached to by the qp and the other is the list of qp's attached
 * to the mcg. mcg's are reference counted and once the count goes to
 * zero it is inactive and will be cleaned up.
 *
 * The qp list is protected by mcg->lock while the other data
 * structures are protected by rxe->mcg_lock. The performance critical
 * path of processing multicast packets only requres holding the mcg->lock
 * while the multicast related verbs APIs require holding both the locks.
 */

#include "rxe.h"

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
 * __rxe_insert_mcg() - insert an mcg into red-black tree (rxe->mcg_tree)
 * @mcg: mcast group object with an embedded red-black tree node
 *
 * Context: caller must hold rxe->mcg_lock and must first search
 * the tree to see if the mcg is already present.
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

static void __rxe_remove_mcg(struct rxe_mcg *mcg)
{
	rb_erase(&mcg->node, &mcg->rxe->mcg_tree);
}

/**
 * __rxe_lookup_mcg() - lookup mcg in rxe->mcg_tree while holding lock
 * @rxe: rxe device object
 * @mgid: multicast IP address
 *
 * Context: caller must hold rxe->mcg_lock
 * Returns: mcg on success or NULL
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

	if (node && kref_get_unless_zero(&mcg->ref_cnt))
		return mcg;

	return NULL;
}

/**
 * rxe_lookup_mcg() - lookup up mcast group from mgid
 * @rxe: rxe device object
 * @mgid: multicast IP address
 *
 * Returns: mcg if found else NULL
 */
struct rxe_mcg *rxe_lookup_mcg(struct rxe_dev *rxe,
					   union ib_gid *mgid)
{
	struct rxe_mcg *mcg;

	spin_lock_bh(&rxe->mcg_lock);
	mcg = __rxe_lookup_mcg(rxe, mgid);
	spin_unlock_bh(&rxe->mcg_lock);

	return mcg;
}

/**
 * rxe_get_mcg() - lookup or allocate a mcg
 * @rxe: rxe device object
 * @mgid: multicast IP address
 * @mcgg: address of returned mcg value
 *
 * Returns: 0 on success else an error
 */
static int rxe_get_mcg(struct rxe_dev *rxe, union ib_gid *mgid,
		       struct rxe_mcg **mcgp)
{
	struct rxe_mcg *mcg, *tmp;
	int err;

	if (rxe->attr.max_mcast_grp == 0)
		return -EINVAL;

	mcg = rxe_lookup_mcg(rxe, mgid);
	if (mcg)
		goto done;

	mcg = kzalloc(sizeof(*mcg), GFP_KERNEL);
	if (!mcg)
		return -ENOMEM;

	spin_lock_bh(&rxe->mcg_lock);
	tmp = __rxe_lookup_mcg(rxe, mgid);
	if (unlikely(tmp)) {
		/* another thread just added this mcg, use that one */
		spin_unlock_bh(&rxe->mcg_lock);
		kfree(mcg);
		mcg = tmp;
		goto done;
	}

	if (rxe->num_mcg >= rxe->attr.max_mcast_grp) {
		err = -ENOMEM;
		goto err_out;
	}

	err = rxe_mcast_add(rxe, mgid);
	if (unlikely(err))
		goto err_out;

	INIT_LIST_HEAD(&mcg->qp_list);
	spin_lock_init(&mcg->lock);
	mcg->rxe = rxe;
	memcpy(&mcg->mgid, mgid, sizeof(*mgid));
	kref_init(&mcg->ref_cnt);
	__rxe_insert_mcg(mcg);
	spin_unlock_bh(&rxe->mcg_lock);
done:
	*mcgp = mcg;
	return 0;
err_out:
	spin_unlock_bh(&rxe->mcg_lock);
	kfree(mcg);
	return err;
}

/**
 * rxe_attach_mcg() - attach qp to mcg
 * @qp: qp object
 * @mcg: mcg object
 *
 * Context: caller must hold reference on qp and mcg.
 * Returns: 0 on success else an error
 */
static int rxe_attach_mcg(struct rxe_qp *qp, struct rxe_mcg *mcg)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_mca *mca;
	int err;

	spin_lock_bh(&rxe->mcg_lock);
	spin_lock_bh(&mcg->lock);
	list_for_each_entry(mca, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			err = 0;
			goto out;
		}
	}

	if (rxe->num_attach >= rxe->attr.max_total_mcast_qp_attach
	    || mcg->num_qp >= rxe->attr.max_mcast_qp_attach) {
		err = -ENOMEM;
		goto out;
	}

	mca = kzalloc(sizeof(*mca), GFP_KERNEL);
	if (!mca) {
		err = -ENOMEM;
		goto out;
	}

	/* each mca holds a ref on mcg and qp */
	kref_get(&mcg->ref_cnt);
	rxe_add_ref(qp);

	mcg->num_qp++;
	rxe->num_attach++;
	mca->qp = qp;
	mca->mcg = mcg;

	list_add(&mca->qp_list, &mcg->qp_list);
	list_add(&mca->mcg_list, &qp->mcg_list);

	err = 0;
out:
	spin_unlock_bh(&mcg->lock);
	spin_unlock_bh(&rxe->mcg_lock);
	return err;
}

/**
 * __rxe_cleanup_mca() - cleanup mca object
 * @mca: mca object
 *
 * Context: caller holds rxe->mcg_lock and holds at least one reference
 * to mca->mcg from the mca object and one from the rxe_get_mcg()
 * call. If this is the last attachment to the mcast mcg object then
 * drop the last refernece to it.
 * Returns: 1 if the mcg is finished and needs to be cleaned up else 0.
 */
static void __rxe_cleanup_mca(struct rxe_mca *mca)
{
	struct rxe_mcg *mcg = mca->mcg;
	struct rxe_dev *rxe = mcg->rxe;

	list_del(&mca->qp_list);
	list_del(&mca->mcg_list);
	rxe_drop_ref(mca->qp);
	kfree(mca);
	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);
	rxe->num_attach--;
	if (--mcg->num_qp <= 0)
		kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);
}

/**
 * rxe_detach_mcg() - detach qp from mcg
 * @qp: qp object
 * @mcg: mcg object
 *
 * Context: caller must hold reference to qp and mcg.
 * Returns: 0 on success else an error.
 */
static int rxe_detach_mcg(struct rxe_qp *qp, struct rxe_mcg *mcg)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_mca *mca, *tmp;
	int ret = -EINVAL;

	spin_lock_bh(&rxe->mcg_lock);
	spin_lock_bh(&mcg->lock);

	list_for_each_entry_safe(mca, tmp, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			__rxe_cleanup_mca(mca);
			ret = 0;
			goto done;
		}
	}
done:
	spin_unlock_bh(&mcg->lock);
	spin_unlock_bh(&rxe->mcg_lock);
	return ret;
}

/**
 * rxe_attach_mcast() - attach qp to multicast group (see IBA-11.3.1)
 * @ibqp: (IB) qp object
 * @mgid: multicast IP address
 * @mlid: multicast LID, ignored for RoCEv2 (see IBA-A17.5.6)
 *
 * Returns: 0 on success else an errno
 */
int rxe_attach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);
	struct rxe_mcg *mcg;
	int err;

	err = rxe_get_mcg(rxe, mgid, &mcg);
	if (err)
		return err;

	err = rxe_attach_mcg(qp, mcg);
	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);

	return err;
}

/**
 * rxe_detach_mcast() - detach qp from multicast group (see IBA-11.3.2)
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

	err = rxe_detach_mcg(qp, mcg);
	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);

	return err;
}

/**
 * rxe_cleanup_mcast() - cleanup all mcg's qp is attached to
 * @qp: qp object
 */
void rxe_cleanup_mcast(struct rxe_qp *qp)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_mca *mca;
	struct rxe_mcg *mcg;

	while (1) {
		spin_lock_bh(&rxe->mcg_lock);
		if (list_empty(&qp->mcg_list)) {
			spin_unlock_bh(&rxe->mcg_lock);
			return;
		}
		mca = list_first_entry(&qp->mcg_list, typeof(*mca), mcg_list);
		mcg = mca->mcg;
		spin_lock_bh(&mcg->lock);
		__rxe_cleanup_mca(mca);
		spin_unlock_bh(&mcg->lock);
		spin_unlock_bh(&rxe->mcg_lock);
	}
}

/**
 * rxe_cleanup_mcg() - cleanup mcg object
 * @mcg: mcg object
 *
 * Context: caller has removed all references to mcg
 */
void rxe_cleanup_mcg(struct kref *kref)
{
	struct rxe_mcg *mcg = container_of(kref, typeof(*mcg), ref_cnt);
	struct rxe_dev *rxe = mcg->rxe;

	__rxe_remove_mcg(mcg);
	rxe_mcast_delete(rxe, &mcg->mgid);
	kfree(mcg);
}
