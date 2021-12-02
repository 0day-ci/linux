// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"
#include "rxe_loc.h"

int rxe_init_grp(struct rxe_pool_elem *elem)
{
	struct rxe_dev *rxe = elem->pool->rxe;
	struct rxe_mc_grp *grp = elem->obj;
	int err;

	INIT_LIST_HEAD(&grp->qp_list);
	spin_lock_init(&grp->mcg_lock);
	grp->rxe = rxe;

	err = rxe_mcast_add(rxe, &grp->mgid);
	if (err)
		rxe_drop_ref(grp);

	return err;
}

int rxe_mcast_get_grp(struct rxe_dev *rxe, union ib_gid *mgid,
		      struct rxe_mc_grp **grp_p)
{
	struct rxe_pool *pool = &rxe->mc_grp_pool;
	struct rxe_mc_grp *grp;

	if (rxe->attr.max_mcast_qp_attach == 0)
		return -EINVAL;

	grp = rxe_pool_add_key(pool, mgid);
	if (!grp)
		return -EINVAL;

	*grp_p = grp;

	return 0;
}

int rxe_mcast_add_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			   struct rxe_mc_grp *grp)
{
	int err;
	struct rxe_mc_elem *elem;

	/* check to see of the qp is already a member of the group */
	spin_lock_bh(&qp->grp_lock);
	spin_lock_bh(&grp->mcg_lock);
	list_for_each_entry(elem, &grp->qp_list, qp_list) {
		if (elem->qp == qp) {
			err = 0;
			goto out;
		}
	}

	if (grp->num_qp >= rxe->attr.max_mcast_qp_attach) {
		err = -ENOMEM;
		goto out;
	}

	elem = kzalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem) {
		err = -ENOMEM;
		goto out;
	}

	/* each elem holds a ref on the grp and the qp */
	rxe_add_ref(grp);
	rxe_add_ref(qp);

	grp->num_qp++;
	elem->qp = qp;
	elem->grp = grp;

	list_add(&elem->qp_list, &grp->qp_list);
	list_add(&elem->grp_list, &qp->grp_list);

	err = 0;
out:
	spin_unlock_bh(&grp->mcg_lock);
	spin_unlock_bh(&qp->grp_lock);
	return err;
}

int rxe_mcast_drop_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			    union ib_gid *mgid)
{
	struct rxe_mc_grp *grp;
	struct rxe_mc_elem *elem, *tmp;
	int ret = -EINVAL;

	grp = rxe_pool_get_key(&rxe->mc_grp_pool, mgid);
	if (!grp)
		goto err1;

	spin_lock_bh(&qp->grp_lock);
	spin_lock_bh(&grp->mcg_lock);

	list_for_each_entry_safe(elem, tmp, &grp->qp_list, qp_list) {
		if (elem->qp == qp) {
			list_del(&elem->qp_list);
			list_del(&elem->grp_list);
			grp->num_qp--;

			spin_unlock_bh(&grp->mcg_lock);
			spin_unlock_bh(&qp->grp_lock);
			kfree(elem);
			rxe_drop_ref(qp);	/* ref held by elem */
			rxe_drop_ref(grp);	/* ref held by elem */
			ret = 0;
			goto out_drop_ref;
		}
	}

	spin_unlock_bh(&grp->mcg_lock);
	spin_unlock_bh(&qp->grp_lock);

out_drop_ref:
	rxe_drop_ref(grp);			/* ref from get_key */
	if (grp->elem.complete.done)
		rxe_fini(grp);
err1:
	return ret;
}

void rxe_drop_all_mcast_groups(struct rxe_qp *qp)
{
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
		rxe_drop_ref(qp);
		rxe_drop_ref(grp);
		if (grp->elem.complete.done)
			rxe_fini(grp);
		kfree(elem);
	}
}

void rxe_mc_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_mc_grp *grp = container_of(elem, typeof(*grp), elem);
	struct rxe_dev *rxe = grp->rxe;

	rxe_mcast_delete(rxe, &grp->mgid);
}
