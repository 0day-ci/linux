// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "iecm.h"

/**
 * iecm_statistics_task - Delayed task to get statistics over mailbox
 * @work: work_struct handle to our data
 */
static void iecm_statistics_task(struct work_struct *work)
{
	/* stub */
}

/**
 * iecm_service_task - Delayed task for handling mailbox responses
 * @work: work_struct handle to our data
 *
 */
static void iecm_service_task(struct work_struct *work)
{
	/* stub */
}

/**
 * iecm_init_task - Delayed initialization task
 * @work: work_struct handle to our data
 *
 * Init task finishes up pending work started in probe.  Due to the asynchronous
 * nature in which the device communicates with hardware, we may have to wait
 * several milliseconds to get a response.  Instead of busy polling in probe,
 * pulling it out into a delayed work task prevents us from bogging down the
 * whole system waiting for a response from hardware.
 */
static void iecm_init_task(struct work_struct *work)
{
	/* stub */
}

/**
 * iecm_deinit_task - Device deinit routine
 * @adapter: Driver specific private structue
 *
 * Extended remove logic which will be used for
 * hard reset as well
 */
static void iecm_deinit_task(struct iecm_adapter *adapter)
{
	/* stub */
}

/**
 * iecm_vc_event_task - Handle virtchannel event logic
 * @work: work queue struct
 */
static void iecm_vc_event_task(struct work_struct *work)
{
	/* stub */
}

/**
 * iecm_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in iecm_pci_tbl
 * @adapter: driver specific private structure
 *
 * Returns 0 on success, negative on failure
 */
int iecm_probe(struct pci_dev *pdev,
	       const struct pci_device_id __always_unused *ent,
	       struct iecm_adapter *adapter)
{
	int err;

	adapter->pdev = pdev;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, BIT(IECM_BAR0), pci_name(pdev));
	if (err) {
		dev_err(&pdev->dev, "BAR0 I/O map error %d\n", err);
		return err;
	}

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: 0x%x\n", err);
		return err;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);
	pci_set_drvdata(pdev, adapter);

	adapter->init_wq =
		alloc_workqueue("%s", WQ_MEM_RECLAIM, 0, KBUILD_MODNAME);
	if (!adapter->init_wq) {
		dev_err(&pdev->dev, "Failed to allocate workqueue\n");
		err = -ENOMEM;
		goto err_wq_alloc;
	}

	adapter->serv_wq =
		alloc_workqueue("%s", WQ_MEM_RECLAIM, 0, KBUILD_MODNAME);
	if (!adapter->serv_wq) {
		dev_err(&pdev->dev, "Failed to allocate workqueue\n");
		err = -ENOMEM;
		goto err_mbx_wq_alloc;
	}

	adapter->stats_wq =
		alloc_workqueue("%s", WQ_MEM_RECLAIM, 0, KBUILD_MODNAME);
	if (!adapter->stats_wq) {
		dev_err(&pdev->dev, "Failed to allocate workqueue\n");
		err = -ENOMEM;
		goto err_stats_wq_alloc;
	}
	adapter->vc_event_wq =
		alloc_workqueue("%s", WQ_MEM_RECLAIM, 0, KBUILD_MODNAME);
	if (!adapter->vc_event_wq) {
		dev_err(&pdev->dev, "Failed to allocate workqueue\n");
		err = -ENOMEM;
		goto err_vc_event_wq_alloc;
	}

	/* setup msglvl */
	adapter->msg_enable = netif_msg_init(-1, IECM_AVAIL_NETIF_M);

	adapter->vports = kcalloc(IECM_MAX_NUM_VPORTS,
				  sizeof(*adapter->vports), GFP_KERNEL);
	if (!adapter->vports) {
		err = -ENOMEM;
		goto err_vport_alloc;
	}

	adapter->netdevs = kcalloc(IECM_MAX_NUM_VPORTS,
				   sizeof(struct net_device *), GFP_KERNEL);
	if (!adapter->netdevs) {
		err = -ENOMEM;
		goto err_netdev_alloc;
	}

	mutex_init(&adapter->sw_mutex);
	mutex_init(&adapter->reset_lock);
	init_waitqueue_head(&adapter->vchnl_wq);
	init_waitqueue_head(&adapter->sw_marker_wq);

	spin_lock_init(&adapter->cloud_filter_list_lock);
	spin_lock_init(&adapter->mac_filter_list_lock);
	spin_lock_init(&adapter->vlan_list_lock);
	spin_lock_init(&adapter->adv_rss_list_lock);
	spin_lock_init(&adapter->fdir_fltr_list_lock);
	INIT_LIST_HEAD(&adapter->config_data.mac_filter_list);
	INIT_LIST_HEAD(&adapter->config_data.vlan_filter_list);
	INIT_LIST_HEAD(&adapter->config_data.adv_rss_list);

	INIT_DELAYED_WORK(&adapter->stats_task, iecm_statistics_task);
	INIT_DELAYED_WORK(&adapter->serv_task, iecm_service_task);
	INIT_DELAYED_WORK(&adapter->init_task, iecm_init_task);
	INIT_DELAYED_WORK(&adapter->vc_event_task, iecm_vc_event_task);

	set_bit(__IECM_HR_DRV_LOAD, adapter->flags);
	queue_delayed_work(adapter->vc_event_wq, &adapter->vc_event_task,
			   msecs_to_jiffies(10 * (pdev->devfn & 0x07)));

	return 0;
err_netdev_alloc:
	kfree(adapter->vports);
err_vport_alloc:
	destroy_workqueue(adapter->vc_event_wq);
err_vc_event_wq_alloc:
	destroy_workqueue(adapter->stats_wq);
err_stats_wq_alloc:
	destroy_workqueue(adapter->serv_wq);
err_mbx_wq_alloc:
	destroy_workqueue(adapter->init_wq);
err_wq_alloc:
	pci_disable_pcie_error_reporting(pdev);
	return err;
}
EXPORT_SYMBOL(iecm_probe);

/**
 * iecm_del_user_cfg_data - delete all user configuration data
 * @adapter: Driver specific private structue
 */
static void iecm_del_user_cfg_data(struct iecm_adapter *adapter)
{
	/* stub */
}

/**
 * iecm_remove - Device removal routine
 * @pdev: PCI device information struct
 */
void iecm_remove(struct pci_dev *pdev)
{
	struct iecm_adapter *adapter = pci_get_drvdata(pdev);

	if (!adapter)
		return;
	/* Wait until vc_event_task is done to consider if any hard reset is
	 * in progress else we may go ahead and release the resources but the
	 * thread doing the hard reset might continue the init path and
	 * end up in bad state.
	 */
	cancel_delayed_work_sync(&adapter->vc_event_task);
	iecm_deinit_task(adapter);
	iecm_del_user_cfg_data(adapter);
	msleep(20);
	destroy_workqueue(adapter->serv_wq);
	destroy_workqueue(adapter->vc_event_wq);
	destroy_workqueue(adapter->stats_wq);
	destroy_workqueue(adapter->init_wq);
	kfree(adapter->vports);
	kfree(adapter->netdevs);
	kfree(adapter->vlan_caps);
	mutex_destroy(&adapter->sw_mutex);
	mutex_destroy(&adapter->reset_lock);
	pci_disable_pcie_error_reporting(pdev);
	pcim_iounmap_regions(pdev, BIT(IECM_BAR0));
	pci_disable_device(pdev);
}
EXPORT_SYMBOL(iecm_remove);
