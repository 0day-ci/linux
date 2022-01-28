// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "iecm.h"

const char * const iecm_vport_vc_state_str[] = {
	IECM_FOREACH_VPORT_VC_STATE(IECM_GEN_STRING)
};
EXPORT_SYMBOL(iecm_vport_vc_state_str);

/**
 * iecm_cfg_hw - Initialize HW struct
 * @adapter: adapter to setup hw struct for
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_cfg_hw(struct iecm_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct iecm_hw *hw = &adapter->hw;

	hw->hw_addr = pcim_iomap_table(pdev)[IECM_BAR0];
	if (!hw->hw_addr)
		return -EIO;
	hw->back = adapter;

	return 0;
}

/**
 * iecm_get_free_slot - get the next non-NULL location index in array
 * @array: array to search
 * @size: size of the array
 * @curr: last known occupied index to be used as a search hint
 *
 * void * is being used to keep the functionality generic. This lets us use this
 * function on any array of pointers.
 */
static int iecm_get_free_slot(void *array, int size, int curr)
{
	int **tmp_array = (int **)array;
	int next;

	if (curr < (size - 1) && !tmp_array[curr + 1]) {
		next = curr + 1;
	} else {
		int i = 0;

		while ((i < size) && (tmp_array[i]))
			i++;
		if (i == size)
			next = IECM_NO_FREE_SLOT;
		else
			next = i;
	}
	return next;
}

/**
 * iecm_vport_rel - Delete a vport and free its resources
 * @vport: the vport being removed
 */
static void iecm_vport_rel(struct iecm_vport *vport)
{
	mutex_destroy(&vport->stop_mutex);
	kfree(vport);
}

/**
 * iecm_vport_rel_all - Delete all vports
 * @adapter: adapter from which all vports are being removed
 */
static void iecm_vport_rel_all(struct iecm_adapter *adapter)
{
	int i;

	if (!adapter->vports)
		return;

	for (i = 0; i < adapter->num_alloc_vport; i++) {
		if (!adapter->vports[i])
			continue;

		iecm_vport_rel(adapter->vports[i]);
		adapter->vports[i] = NULL;
		adapter->next_vport = 0;
	}
	adapter->num_alloc_vport = 0;
}

/**
 * iecm_vport_set_hsplit - enable or disable header split on a given vport
 * @vport: virtual port
 * @ena: flag controlling header split, On (true) or Off (false)
 */
void iecm_vport_set_hsplit(struct iecm_vport *vport, bool ena)
{
	if (iecm_is_cap_ena_all(vport->adapter, IECM_HSPLIT_CAPS,
				IECM_CAP_HSPLIT) &&
	    iecm_is_queue_model_split(vport->rxq_model))
		set_bit(__IECM_PRIV_FLAGS_HDR_SPLIT,
			vport->adapter->config_data.user_flags);
}

/**
 * iecm_vport_alloc - Allocates the next available struct vport in the adapter
 * @adapter: board private structure
 * @vport_id: vport identifier
 *
 * returns a pointer to a vport on success, NULL on failure.
 */
static struct iecm_vport *
iecm_vport_alloc(struct iecm_adapter *adapter, int vport_id)
{
	struct iecm_vport *vport = NULL;

	if (adapter->next_vport == IECM_NO_FREE_SLOT)
		return vport;

	/* Need to protect the allocation of the vports at the adapter level */
	mutex_lock(&adapter->sw_mutex);

	vport = kzalloc(sizeof(*vport), GFP_KERNEL);
	if (!vport)
		goto unlock_adapter;

	vport->adapter = adapter;
	vport->idx = adapter->next_vport;
	vport->compln_clean_budget = IECM_TX_COMPLQ_CLEAN_BUDGET;
	adapter->num_alloc_vport++;

	/* Setup default MSIX irq handler for the vport */
	vport->irq_q_handler = iecm_vport_intr_clean_queues;
	vport->q_vector_base = IECM_NONQ_VEC;

	mutex_init(&vport->stop_mutex);

	/* fill vport slot in the adapter struct */
	adapter->vports[adapter->next_vport] = vport;

	/* prepare adapter->next_vport for next use */
	adapter->next_vport = iecm_get_free_slot(adapter->vports,
						 adapter->num_alloc_vport,
						 adapter->next_vport);

unlock_adapter:
	mutex_unlock(&adapter->sw_mutex);
	return vport;
}

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
	struct iecm_adapter *adapter = container_of(work,
						    struct iecm_adapter,
						    init_task.work);
	struct iecm_vport *vport;
	struct pci_dev *pdev;
	int vport_id, err;

	err = adapter->dev_ops.vc_ops.core_init(adapter, &vport_id);
	if (err)
		return;

	pdev = adapter->pdev;
	vport = iecm_vport_alloc(adapter, vport_id);
	if (!vport) {
		err = -EFAULT;
		dev_err(&pdev->dev, "failed to allocate vport: %d\n",
			err);
		return;
	}
}

/**
 * iecm_api_init - Initialize and verify device API
 * @adapter: driver specific private structure
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_api_init(struct iecm_adapter *adapter)
{
	struct iecm_reg_ops *reg_ops = &adapter->dev_ops.reg_ops;
	struct pci_dev *pdev = adapter->pdev;

	if (!adapter->dev_ops.reg_ops_init) {
		dev_err(&pdev->dev, "Invalid device, register API init not defined\n");
		return -EINVAL;
	}
	adapter->dev_ops.reg_ops_init(adapter);
	if (!(reg_ops->ctlq_reg_init && reg_ops->intr_reg_init &&
	      reg_ops->mb_intr_reg_init && reg_ops->reset_reg_init &&
	      reg_ops->trigger_reset)) {
		dev_err(&pdev->dev, "Invalid device, missing one or more register functions\n");
		return -EINVAL;
	}

	if (adapter->dev_ops.vc_ops_init) {
		struct iecm_virtchnl_ops *vc_ops;

		adapter->dev_ops.vc_ops_init(adapter);
		vc_ops = &adapter->dev_ops.vc_ops;
		if (!(vc_ops->core_init &&
		      vc_ops->vport_init &&
		      vc_ops->vport_queue_ids_init &&
		      vc_ops->get_caps &&
		      vc_ops->config_queues &&
		      vc_ops->enable_queues &&
		      vc_ops->disable_queues &&
		      vc_ops->irq_map_unmap &&
		      vc_ops->get_set_rss_lut &&
		      vc_ops->get_set_rss_hash &&
		      vc_ops->adjust_qs &&
		      vc_ops->get_ptype &&
		      vc_ops->init_max_queues)) {
			dev_err(&pdev->dev, "Invalid device, missing one or more virtchnl functions\n");
			return -EINVAL;
		}
	} else {
		iecm_vc_ops_init(adapter);
	}

	return 0;
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
	set_bit(__IECM_REL_RES_IN_PROG, adapter->flags);
	/* Wait until the init_task is done else this thread might release
	 * the resources first and the other thread might end up in a bad state
	 */
	cancel_delayed_work_sync(&adapter->init_task);
	iecm_vport_rel_all(adapter);

	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->stats_task);
}

/**
 * iecm_check_reset_complete - check that reset is complete
 * @hw: pointer to hw struct
 * @reset_reg: struct with reset registers
 *
 * Returns 0 if device is ready to use, or -EBUSY if it's in reset.
 **/
static int iecm_check_reset_complete(struct iecm_hw *hw,
				     struct iecm_reset_reg *reset_reg)
{
	struct iecm_adapter *adapter = (struct iecm_adapter *)hw->back;
	int i;

	for (i = 0; i < 2000; i++) {
		u32 reg_val = rd32(hw, reset_reg->rstat);

		/* 0xFFFFFFFF might be read if other side hasn't cleared the
		 * register for us yet and 0xFFFFFFFF is not a valid value for
		 * the register, so treat that as invalid.
		 */
		if (reg_val != 0xFFFFFFFF && (reg_val & reset_reg->rstat_m))
			return 0;
		usleep_range(5000, 10000);
	}

	dev_warn(&adapter->pdev->dev, "Device reset timeout!\n");
	return -EBUSY;
}

/**
 * iecm_init_hard_reset - Initiate a hardware reset
 * @adapter: Driver specific private structure
 *
 * Deallocate the vports and all the resources associated with them and
 * reallocate. Also reinitialize the mailbox. Return 0 on success,
 * negative on failure.
 */
static int iecm_init_hard_reset(struct iecm_adapter *adapter)
{
	int err = 0;

	mutex_lock(&adapter->reset_lock);

	/* Prepare for reset */
	if (test_and_clear_bit(__IECM_HR_DRV_LOAD, adapter->flags)) {
		adapter->dev_ops.reg_ops.trigger_reset(adapter,
						       __IECM_HR_DRV_LOAD);
	} else if (test_and_clear_bit(__IECM_HR_FUNC_RESET, adapter->flags)) {
		bool is_reset = iecm_is_reset_detected(adapter);

		if (adapter->state == __IECM_UP)
			set_bit(__IECM_UP_REQUESTED, adapter->flags);
		iecm_deinit_task(adapter);
		if (!is_reset)
			adapter->dev_ops.reg_ops.trigger_reset(adapter,
							       __IECM_HR_FUNC_RESET);
		iecm_deinit_dflt_mbx(adapter);
	} else if (test_and_clear_bit(__IECM_HR_CORE_RESET, adapter->flags)) {
		if (adapter->state == __IECM_UP)
			set_bit(__IECM_UP_REQUESTED, adapter->flags);
		iecm_deinit_task(adapter);
	} else {
		dev_err(&adapter->pdev->dev, "Unhandled hard reset cause\n");
		err = -EBADRQC;
		goto handle_err;
	}

	/* Wait for reset to complete */
	err = iecm_check_reset_complete(&adapter->hw, &adapter->reset_reg);
	if (err) {
		dev_err(&adapter->pdev->dev, "The driver was unable to contact the device's firmware.  Check that the FW is running. Driver state=%u\n",
			adapter->state);
		goto handle_err;
	}

	/* Reset is complete and so start building the driver resources again */
	err = iecm_init_dflt_mbx(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to initialize default mailbox: %d\n",
			err);
	}
handle_err:
	mutex_unlock(&adapter->reset_lock);
	return err;
}

/**
 * iecm_vc_event_task - Handle virtchannel event logic
 * @work: work queue struct
 */
static void iecm_vc_event_task(struct work_struct *work)
{
	struct iecm_adapter *adapter = container_of(work,
						    struct iecm_adapter,
						    vc_event_task.work);

	if (test_bit(__IECM_HR_CORE_RESET, adapter->flags) ||
	    test_bit(__IECM_HR_FUNC_RESET, adapter->flags) ||
	    test_bit(__IECM_HR_DRV_LOAD, adapter->flags)) {
		set_bit(__IECM_HR_RESET_IN_PROG, adapter->flags);
		iecm_init_hard_reset(adapter);
	}
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
	err = iecm_api_init(adapter);
	if (err) {
		dev_err(&pdev->dev, "Device API is incorrectly configured\n");
		return err;
	}

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

	err = iecm_vport_params_buf_alloc(adapter);
	if (err) {
		dev_err(&pdev->dev, "Failed to alloc vport params buffer: %d\n",
			err);
		goto err_mb_res;
	}

	err = iecm_cfg_hw(adapter);
	if (err) {
		dev_err(&pdev->dev, "Failed to configure HW structure for adapter: %d\n",
			err);
		goto err_cfg_hw;
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

	adapter->dev_ops.reg_ops.reset_reg_init(&adapter->reset_reg);
	set_bit(__IECM_HR_DRV_LOAD, adapter->flags);
	queue_delayed_work(adapter->vc_event_wq, &adapter->vc_event_task,
			   msecs_to_jiffies(10 * (pdev->devfn & 0x07)));

	return 0;
err_cfg_hw:
	iecm_vport_params_buf_rel(adapter);
err_mb_res:
	kfree(adapter->netdevs);
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
	iecm_deinit_dflt_mbx(adapter);
	msleep(20);
	destroy_workqueue(adapter->serv_wq);
	destroy_workqueue(adapter->vc_event_wq);
	destroy_workqueue(adapter->stats_wq);
	destroy_workqueue(adapter->init_wq);
	kfree(adapter->vports);
	kfree(adapter->netdevs);
	kfree(adapter->vlan_caps);
	iecm_vport_params_buf_rel(adapter);
	mutex_destroy(&adapter->sw_mutex);
	mutex_destroy(&adapter->reset_lock);
	pci_disable_pcie_error_reporting(pdev);
	pcim_iounmap_regions(pdev, BIT(IECM_BAR0));
	pci_disable_device(pdev);
}
EXPORT_SYMBOL(iecm_remove);

void *iecm_alloc_dma_mem(struct iecm_hw *hw, struct iecm_dma_mem *mem, u64 size)
{
	struct iecm_adapter *adapter = (struct iecm_adapter *)hw->back;
	size_t sz = ALIGN(size, 4096);

	mem->va = dma_alloc_coherent(&adapter->pdev->dev, sz,
				     &mem->pa, GFP_KERNEL | __GFP_ZERO);
	mem->size = size;

	return mem->va;
}

void iecm_free_dma_mem(struct iecm_hw *hw, struct iecm_dma_mem *mem)
{
	struct iecm_adapter *adapter = (struct iecm_adapter *)hw->back;

	dma_free_coherent(&adapter->pdev->dev, mem->size,
			  mem->va, mem->pa);
	mem->size = 0;
	mem->va = NULL;
	mem->pa = 0;
}
