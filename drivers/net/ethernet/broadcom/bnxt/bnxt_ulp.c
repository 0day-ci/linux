/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <asm/byteorder.h>
#include <linux/bitmap.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_ulp.h"

int bnxt_register_dev(struct bnxt_en_dev *edev, struct bnxt_ulp_ops *ulp_ops,
		      void *handle)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	unsigned int max_stat_ctxs;
	struct bnxt_ulp *ulp;

	ASSERT_RTNL();

	max_stat_ctxs = bnxt_get_max_func_stat_ctxs(bp);
	if (max_stat_ctxs <= BNXT_MIN_ROCE_STAT_CTXS ||
	    bp->cp_nr_rings == max_stat_ctxs)
		return -ENOMEM;

	ulp = kzalloc(sizeof(*ulp), GFP_KERNEL);
	if (!ulp)
		return -ENOMEM;

	edev->ulp_tbl = ulp;
	ulp->handle = handle;
	ulp->ulp_ops = ulp_ops;

	if (test_bit(BNXT_STATE_OPEN, &bp->state))
		bnxt_hwrm_vnic_cfg(bp, 0);

	return 0;
}
EXPORT_SYMBOL(bnxt_register_dev);

void bnxt_unregister_dev(struct bnxt_en_dev *edev)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ulp *ulp;
	int i = 0;

	ASSERT_RTNL();

	ulp = edev->ulp_tbl;
	if (ulp->msix_requested)
		bnxt_free_msix_vecs(edev);

	if (ulp->max_async_event_id)
		bnxt_hwrm_func_drv_rgtr(bp, NULL, 0, true);

	ulp->max_async_event_id = 0;
	ulp->async_events_bmap = NULL;
	while (atomic_read(&ulp->ref_count) != 0 && i < 10) {
		msleep(100);
		i++;
	}
	kfree(ulp);
	edev->ulp_tbl = NULL;
}
EXPORT_SYMBOL(bnxt_unregister_dev);

static void bnxt_fill_msix_vecs(struct bnxt *bp, struct bnxt_msix_entry *ent)
{
	struct bnxt_en_dev *edev = bp->edev;
	int num_msix, idx, i;

	num_msix = edev->ulp_tbl->msix_requested;
	idx = edev->ulp_tbl->msix_base;
	for (i = 0; i < num_msix; i++) {
		ent[i].vector = bp->irq_tbl[idx + i].vector;
		ent[i].ring_idx = idx + i;
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			ent[i].db_offset = DB_PF_OFFSET_P5;
			if (BNXT_VF(bp))
				ent[i].db_offset = DB_VF_OFFSET_P5;
		} else {
			ent[i].db_offset = (idx + i) * 0x80;
		}
	}
}

int bnxt_req_msix_vecs(struct bnxt_en_dev *edev, struct bnxt_msix_entry *ent,
		       int num_msix)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_hw_resc *hw_resc;
	int max_idx, max_cp_rings;
	int avail_msix, idx;
	int total_vecs;
	int rc = 0;

	ASSERT_RTNL();

	if (!(bp->flags & BNXT_FLAG_USING_MSIX))
		return -ENODEV;

	if (edev->ulp_tbl->msix_requested)
		return -EAGAIN;

	max_cp_rings = bnxt_get_max_func_cp_rings(bp);
	avail_msix = bnxt_get_avail_msix(bp, num_msix);
	if (!avail_msix)
		return -ENOMEM;
	if (avail_msix > num_msix)
		avail_msix = num_msix;

	if (BNXT_NEW_RM(bp)) {
		idx = bp->cp_nr_rings;
	} else {
		max_idx = min_t(int, bp->total_irqs, max_cp_rings);
		idx = max_idx - avail_msix;
	}
	edev->ulp_tbl->msix_base = idx;
	edev->ulp_tbl->msix_requested = avail_msix;
	hw_resc = &bp->hw_resc;
	total_vecs = idx + avail_msix;
	if (bp->total_irqs < total_vecs ||
	    (BNXT_NEW_RM(bp) && hw_resc->resv_irqs < total_vecs)) {
		if (netif_running(dev)) {
			bnxt_close_nic(bp, true, false);
			rc = bnxt_open_nic(bp, true, false);
		} else {
			rc = bnxt_reserve_rings(bp, true);
		}
	}
	if (rc) {
		edev->ulp_tbl->msix_requested = 0;
		return -EAGAIN;
	}

	if (BNXT_NEW_RM(bp)) {
		int resv_msix;

		resv_msix = hw_resc->resv_irqs - bp->cp_nr_rings;
		avail_msix = min_t(int, resv_msix, avail_msix);
		edev->ulp_tbl->msix_requested = avail_msix;
	}
	bnxt_fill_msix_vecs(bp, ent);
	edev->flags |= BNXT_EN_FLAG_MSIX_REQUESTED;
	return avail_msix;
}
EXPORT_SYMBOL(bnxt_req_msix_vecs);

void bnxt_free_msix_vecs(struct bnxt_en_dev *edev)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);

	ASSERT_RTNL();

	if (!(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return;

	edev->ulp_tbl->msix_requested = 0;
	edev->flags &= ~BNXT_EN_FLAG_MSIX_REQUESTED;
	if (netif_running(dev) && !(edev->flags & BNXT_EN_FLAG_ULP_STOPPED)) {
		bnxt_close_nic(bp, true, false);
		bnxt_open_nic(bp, true, false);
	}
}
EXPORT_SYMBOL(bnxt_free_msix_vecs);

int bnxt_get_ulp_msix_num(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_en_dev *edev = bp->edev;

		return edev->ulp_tbl->msix_requested;
	}
	return 0;
}

int bnxt_get_ulp_msix_base(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_en_dev *edev = bp->edev;

		if (edev->ulp_tbl->msix_requested)
			return edev->ulp_tbl->msix_base;
	}
	return 0;
}

int bnxt_get_ulp_stat_ctxs(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_en_dev *edev = bp->edev;

		if (edev->ulp_tbl->msix_requested)
			return BNXT_MIN_ROCE_STAT_CTXS;
	}

	return 0;
}

int bnxt_send_msg(struct bnxt_en_dev *edev, struct bnxt_fw_msg *fw_msg)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct input *req;
	int rc;

	mutex_lock(&bp->hwrm_cmd_lock);
	req = fw_msg->msg;
	req->resp_addr = cpu_to_le64(bp->hwrm_cmd_resp_dma_addr);
	rc = _hwrm_send_message(bp, fw_msg->msg, fw_msg->msg_len,
				fw_msg->timeout);
	if (!rc) {
		struct output *resp = bp->hwrm_cmd_resp_addr;
		u32 len = le16_to_cpu(resp->resp_len);

		if (fw_msg->resp_max_len < len)
			len = fw_msg->resp_max_len;

		memcpy(fw_msg->resp, resp, len);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}
EXPORT_SYMBOL(bnxt_send_msg);

static void bnxt_ulp_get(struct bnxt_ulp *ulp)
{
	atomic_inc(&ulp->ref_count);
}

static void bnxt_ulp_put(struct bnxt_ulp *ulp)
{
	atomic_dec(&ulp->ref_count);
}

void bnxt_ulp_stop(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	struct bnxt_ulp *ulp;

	if (!edev)
		return;

	edev->flags |= BNXT_EN_FLAG_ULP_STOPPED;
	ulp = edev->ulp_tbl;
	ops = ulp->ulp_ops;
	if (!ops || !ops->ulp_stop)
		return;
	ops->ulp_stop(ulp->handle);
}

void bnxt_ulp_start(struct bnxt *bp, int err)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	struct bnxt_ulp *ulp;

	if (!edev)
		return;

	edev->flags &= ~BNXT_EN_FLAG_ULP_STOPPED;

	if (err)
		return;

	ulp = edev->ulp_tbl;
	ops = ulp->ulp_ops;
	if (!ops || !ops->ulp_start)
		return;
	ops->ulp_start(ulp->handle);
}

void bnxt_ulp_sriov_cfg(struct bnxt *bp, int num_vfs)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	struct bnxt_ulp *ulp;

	if (!edev)
		return;

	ulp = edev->ulp_tbl;

	ops = ulp->ulp_ops;
	if (!ops || !ops->ulp_sriov_config)
		return;

	bnxt_ulp_get(ulp);
	ops->ulp_sriov_config(ulp->handle, num_vfs);
	bnxt_ulp_put(ulp);
}

void bnxt_ulp_shutdown(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	struct bnxt_ulp *ulp;

	if (!edev)
		return;

	ulp = edev->ulp_tbl;
	ops = ulp->ulp_ops;
	if (!ops || !ops->ulp_shutdown)
		return;

	ops->ulp_shutdown(ulp->handle);
}

void bnxt_ulp_irq_stop(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;

	if (!edev || !(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return;

	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_ulp *ulp = edev->ulp_tbl;

		if (!ulp->msix_requested)
			return;

		ops = ulp->ulp_ops;
		if (!ops || !ops->ulp_irq_stop)
			return;
		ops->ulp_irq_stop(ulp->handle);
	}
}

void bnxt_ulp_irq_restart(struct bnxt *bp, int err)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;

	if (!edev || !(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return;

	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_ulp *ulp = edev->ulp_tbl;
		struct bnxt_msix_entry *ent = NULL;

		if (!ulp->msix_requested)
			return;

		ops = ulp->ulp_ops;
		if (!ops || !ops->ulp_irq_restart)
			return;

		if (!err) {
			ent = kcalloc(ulp->msix_requested, sizeof(*ent),
				      GFP_KERNEL);
			if (!ent)
				return;
			bnxt_fill_msix_vecs(bp, ent);
		}
		ops->ulp_irq_restart(ulp->handle, ent);
		kfree(ent);
	}
}

void bnxt_ulp_async_events(struct bnxt *bp, struct hwrm_async_event_cmpl *cmpl)
{
	u16 event_id = le16_to_cpu(cmpl->event_id);
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	struct bnxt_ulp *ulp;

	if (!edev)
		return;

	ulp = edev->ulp_tbl;
	ops = ulp->ulp_ops;
	if (!ops || !ops->ulp_async_notifier)
		return;

	if (!ulp->async_events_bmap || event_id > ulp->max_async_event_id)
		return;

	/* Read max_async_event_id first before testing the bitmap. */
	smp_rmb();
	if (test_bit(event_id, ulp->async_events_bmap))
		ops->ulp_async_notifier(ulp->handle, cmpl);
}

struct bnxt_en_dev *bnxt_ulp_probe(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_en_dev *edev;

	edev = bp->edev;
	if (!edev) {
		edev = kzalloc(sizeof(*edev), GFP_KERNEL);
		if (!edev)
			return ERR_PTR(-ENOMEM);
		if (bp->flags & BNXT_FLAG_ROCEV1_CAP)
			edev->flags |= BNXT_EN_FLAG_ROCEV1_CAP;
		if (bp->flags & BNXT_FLAG_ROCEV2_CAP)
			edev->flags |= BNXT_EN_FLAG_ROCEV2_CAP;
		edev->net = dev;
		edev->pdev = bp->pdev;
		edev->l2_db_size = bp->db_size;
		edev->l2_db_size_nc = bp->db_size;
		bp->edev = edev;
	}
	return bp->edev;
}
EXPORT_SYMBOL(bnxt_ulp_probe);
