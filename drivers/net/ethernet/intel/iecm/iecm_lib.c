// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "iecm.h"

static const struct net_device_ops iecm_netdev_ops_splitq;
static const struct net_device_ops iecm_netdev_ops_singleq;

const char * const iecm_vport_vc_state_str[] = {
	IECM_FOREACH_VPORT_VC_STATE(IECM_GEN_STRING)
};
EXPORT_SYMBOL(iecm_vport_vc_state_str);

/**
 * iecm_get_vport_index - Get the vport index
 * @adapter: adapter structure to get the vports array
 * @vport: vport pointer for which the index to find
 */
static int iecm_get_vport_index(struct iecm_adapter *adapter,
				struct iecm_vport *vport)
{
	int i, err = -EINVAL;

	if (!adapter->vports)
		return err;

	for (i = 0; i < adapter->num_alloc_vport; i++) {
		if (adapter->vports[i] != vport)
			continue;
		return i;
	}
	return err;
}

/**
 * iecm_is_feature_ena - Determine if a particular feature is enabled
 * @vport: vport to check
 * @feature: netdev flag to check
 *
 * Returns true or false if a particular feature is enabled.
 */
bool iecm_is_feature_ena(struct iecm_vport *vport, netdev_features_t feature)
{
	bool ena;

	switch (feature) {
	default:
		ena = vport->netdev->features & feature;
		break;
	}
	return ena;
}

/**
 * iecm_is_vlan_cap_ena - Check if VLAN capability is enabled
 * @adapter: pointer to adapter
 * @vcaps: VLAN capability bit
 *
 * Returns true if VLAN capability is set, false otherwise
 */
static bool iecm_is_vlan_cap_ena(struct iecm_adapter *adapter,
				 enum iecm_vlan_caps vcaps)
{
	if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_VLAN)) {
		struct virtchnl_vlan_supported_caps *offload;

		if (!adapter->vlan_caps)
			return false;

		switch (vcaps) {
		case IECM_CAP_VLAN_CTAG_INSERT:
			offload =
			&adapter->vlan_caps->offloads.insertion_support;
			if ((offload->outer & IECM_VLAN_8100) == IECM_VLAN_8100 ||
			    (offload->inner & IECM_VLAN_8100) == IECM_VLAN_8100)
				return true;
			break;
		case IECM_CAP_VLAN_STAG_INSERT:
			offload =
			&adapter->vlan_caps->offloads.insertion_support;
			if ((offload->outer & IECM_VLAN_88A8) == IECM_VLAN_88A8)
				return true;
			break;
		case IECM_CAP_VLAN_CTAG_STRIP:
			offload =
			&adapter->vlan_caps->offloads.stripping_support;
			if ((offload->outer & IECM_VLAN_8100) == IECM_VLAN_8100 ||
			    (offload->inner & IECM_VLAN_8100) == IECM_VLAN_8100)
				return true;
			break;
		case IECM_CAP_VLAN_STAG_STRIP:
			offload =
			&adapter->vlan_caps->offloads.stripping_support;
			if ((offload->outer & IECM_VLAN_88A8) == IECM_VLAN_88A8)
				return true;
			break;
		case IECM_CAP_VLAN_CTAG_ADD_DEL:
			offload =
			&adapter->vlan_caps->filtering.filtering_support;
			if ((offload->outer & VIRTCHNL_VLAN_ETHERTYPE_8100) ||
			    (offload->inner & VIRTCHNL_VLAN_ETHERTYPE_8100))
				return true;
			break;
		case IECM_CAP_VLAN_STAG_ADD_DEL:
			offload =
			&adapter->vlan_caps->filtering.filtering_support;
			if ((offload->outer & VIRTCHNL_VLAN_ETHERTYPE_88A8) ||
			    (offload->inner & VIRTCHNL_VLAN_ETHERTYPE_88A8))
				return true;
			break;
		default:
			dev_err(&adapter->pdev->dev, "Invalid VLAN capability %d\n",
				vcaps);
			return false;
		}
	} else if (iecm_is_cap_ena(adapter, IECM_BASE_CAPS,
				   VIRTCHNL2_CAP_VLAN)) {
		switch (vcaps) {
		case IECM_CAP_VLAN_CTAG_INSERT:
		case IECM_CAP_VLAN_CTAG_STRIP:
		case IECM_CAP_VLAN_CTAG_ADD_DEL:
			return true;
		default:
			return false;
		}
	}

	return false;
}

/**
 * iecm_netdev_to_vport - get a vport handle from a netdev
 * @netdev: network interface device structure
 */
struct iecm_vport *iecm_netdev_to_vport(struct net_device *netdev)
{
	struct iecm_netdev_priv *np = netdev_priv(netdev);

	return np->vport;
}

/**
 * iecm_netdev_to_adapter - get an adapter handle from a netdev
 * @netdev: network interface device structure
 */
struct iecm_adapter *iecm_netdev_to_adapter(struct net_device *netdev)
{
	struct iecm_netdev_priv *np = netdev_priv(netdev);

	return np->vport->adapter;
}

/**
 * iecm_mb_intr_rel_irq - Free the IRQ association with the OS
 * @adapter: adapter structure
 */
static void iecm_mb_intr_rel_irq(struct iecm_adapter *adapter)
{
	int irq_num;

	irq_num = adapter->msix_entries[0].vector;
	free_irq(irq_num, adapter);
}

/**
 * iecm_intr_rel - Release interrupt capabilities and free memory
 * @adapter: adapter to disable interrupts on
 */
static void iecm_intr_rel(struct iecm_adapter *adapter)
{
	if (!adapter->msix_entries)
		return;
	clear_bit(__IECM_MB_INTR_MODE, adapter->flags);
	clear_bit(__IECM_MB_INTR_TRIGGER, adapter->flags);
	iecm_mb_intr_rel_irq(adapter);

	pci_free_irq_vectors(adapter->pdev);
	if (adapter->dev_ops.vc_ops.dealloc_vectors) {
		int err;

		err = adapter->dev_ops.vc_ops.dealloc_vectors(adapter);
		if (err) {
			dev_err(&adapter->pdev->dev,
				"Failed to deallocate vectors: %d\n", err);
		}
	}
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
	kfree(adapter->req_vec_chunks);
	adapter->req_vec_chunks = NULL;
}

/**
 * iecm_mb_intr_clean - Interrupt handler for the mailbox
 * @irq: interrupt number
 * @data: pointer to the adapter structure
 */
static irqreturn_t iecm_mb_intr_clean(int __always_unused irq, void *data)
{
	struct iecm_adapter *adapter = (struct iecm_adapter *)data;

	set_bit(__IECM_MB_INTR_TRIGGER, adapter->flags);
	queue_delayed_work(adapter->serv_wq, &adapter->serv_task,
			   msecs_to_jiffies(0));
	return IRQ_HANDLED;
}

/**
 * iecm_mb_irq_enable - Enable MSIX interrupt for the mailbox
 * @adapter: adapter to get the hardware address for register write
 */
static void iecm_mb_irq_enable(struct iecm_adapter *adapter)
{
	struct iecm_hw *hw = &adapter->hw;
	struct iecm_intr_reg *intr = &adapter->mb_vector.intr_reg;
	u32 val;

	val = intr->dyn_ctl_intena_m | intr->dyn_ctl_itridx_m;
	wr32(hw, intr->dyn_ctl, val);
	wr32(hw, intr->icr_ena, intr->icr_ena_ctlq_m);
}

/**
 * iecm_mb_intr_req_irq - Request irq for the mailbox interrupt
 * @adapter: adapter structure to pass to the mailbox irq handler
 */
static int iecm_mb_intr_req_irq(struct iecm_adapter *adapter)
{
	struct iecm_q_vector *mb_vector = &adapter->mb_vector;
	int irq_num, mb_vidx = 0, err;

	irq_num = adapter->msix_entries[mb_vidx].vector;
	snprintf(mb_vector->name, sizeof(mb_vector->name) - 1,
		 "%s-%s-%d", dev_driver_string(&adapter->pdev->dev),
		 "Mailbox", mb_vidx);
	err = request_irq(irq_num, adapter->irq_mb_handler, 0,
			  mb_vector->name, adapter);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Request_irq for mailbox failed, error: %d\n", err);
		return err;
	}
	set_bit(__IECM_MB_INTR_MODE, adapter->flags);
	return 0;
}

/**
 * iecm_get_mb_vec_id - Get vector index for mailbox
 * @adapter: adapter structure to access the vector chunks
 *
 * The first vector id in the requested vector chunks from the CP is for
 * the mailbox
 */
static void iecm_get_mb_vec_id(struct iecm_adapter *adapter)
{
	if (adapter->req_vec_chunks) {
		struct virtchnl2_get_capabilities *caps;

		caps = (struct virtchnl2_get_capabilities *)adapter->caps;
		adapter->mb_vector.v_idx = le16_to_cpu(caps->mailbox_vector_id);
	} else {
		adapter->mb_vector.v_idx = 0;
	}
}

/**
 * iecm_mb_intr_init - Initialize the mailbox interrupt
 * @adapter: adapter structure to store the mailbox vector
 */
static int iecm_mb_intr_init(struct iecm_adapter *adapter)
{
	adapter->dev_ops.reg_ops.mb_intr_reg_init(adapter);
	adapter->irq_mb_handler = iecm_mb_intr_clean;
	return iecm_mb_intr_req_irq(adapter);
}

/**
 * iecm_intr_distribute - Distribute MSIX vectors
 * @adapter: adapter structure to get the vports
 * @pre_req: before or after msi request
 *
 * Distribute the MSIX vectors acquired from the OS to the vports based on the
 * num of vectors requested by each vport
 */
static int
iecm_intr_distribute(struct iecm_adapter *adapter, bool pre_req)
{
	struct iecm_vport *vport = adapter->vports[0];
	int err = 0;

	if (pre_req) {
		u16 vecs_avail;

		vecs_avail = iecm_get_reserved_vecs(adapter);
		if (vecs_avail < IECM_MIN_VEC) {
			return -EAGAIN;
		} else if (vecs_avail == IECM_MIN_VEC) {
			vport->num_q_vectors = IECM_MIN_Q_VEC;
		} else {
			vport->num_q_vectors = vecs_avail - IECM_NONQ_VEC -
						IECM_MAX_RDMA_VEC;
		}
	} else {
		if (adapter->num_msix_entries != adapter->num_req_msix)
			vport->num_q_vectors = adapter->num_msix_entries -
					       IECM_NONQ_VEC;
	}

	return err;
}

/**
 * iecm_intr_req - Request interrupt capabilities
 * @adapter: adapter to enable interrupts on
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_intr_req(struct iecm_adapter *adapter)
{
	int min_vectors, max_vectors, err = 0;
	int num_q_vecs, total_num_vecs;
	u16 vecids[IECM_MAX_VECIDS];
	unsigned int vector;
	int v_actual;

	err = iecm_intr_distribute(adapter, true);
	if (err)
		return err;

	num_q_vecs = adapter->vports[0]->num_q_vectors;

	total_num_vecs = num_q_vecs + IECM_NONQ_VEC;

	if (adapter->dev_ops.vc_ops.alloc_vectors) {
		err = adapter->dev_ops.vc_ops.alloc_vectors(adapter,
							    num_q_vecs);
		if (err) {
			dev_err(&adapter->pdev->dev,
				"Failed to allocate vectors: %d\n", err);
			return -EAGAIN;
		}
	}

	min_vectors = IECM_MIN_VEC;
	max_vectors = total_num_vecs;
	v_actual = pci_alloc_irq_vectors(adapter->pdev, min_vectors,
					 max_vectors, PCI_IRQ_MSIX);
	if (v_actual < 0) {
		dev_err(&adapter->pdev->dev, "Failed to allocate MSIX vectors: %d\n",
			v_actual);
		if (adapter->dev_ops.vc_ops.dealloc_vectors)
			adapter->dev_ops.vc_ops.dealloc_vectors(adapter);
		return -EAGAIN;
	}

	adapter->msix_entries = kcalloc(v_actual, sizeof(struct msix_entry),
					GFP_KERNEL);

	if (!adapter->msix_entries) {
		pci_free_irq_vectors(adapter->pdev);
		if (adapter->dev_ops.vc_ops.dealloc_vectors)
			adapter->dev_ops.vc_ops.dealloc_vectors(adapter);
		return -ENOMEM;
	}

	iecm_get_mb_vec_id(adapter);

	if (adapter->req_vec_chunks) {
		struct virtchnl2_vector_chunks *vchunks;
		struct virtchnl2_alloc_vectors *ac;

		ac = adapter->req_vec_chunks;
		vchunks = &ac->vchunks;

		iecm_get_vec_ids(adapter, vecids, IECM_MAX_VECIDS, vchunks);
	} else {
		int i = 0;

		for (i = 0; i < v_actual; i++)
			vecids[i] = i;
	}

	for (vector = 0; vector < v_actual; vector++) {
		adapter->msix_entries[vector].entry = vecids[vector];
		adapter->msix_entries[vector].vector =
			pci_irq_vector(adapter->pdev, vector);
	}
	adapter->num_msix_entries = v_actual;
	adapter->num_req_msix = total_num_vecs;

	iecm_intr_distribute(adapter, false);

	err = iecm_mb_intr_init(adapter);
	if (err)
		goto intr_rel;
	iecm_mb_irq_enable(adapter);
	return err;

intr_rel:
	iecm_intr_rel(adapter);
	return err;
}

/**
 * iecm_find_mac_filter - Search filter list for specific mac filter
 * @vport: main vport structure
 * @macaddr: the MAC address
 *
 * Returns ptr to the filter object or NULL. Must be called while holding the
 * mac_filter_list_lock.
 **/
static struct
iecm_mac_filter *iecm_find_mac_filter(struct iecm_vport *vport,
				      const u8 *macaddr)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_mac_filter *f;

	if (!macaddr)
		return NULL;

	list_for_each_entry(f, &adapter->config_data.mac_filter_list, list) {
		if (ether_addr_equal(macaddr, f->macaddr))
			return f;
	}
	return NULL;
}

/**
 * __iecm_del_mac_filter - Delete MAC filter helper
 * @vport: main vport struct
 * @macaddr: address to delete
 *
 * Takes mac_filter_list_lock spinlock to set remove field for filter in list.
 */
static struct
iecm_mac_filter *__iecm_del_mac_filter(struct iecm_vport *vport,
				       const u8 *macaddr)
{
	struct iecm_mac_filter *f;

	spin_lock_bh(&vport->adapter->mac_filter_list_lock);
	f = iecm_find_mac_filter(vport, macaddr);
	if (f) {
		/* If filter was never synced to HW we can just delete it here,
		 * otherwise mark for removal.
		 */
		if (f->add) {
			list_del(&f->list);
			kfree(f);
			f = NULL;
		} else {
			f->remove = true;
		}
	}
	spin_unlock_bh(&vport->adapter->mac_filter_list_lock);

	return f;
}

/**
 * iecm_del_mac_filter - Delete a MAC filter from the filter list
 * @vport: main vport structure
 * @macaddr: the MAC address
 *
 * Removes filter from list and if interface is up, tells hardware about the
 * removed filter.
 **/
static void iecm_del_mac_filter(struct iecm_vport *vport, const u8 *macaddr)
{
	struct iecm_mac_filter *f;

	if (!macaddr)
		return;

	f = __iecm_del_mac_filter(vport, macaddr);
	if (!f)
		return;

	if (vport->adapter->state == __IECM_UP)
		iecm_add_del_ether_addrs(vport, false, false);
}

/**
 * __iecm_add_mac_filter - Add mac filter helper function
 * @vport: main vport struct
 * @macaddr: address to add
 *
 * Takes mac_filter_list_lock spinlock to add new filter to list.
 */
static struct
iecm_mac_filter *__iecm_add_mac_filter(struct iecm_vport *vport,
				       const u8 *macaddr)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_mac_filter *f = NULL;

	spin_lock_bh(&adapter->mac_filter_list_lock);
	f = iecm_find_mac_filter(vport, macaddr);
	if (!f) {
		f = kzalloc(sizeof(*f), GFP_ATOMIC);
		if (!f) {
			dev_err(&adapter->pdev->dev, "Failed to allocate filter: %pM",
				macaddr);
			goto error;
		}

		ether_addr_copy(f->macaddr, macaddr);

		list_add_tail(&f->list, &adapter->config_data.mac_filter_list);
		f->add = true;
	} else {
		f->remove = false;
	}
error:
	spin_unlock_bh(&adapter->mac_filter_list_lock);

	return f;
}

/**
 * iecm_add_mac_filter - Add a mac filter to the filter list
 * @vport: main vport structure
 * @macaddr: the MAC address
 *
 * Returns ptr to the filter or NULL on error. If interface is up, we'll also
 * send the virtchnl message to tell hardware about the filter.
 **/
static struct iecm_mac_filter *iecm_add_mac_filter(struct iecm_vport *vport,
						   const u8 *macaddr)
{
	struct iecm_mac_filter *f;

	if (!macaddr)
		return NULL;

	f = __iecm_add_mac_filter(vport, macaddr);
	if (!f)
		return NULL;

	if (vport->adapter->state == __IECM_UP)
		iecm_add_del_ether_addrs(vport, true, false);

	return f;
}

/**
 * iecm_set_all_filters - Re-add all MAC filters in list
 * @vport: main vport struct
 *
 * Takes mac_filter_list_lock spinlock.  Sets add field to true for filters to
 * resync filters back to HW.
 */
static void iecm_set_all_filters(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_mac_filter *f;

	spin_lock_bh(&adapter->mac_filter_list_lock);
	list_for_each_entry(f, &adapter->config_data.mac_filter_list, list) {
		if (!f->remove)
			f->add = true;
	}
	spin_unlock_bh(&adapter->mac_filter_list_lock);

	iecm_add_del_ether_addrs(vport, true, false);
}

/**
 * iecm_find_vlan - Search filter list for specific vlan filter
 * @vport: vport structure
 * @vlan: vlan tag
 *
 * Returns ptr to the filter object or NULL. Must be called while holding the
 * vlan_list_lock.
 */
static struct
iecm_vlan_filter *iecm_find_vlan(struct iecm_vport *vport,
				 struct iecm_vlan *vlan)
{
	struct iecm_vlan_filter *f;

	list_for_each_entry(f, &vport->adapter->config_data.vlan_filter_list,
			    list) {
		if (vlan->vid == f->vlan.vid && vlan->tpid == f->vlan.tpid)
			return f;
	}
	return NULL;
}

/**
 * iecm_add_vlan - Add a vlan filter to the list
 * @vport: vport structure
 * @vlan: VLAN tag
 *
 * Returns ptr to the filter object or NULL when no memory available.
 */
static struct
iecm_vlan_filter *iecm_add_vlan(struct iecm_vport *vport,
				struct iecm_vlan *vlan)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_vlan_filter *f = NULL;

	spin_lock_bh(&adapter->vlan_list_lock);

	f = iecm_find_vlan(vport, vlan);
	if (!f) {
		f = kzalloc(sizeof(*f), GFP_ATOMIC);
		if (!f)
			goto error;

		f->vlan.vid = vlan->vid;
		f->vlan.tpid = vlan->tpid;

		list_add_tail(&f->list, &adapter->config_data.vlan_filter_list);
		f->add = true;
	}

error:
	spin_unlock_bh(&adapter->vlan_list_lock);
	return f;
}

/**
 * iecm_del_vlan - Remove a vlan filter from the list
 * @vport: vport structure
 * @vlan: VLAN tag
 */
static void iecm_del_vlan(struct iecm_vport *vport, struct iecm_vlan *vlan)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_vlan_filter *f;

	spin_lock_bh(&adapter->vlan_list_lock);

	f = iecm_find_vlan(vport, vlan);
	if (f)
		f->remove = true;

	spin_unlock_bh(&adapter->vlan_list_lock);
}

/**
 * iecm_vlan_rx_add_vid - Add a VLAN filter to the device
 * @netdev: network device struct
 * @proto: unused protocol data
 * @vid: VLAN tag
 *
 * Returns 0 on success
 */
static int iecm_vlan_rx_add_vid(struct net_device *netdev,
				__always_unused __be16 proto, u16 vid)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_vlan vlan;

	vlan = IECM_VLAN(vid, be16_to_cpu(proto));
	if (!iecm_is_feature_ena(vport, NETIF_F_HW_VLAN_CTAG_FILTER))
		return -EINVAL;

	iecm_add_vlan(vport, &vlan);

	if (adapter->state == __IECM_UP)
		adapter->dev_ops.vc_ops.add_del_vlans(vport, true);

	return 0;
}

/**
 * iecm_vlan_rx_kill_vid - Remove a VLAN filter from the device
 * @netdev: network device struct
 * @proto: unused protocol data
 * @vid: VLAN tag
 *
 * Returns 0 on success
 */
static int iecm_vlan_rx_kill_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_vlan_filter *f, *ftmp;
	struct iecm_vlan vlan;

	vlan = IECM_VLAN(vid, be16_to_cpu(proto));
	if (!iecm_is_feature_ena(vport, NETIF_F_HW_VLAN_CTAG_FILTER))
		return -EINVAL;

	if (vport->adapter->state == __IECM_UP) {
		iecm_del_vlan(vport, &vlan);
		adapter->dev_ops.vc_ops.add_del_vlans(vport, false);
	}
	/* It is safe to delete entry from the list now */
	spin_lock_bh(&adapter->vlan_list_lock);
	list_for_each_entry_safe(f, ftmp,
				 &adapter->config_data.vlan_filter_list,
				 list) {
		if (f->vlan.vid == vlan.vid && f->vlan.tpid == vlan.tpid) {
			list_del(&f->list);
			kfree(f);
		}
	}
	spin_unlock_bh(&adapter->vlan_list_lock);

	return 0;
}

/**
 * iecm_set_all_vlans - Re-add all VLANs in list
 * @vport: main vport struct
 *
 * Takes vlan_list_lock spinlock.  Sets add field to true for vlan filters and
 * resyncs vlans back to HW.
 */
static void iecm_set_all_vlans(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_vlan_filter *f;

	spin_lock_bh(&adapter->vlan_list_lock);
	list_for_each_entry(f, &adapter->config_data.vlan_filter_list, list) {
		if (!f->remove)
			f->add = true;
	}
	spin_unlock_bh(&adapter->vlan_list_lock);

	/* Do both add and remove to make sure list is in sync in the case
	 * filters were added and removed before up.
	 */
	adapter->dev_ops.vc_ops.add_del_vlans(vport, false);
	adapter->dev_ops.vc_ops.add_del_vlans(vport, true);
}

/**
 * iecm_init_mac_addr - initialize mac address for vport
 * @vport: main vport structure
 * @netdev: pointer to netdev struct associated with this vport
 */
static int iecm_init_mac_addr(struct iecm_vport *vport,
			      struct net_device *netdev)
{
	struct iecm_adapter *adapter = vport->adapter;

	if (!is_valid_ether_addr(vport->default_mac_addr)) {
		if (!iecm_is_cap_ena(vport->adapter, IECM_OTHER_CAPS,
				     VIRTCHNL2_CAP_MACFILTER)) {
			dev_err(&adapter->pdev->dev,
				"MAC address not provided and capability is not set\n");
			return -EINVAL;
		}

		dev_info(&adapter->pdev->dev, "Invalid MAC address %pM, using random\n",
			 vport->default_mac_addr);
		eth_hw_addr_random(netdev);

		if (!iecm_add_mac_filter(vport, netdev->dev_addr))
			return -ENOMEM;

		ether_addr_copy(vport->default_mac_addr, netdev->dev_addr);
	} else {
		dev_addr_mod(netdev, 0, vport->default_mac_addr, ETH_ALEN);
		ether_addr_copy(netdev->perm_addr, vport->default_mac_addr);
	}

	return 0;
}

/**
 * iecm_cfg_netdev - Allocate, configure and register a netdev
 * @vport: main vport structure
 *
 * Returns 0 on success, negative value on failure.
 */
static int iecm_cfg_netdev(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	netdev_features_t dflt_features;
	netdev_features_t offloads = 0;
	struct iecm_netdev_priv *np;
	struct net_device *netdev;
	u16 max_q;
	int err;

	lockdep_assert_held(&adapter->sw_mutex);

	/* It's possible we already have a netdev allocated and registered for
	 * this vport
	 */
	if (adapter->netdevs[vport->idx]) {
		netdev = adapter->netdevs[vport->idx];
		np = netdev_priv(netdev);
		np->vport = vport;
		vport->netdev = netdev;

		return iecm_init_mac_addr(vport, netdev);
	}

	max_q = adapter->max_queue_limit;

	netdev = alloc_etherdev_mqs(sizeof(struct iecm_netdev_priv),
				    max_q, max_q);
	if (!netdev)
		return -ENOMEM;
	vport->netdev = netdev;
	np = netdev_priv(netdev);
	np->vport = vport;

	err = iecm_init_mac_addr(vport, netdev);
	if (err)
		goto err;

	/* assign netdev_ops */
	if (iecm_is_queue_model_split(vport->txq_model))
		netdev->netdev_ops = &iecm_netdev_ops_splitq;
	else
		netdev->netdev_ops = &iecm_netdev_ops_singleq;

	/* setup watchdog timeout value to be 5 second */
	netdev->watchdog_timeo = 5 * HZ;

	/* configure default MTU size */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = vport->max_mtu;

	dflt_features = NETIF_F_SG	|
			NETIF_F_HIGHDMA;

	if (iecm_is_cap_ena_all(adapter, IECM_RSS_CAPS, IECM_CAP_RSS))
		dflt_features |= NETIF_F_RXHASH;
	if (iecm_is_cap_ena_all(adapter, IECM_CSUM_CAPS, IECM_CAP_RX_CSUM_L4V4))
		dflt_features |= NETIF_F_IP_CSUM;
	if (iecm_is_cap_ena_all(adapter, IECM_CSUM_CAPS, IECM_CAP_RX_CSUM_L4V6))
		dflt_features |= NETIF_F_IPV6_CSUM;
	if (iecm_is_cap_ena(adapter, IECM_CSUM_CAPS, IECM_CAP_RX_CSUM))
		dflt_features |= NETIF_F_RXCSUM;
	if (iecm_is_cap_ena_all(adapter, IECM_CSUM_CAPS, IECM_CAP_SCTP_CSUM))
		dflt_features |= NETIF_F_SCTP_CRC;

	if (iecm_is_vlan_cap_ena(adapter, IECM_CAP_VLAN_CTAG_INSERT))
		dflt_features |= IECM_F_HW_VLAN_CTAG_TX;
	if (iecm_is_vlan_cap_ena(adapter, IECM_CAP_VLAN_CTAG_STRIP))
		dflt_features |= IECM_F_HW_VLAN_CTAG_RX;
	if (iecm_is_vlan_cap_ena(adapter, IECM_CAP_VLAN_CTAG_ADD_DEL))
		dflt_features |= IECM_F_HW_VLAN_CTAG_FILTER;

	if (iecm_is_vlan_cap_ena(adapter, IECM_CAP_VLAN_STAG_INSERT))
		dflt_features |= NETIF_F_HW_VLAN_STAG_TX;
	if (iecm_is_vlan_cap_ena(adapter, IECM_CAP_VLAN_STAG_STRIP))
		dflt_features |= NETIF_F_HW_VLAN_STAG_RX;
	if (iecm_is_vlan_cap_ena(adapter, IECM_CAP_VLAN_STAG_ADD_DEL))
		dflt_features |= NETIF_F_HW_VLAN_STAG_FILTER;
	/* Enable cloud filter if ADQ is supported */
	if (iecm_is_cap_ena(adapter, IECM_BASE_CAPS, VIRTCHNL2_CAP_ADQ) ||
	    iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_ADQ))
		dflt_features |= NETIF_F_HW_TC;
	if (iecm_is_cap_ena(adapter, IECM_SEG_CAPS, VIRTCHNL2_CAP_SEG_IPV4_TCP))
		dflt_features |= NETIF_F_TSO;
	if (iecm_is_cap_ena(adapter, IECM_SEG_CAPS, VIRTCHNL2_CAP_SEG_IPV6_TCP))
		dflt_features |= NETIF_F_TSO6;
	if (iecm_is_cap_ena_all(adapter, IECM_SEG_CAPS,
				VIRTCHNL2_CAP_SEG_IPV4_UDP |
				VIRTCHNL2_CAP_SEG_IPV6_UDP))
		dflt_features |= NETIF_F_GSO_UDP_L4;
	if (iecm_is_cap_ena_all(adapter, IECM_RSC_CAPS, IECM_CAP_RSC))
		offloads |= NETIF_F_GRO_HW;
	netdev->features |= dflt_features;
	netdev->hw_features |= dflt_features | offloads;
	netdev->hw_enc_features |= dflt_features | offloads;

	SET_NETDEV_DEV(netdev, &adapter->pdev->dev);

	/* carrier off on init to avoid Tx hangs */
	netif_carrier_off(netdev);

	/* make sure transmit queues start off as stopped */
	netif_tx_stop_all_queues(netdev);

	/* register last */
	err = register_netdev(netdev);
	if (err)
		goto err;

	/* The vport can be arbitrarily released so we need to also track
	 * netdevs in the adapter struct
	 */
	adapter->netdevs[vport->idx] = netdev;

	return 0;
err:
	free_netdev(vport->netdev);
	vport->netdev = NULL;

	return err;
}

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
 * iecm_remove_vlan_filters - Remove all vlan filters
 * @vport: vport structure
 */
static void iecm_remove_vlan_filters(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_user_config_data *config_data;

	config_data = &adapter->config_data;
	if (!list_empty(&config_data->vlan_filter_list)) {
		struct iecm_vlan_filter *f;

		spin_lock_bh(&adapter->vlan_list_lock);
		list_for_each_entry(f, &config_data->vlan_filter_list, list)
			f->remove = true;
		spin_unlock_bh(&adapter->vlan_list_lock);
		adapter->dev_ops.vc_ops.add_del_vlans(vport, false);
	}
}

/**
 * iecm_vport_stop - Disable a vport
 * @vport: vport to disable
 */
static void iecm_vport_stop(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;

	mutex_lock(&vport->stop_mutex);
	if (adapter->state <= __IECM_DOWN)
		goto stop_unlock;

	netif_tx_stop_all_queues(vport->netdev);
	netif_carrier_off(vport->netdev);
	netif_tx_disable(vport->netdev);

	if (adapter->dev_ops.vc_ops.disable_vport)
		adapter->dev_ops.vc_ops.disable_vport(vport);
	adapter->dev_ops.vc_ops.disable_queues(vport);
	adapter->dev_ops.vc_ops.irq_map_unmap(vport, false);
	/* Normally we ask for queues in create_vport, but if we're changing
	 * number of requested queues we do a delete then add instead of
	 * deleting and reallocating the vport.
	 */
	if (test_and_clear_bit(__IECM_DEL_QUEUES,
			       vport->adapter->flags))
		iecm_send_delete_queues_msg(vport);
	if (!test_bit(__IECM_REL_RES_IN_PROG, adapter->flags))
		iecm_remove_vlan_filters(vport);

	adapter->link_up = false;
	iecm_vport_intr_deinit(vport);
	iecm_vport_intr_rel(vport);
	iecm_vport_queues_rel(vport);
	adapter->state = __IECM_DOWN;

stop_unlock:
	mutex_unlock(&vport->stop_mutex);
}

/**
 * iecm_stop - Disables a network interface
 * @netdev: network interface device structure
 *
 * The stop entry point is called when an interface is de-activated by the OS,
 * and the netdevice enters the DOWN state.  The hardware is still under the
 * driver's control, but the netdev interface is disabled.
 *
 * Returns success only - not allowed to fail
 */
static int iecm_stop(struct net_device *netdev)
{
	struct iecm_netdev_priv *np = netdev_priv(netdev);

	iecm_vport_stop(np->vport);

	return 0;
}

/**
 * iecm_decfg_netdev - Unregister the netdev
 * @vport: vport for which netdev to be unregistred
 */
static void iecm_decfg_netdev(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;

	if (!vport->netdev)
		return;

	unregister_netdev(vport->netdev);
	free_netdev(vport->netdev);
	vport->netdev = NULL;

	adapter->netdevs[vport->idx] = NULL;
}

/**
 * iecm_vport_rel - Delete a vport and free its resources
 * @vport: the vport being removed
 */
static void iecm_vport_rel(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;

	iecm_deinit_rss(vport);
	if (adapter->dev_ops.vc_ops.destroy_vport)
		adapter->dev_ops.vc_ops.destroy_vport(vport);
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

		iecm_vport_stop(adapter->vports[i]);
		if (!test_bit(__IECM_HR_RESET_IN_PROG, adapter->flags))
			iecm_decfg_netdev(adapter->vports[i]);
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
	adapter->dev_ops.vc_ops.vport_init(vport, vport_id);

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
	struct iecm_adapter *adapter = container_of(work,
						    struct iecm_adapter,
						    serv_task.work);

	if (test_bit(__IECM_MB_INTR_MODE, adapter->flags)) {
		if (test_and_clear_bit(__IECM_MB_INTR_TRIGGER,
				       adapter->flags)) {
			iecm_recv_mb_msg(adapter, VIRTCHNL_OP_UNKNOWN, NULL, 0);
			iecm_mb_irq_enable(adapter);
		}
	} else {
		iecm_recv_mb_msg(adapter, VIRTCHNL_OP_UNKNOWN, NULL, 0);
	}

	if (iecm_is_reset_detected(adapter) &&
	    !iecm_is_reset_in_prog(adapter)) {
		dev_info(&adapter->pdev->dev, "HW reset detected\n");
		set_bit(__IECM_HR_FUNC_RESET, adapter->flags);
		queue_delayed_work(adapter->vc_event_wq,
				   &adapter->vc_event_task,
				   msecs_to_jiffies(10));
	}

	queue_delayed_work(adapter->serv_wq, &adapter->serv_task,
			   msecs_to_jiffies(300));
}

/**
 * iecm_restore_vlans - Restore vlan filters/vlan stripping/insert config
 * @vport: virtual port structure
 */
static void iecm_restore_vlans(struct iecm_vport *vport)
{
	if (iecm_is_feature_ena(vport, NETIF_F_HW_VLAN_CTAG_FILTER))
		iecm_set_all_vlans(vport);
}

/**
 * iecm_restore_features - Restore feature configs
 * @vport: virtual port structure
 */
static void iecm_restore_features(struct iecm_vport *vport)
{
	struct iecm_adapter *adapter = vport->adapter;

	if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_MACFILTER))
		iecm_set_all_filters(vport);

	if (iecm_is_cap_ena(adapter, IECM_BASE_CAPS, VIRTCHNL2_CAP_VLAN) ||
	    iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_VLAN))
		iecm_restore_vlans(vport);

	if ((iecm_is_user_flag_ena(adapter, __IECM_PROMISC_UC) ||
	     iecm_is_user_flag_ena(adapter, __IECM_PROMISC_MC)) &&
	    test_and_clear_bit(__IECM_VPORT_INIT_PROMISC, vport->flags)) {
		if (iecm_set_promiscuous(adapter))
			dev_info(&adapter->pdev->dev, "Failed to restore promiscuous settings\n");
	}
}

/**
 * iecm_set_real_num_queues - set number of queues for netdev
 * @vport: virtual port structure
 *
 * Returns 0 on success, negative on failure.
 */
static int iecm_set_real_num_queues(struct iecm_vport *vport)
{
	int err;

	/* If we're in normal up path, the stack already takes the rtnl_lock
	 * for us, however, if we're doing up as a part of a hard reset, we'll
	 * need to take the lock ourself before touching the netdev.
	 */
	if (test_bit(__IECM_HR_RESET_IN_PROG, vport->adapter->flags))
		rtnl_lock();
	err = netif_set_real_num_rx_queues(vport->netdev, vport->num_rxq);
	if (err)
		goto error;
	err = netif_set_real_num_tx_queues(vport->netdev, vport->num_txq);
error:
	if (test_bit(__IECM_HR_RESET_IN_PROG, vport->adapter->flags))
		rtnl_unlock();
	return err;
}

/**
 * iecm_up_complete - Complete interface up sequence
 * @vport: virtual port strucutre
 *
 * Returns 0 on success, negative on failure.
 */
static int iecm_up_complete(struct iecm_vport *vport)
{
	int err;

	err = iecm_set_real_num_queues(vport);
	if (err)
		return err;

	if (vport->adapter->link_up && !netif_carrier_ok(vport->netdev)) {
		netif_carrier_on(vport->netdev);
		netif_tx_start_all_queues(vport->netdev);
	}

	vport->adapter->state = __IECM_UP;
	return 0;
}

/**
 * iecm_rx_init_buf_tail - Write initial buffer ring tail value
 * @vport: virtual port struct
 */
static void iecm_rx_init_buf_tail(struct iecm_vport *vport)
{
	int i, j;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct iecm_rxq_group *grp = &vport->rxq_grps[i];

		if (iecm_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				struct iecm_queue *q =
					&grp->splitq.bufq_sets[j].bufq;

				writel(q->next_to_alloc, q->tail);
			}
		} else {
			for (j = 0; j < grp->singleq.num_rxq; j++) {
				struct iecm_queue *q =
					grp->singleq.rxqs[j];

				writel(q->next_to_alloc, q->tail);
			}
		}
	}
}

/* iecm_set_vlan_offload_features - set vlan offload features
 * @netdev: netdev structure
 * @prev_features: previously set features
 * @features: current features received from user
 *
 * Returns 0 on success, error value on failure
 */
static int
iecm_set_vlan_offload_features(struct net_device *netdev,
			       netdev_features_t prev_features,
			       netdev_features_t features)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);
	bool stripping_ena = true, insertion_ena = true;
	struct iecm_virtchnl_ops *vc_ops;
	u16 vlan_ethertype = 0;

	vc_ops = &vport->adapter->dev_ops.vc_ops;
	/* keep cases separate because one ethertype for offloads can be
	 * disabled at the same time as another is disabled, so check for an
	 * enabled ethertype first, then check for disabled. Default to
	 * ETH_P_8021Q so an ethertype is specified if disabling insertion
	 * and stripping.
	 */
	if (features & (NETIF_F_HW_VLAN_STAG_RX | NETIF_F_HW_VLAN_STAG_TX))
		vlan_ethertype = ETH_P_8021AD;
	else if (features & (NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_TX))
		vlan_ethertype = ETH_P_8021Q;
	else if (prev_features & (NETIF_F_HW_VLAN_STAG_RX |
				  NETIF_F_HW_VLAN_STAG_TX))
		vlan_ethertype = ETH_P_8021AD;
	else if (prev_features & (NETIF_F_HW_VLAN_CTAG_RX |
				  NETIF_F_HW_VLAN_CTAG_TX))
		vlan_ethertype = ETH_P_8021Q;
	else
		vlan_ethertype = ETH_P_8021Q;

	if (!(features & (NETIF_F_HW_VLAN_STAG_RX | NETIF_F_HW_VLAN_CTAG_RX)))
		stripping_ena = false;
	if (!(features & (NETIF_F_HW_VLAN_STAG_TX | NETIF_F_HW_VLAN_CTAG_TX)))
		insertion_ena = false;

	vport->adapter->config_data.vlan_ethertype = vlan_ethertype;

	vc_ops->strip_vlan_msg(vport, stripping_ena);
	if (vc_ops->insert_vlan_msg)
		vc_ops->insert_vlan_msg(vport, insertion_ena);

	return 0;
}

/**
 * iecm_vport_open - Bring up a vport
 * @vport: vport to bring up
 * @alloc_res: allocate queue resources
 */
static int iecm_vport_open(struct iecm_vport *vport, bool alloc_res)
{
	struct iecm_adapter *adapter = vport->adapter;
	int err;

	if (vport->adapter->state != __IECM_DOWN)
		return -EBUSY;

	/* we do not allow interface up just yet */
	netif_carrier_off(vport->netdev);

	if (alloc_res) {
		err = iecm_vport_queues_alloc(vport);
		if (err)
			return err;
	}

	err = iecm_vport_intr_alloc(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Call to interrupt alloc returned %d\n",
			err);
		goto unroll_queues_alloc;
	}

	err = adapter->dev_ops.vc_ops.vport_queue_ids_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Call to queue ids init returned %d\n",
			err);
		goto unroll_intr_alloc;
	}

	err = adapter->dev_ops.vc_ops.vportq_reg_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Call to queue reg init returned %d\n",
			err);
		goto unroll_intr_alloc;
	}
	iecm_rx_init_buf_tail(vport);

	err = iecm_vport_intr_init(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Call to vport interrupt init returned %d\n",
			err);
		goto unroll_intr_alloc;
	}
	err = adapter->dev_ops.vc_ops.config_queues(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to config queues\n");
		goto unroll_config_queues;
	}
	err = adapter->dev_ops.vc_ops.irq_map_unmap(vport, true);
	if (err) {
		dev_err(&adapter->pdev->dev, "Call to irq_map_unmap returned %d\n",
			err);
		goto unroll_config_queues;
	}
	err = adapter->dev_ops.vc_ops.enable_queues(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to enable queues\n");
		goto unroll_enable_queues;
	}

	if (adapter->dev_ops.vc_ops.enable_vport) {
		err = adapter->dev_ops.vc_ops.enable_vport(vport);
		if (err) {
			dev_err(&adapter->pdev->dev, "Failed to enable vport\n");
			err = -EAGAIN;
			goto unroll_vport_enable;
		}
	}

	iecm_restore_features(vport);

	if (adapter->rss_data.rss_lut)
		err = iecm_config_rss(vport);
	else
		err = iecm_init_rss(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to init RSS\n");
		goto unroll_init_rss;
	}
	err = iecm_up_complete(vport);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to complete up\n");
		goto unroll_up_comp;
	}

	return 0;

unroll_up_comp:
	iecm_deinit_rss(vport);
unroll_init_rss:
	adapter->dev_ops.vc_ops.disable_vport(vport);
unroll_vport_enable:
	adapter->dev_ops.vc_ops.disable_queues(vport);
unroll_enable_queues:
	adapter->dev_ops.vc_ops.irq_map_unmap(vport, false);
unroll_config_queues:
	iecm_vport_intr_deinit(vport);
unroll_intr_alloc:
	iecm_vport_intr_rel(vport);
unroll_queues_alloc:
	if (alloc_res)
		iecm_vport_queues_rel(vport);

	return err;
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
	int index;

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
	/* Start the service task before requesting vectors. This will ensure
	 * vector information response from mailbox is handled
	 */
	queue_delayed_work(adapter->serv_wq, &adapter->serv_task,
			   msecs_to_jiffies(5 * (pdev->devfn & 0x07)));
	err = iecm_intr_req(adapter);
	if (err) {
		dev_err(&pdev->dev, "failed to enable interrupt vectors: %d\n",
			err);
		goto intr_req_err;
	}
	err = iecm_send_vlan_v2_caps_msg(adapter);
	if (err)
		goto vlan_v2_caps_failed;

	err = adapter->dev_ops.vc_ops.get_supported_desc_ids(vport);
	if (err) {
		dev_err(&pdev->dev, "failed to get required descriptor ids\n");
		goto rxdids_failed;
	}

	if (iecm_cfg_netdev(vport))
		goto cfg_netdev_err;

	if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_VLAN) ||
	    iecm_is_cap_ena(adapter, IECM_BASE_CAPS, VIRTCHNL2_CAP_VLAN)) {
		err = iecm_set_vlan_offload_features(vport->netdev, 0,
						     vport->netdev->features);
		if (err)
			goto cfg_netdev_err;
	}

	err = adapter->dev_ops.vc_ops.get_ptype(vport);
	if (err)
		goto cfg_netdev_err;
	queue_delayed_work(adapter->stats_wq, &adapter->stats_task,
			   msecs_to_jiffies(10 * (pdev->devfn & 0x07)));
	set_bit(__IECM_VPORT_INIT_PROMISC, vport->flags);
	/* Once state is put into DOWN, driver is ready for dev_open */
	adapter->state = __IECM_DOWN;
	if (test_and_clear_bit(__IECM_UP_REQUESTED, adapter->flags))
		iecm_vport_open(vport, true);

	/* Clear the reset flag unconditionally here in case we were in reset
	 * and the link was down
	 */
	clear_bit(__IECM_HR_RESET_IN_PROG, vport->adapter->flags);

	return;

vlan_v2_caps_failed:
rxdids_failed:
cfg_netdev_err:
	iecm_intr_rel(adapter);
intr_req_err:
	index = iecm_get_vport_index(adapter, vport);
	if (index >= 0)
		adapter->vports[index] = NULL;
	iecm_vport_rel(vport);
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
	int i;

	set_bit(__IECM_REL_RES_IN_PROG, adapter->flags);
	/* Wait until the init_task is done else this thread might release
	 * the resources first and the other thread might end up in a bad state
	 */
	cancel_delayed_work_sync(&adapter->init_task);
	iecm_vport_rel_all(adapter);

	/* Set all bits as we dont know on which vc_state the vhnl_wq is
	 * waiting on and wakeup the virtchnl workqueue even if it is waiting
	 * for the response as we are going down
	 */
	for (i = 0; i < IECM_VC_NBITS; i++)
		set_bit(i, adapter->vc_state);
	wake_up(&adapter->vchnl_wq);

	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->stats_task);
	iecm_intr_rel(adapter);
	/* Clear all the bits */
	for (i = 0; i < IECM_VC_NBITS; i++)
		clear_bit(i, adapter->vc_state);
	clear_bit(__IECM_REL_RES_IN_PROG, adapter->flags);
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
 * iecm_initiate_soft_reset - Initiate a software reset
 * @vport: virtual port data struct
 * @reset_cause: reason for the soft reset
 *
 * Soft reset only reallocs vport queue resources. Returns 0 on success,
 * negative on failure.
 */
int iecm_initiate_soft_reset(struct iecm_vport *vport,
			     enum iecm_flags reset_cause)
{
	enum iecm_state current_state = vport->adapter->state;
	struct iecm_adapter *adapter = vport->adapter;
	struct iecm_vport *new_vport;
	int err = 0, i;

	/* make sure we do not end up in initiating multiple resets */
	mutex_lock(&adapter->reset_lock);

	/* If the system is low on memory, we can end up in bad state if we
	 * free all the memory for queue resources and try to allocate them
	 * again. Instead, we can pre-allocate the new resources before doing
	 * anything and bailing if the alloc fails.
	 *
	 * Make a clone of the existing vport to mimic its current configuration,
	 * then modify the new structure with any requested changes. Once the
	 * allocation of the new resources is done, stop the existing vport and
	 * copy the configuration to the main vport. If an error occurred, the
	 * existing vport will be untouched.
	 *
	 */
	new_vport = kzalloc(sizeof(*vport), GFP_KERNEL);
	if (!new_vport) {
		mutex_unlock(&adapter->reset_lock);
		return -ENOMEM;
	}
	memcpy(new_vport, vport, sizeof(*vport));

	/* Adjust resource parameters prior to reallocating resources */
	switch (reset_cause) {
	case __IECM_SR_Q_CHANGE:
		adapter->dev_ops.vc_ops.adjust_qs(new_vport);
		break;
	case __IECM_SR_Q_DESC_CHANGE:
		/* Update queue parameters before allocating resources */
		iecm_vport_calc_num_q_desc(new_vport);
		break;
	case __IECM_SR_Q_SCH_CHANGE:
	case __IECM_SR_MTU_CHANGE:
	case __IECM_SR_RSC_CHANGE:
	case __IECM_SR_HSPLIT_CHANGE:
		break;
	default:
		dev_err(&adapter->pdev->dev, "Unhandled soft reset cause\n");
		err = -EINVAL;
		goto err_default;
	}

	err = iecm_vport_queues_alloc(new_vport);
	if (err)
		goto err_default;

	if (adapter->virt_ver_maj == VIRTCHNL_VERSION_MAJOR_2) {
		if (current_state <= __IECM_DOWN) {
			adapter->dev_ops.vc_ops.delete_queues(vport);
		} else {
			set_bit(__IECM_DEL_QUEUES, adapter->flags);
			iecm_vport_stop(vport);
		}

		iecm_deinit_rss(vport);
		err = adapter->dev_ops.vc_ops.add_queues(new_vport, new_vport->num_txq,
							 new_vport->num_complq,
							 new_vport->num_rxq,
							 new_vport->num_bufq);
		if (err)
			goto err_reset;
	} else {
		iecm_vport_stop(vport);
	}

	memcpy(vport, new_vport, sizeof(*vport));
	/* Since iecm_vport_queues_alloc was called with new_port, the queue
	 * back pointers are currently pointing to the local new_vport. Reset
	 * the backpointers to the original vport here
	 */
	for (i = 0; i < vport->num_txq_grp; i++) {
		struct iecm_txq_group *tx_qgrp = &vport->txq_grps[i];
		int j;

		tx_qgrp->vport = vport;
		for (j = 0; j < tx_qgrp->num_txq; j++)
			tx_qgrp->txqs[j]->vport = vport;

		if (iecm_is_queue_model_split(vport->txq_model))
			tx_qgrp->complq->vport = vport;
	}

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct iecm_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		struct iecm_queue *q;
		int j, num_rxq;

		rx_qgrp->vport = vport;
		for (j = 0; j < vport->num_bufqs_per_qgrp; j++)
			rx_qgrp->splitq.bufq_sets[j].bufq.vport = vport;

		if (iecm_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			if (iecm_is_queue_model_split(vport->rxq_model))
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];
			q->vport = vport;
		}
	}

	/* Post resource allocation reset */
	if (reset_cause == __IECM_SR_Q_CHANGE) {
		iecm_intr_rel(adapter);
		iecm_intr_req(adapter);
	}

	kfree(new_vport);

	if (current_state == __IECM_UP)
		err = iecm_vport_open(vport, false);
	mutex_unlock(&adapter->reset_lock);
	return err;
err_reset:
	iecm_vport_queues_rel(vport);
err_default:
	kfree(new_vport);
	mutex_unlock(&adapter->reset_lock);
	return err;
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

/**
 * iecm_addr_sync - Callback for dev_(mc|uc)_sync to add address
 * @netdev: the netdevice
 * @addr: address to add
 *
 * Called by __dev_(mc|uc)_sync when an address needs to be added. We call
 * __dev_(uc|mc)_sync from .set_rx_mode. Kernel takes addr_list_lock spinlock
 * meaning we cannot sleep in this context. Due to this, we have to add the
 * filter and send the virtchnl message asynchronously without waiting for the
 * response from the other side. We won't know whether or not the operation
 * actually succeeded until we get the message back.  Returns 0 on success,
 * negative on failure.
 */
static int iecm_addr_sync(struct net_device *netdev, const u8 *addr)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);

	if (__iecm_add_mac_filter(vport, addr)) {
		if (vport->adapter->state == __IECM_UP) {
			set_bit(__IECM_ADD_ETH_REQ, vport->adapter->flags);
			iecm_add_del_ether_addrs(vport, true, true);
		}
		return 0;
	}

	return -ENOMEM;
}

/**
 * iecm_addr_unsync - Callback for dev_(mc|uc)_sync to remove address
 * @netdev: the netdevice
 * @addr: address to add
 *
 * Called by __dev_(mc|uc)_sync when an address needs to be added. We call
 * __dev_(uc|mc)_sync from .set_rx_mode. Kernel takes addr_list_lock spinlock
 * meaning we cannot sleep in this context. Due to this we have to delete the
 * filter and send the virtchnl message asychronously without waiting for the
 * return from the other side.  We won't know whether or not the operation
 * actually succeeded until we get the message back. Returns 0 on success,
 * negative on failure.
 */
static int iecm_addr_unsync(struct net_device *netdev, const u8 *addr)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);

	/* Under some circumstances, we might receive a request to delete
	 * our own device address from our uc list. Because we store the
	 * device address in the VSI's MAC/VLAN filter list, we need to ignore
	 * such requests and not delete our device address from this list.
	 */
	if (ether_addr_equal(addr, netdev->dev_addr))
		return 0;

	if (__iecm_del_mac_filter(vport, addr)) {
		if (vport->adapter->state == __IECM_UP) {
			set_bit(__IECM_DEL_ETH_REQ, vport->adapter->flags);
			iecm_add_del_ether_addrs(vport, false, true);
		}
	}

	return 0;
}

/**
 * iecm_set_rx_mode - NDO callback to set the netdev filters
 * @netdev: network interface device structure
 *
 * Stack takes addr_list_lock spinlock before calling our .set_rx_mode.  We
 * cannot sleep in this context.
 */
static void iecm_set_rx_mode(struct net_device *netdev)
{
	struct iecm_adapter *adapter = iecm_netdev_to_adapter(netdev);

	if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_MACFILTER)) {
		__dev_uc_sync(netdev, iecm_addr_sync, iecm_addr_unsync);
		__dev_mc_sync(netdev, iecm_addr_sync, iecm_addr_unsync);
	}

	if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_PROMISC)) {
		bool changed = false;

		/* IFF_PROMISC enables both unicast and multicast promiscuous,
		 * while IFF_ALLMULTI only enables multicast such that:
		 *
		 * promisc  + allmulti		= unicast | multicast
		 * promisc  + !allmulti		= unicast | multicast
		 * !promisc + allmulti		= multicast
		 */
		if ((netdev->flags & IFF_PROMISC) &&
		    !test_and_set_bit(__IECM_PROMISC_UC,
				      adapter->config_data.user_flags)) {
			changed = true;
			dev_info(&adapter->pdev->dev, "Entering promiscuous mode\n");
			if (!test_and_set_bit(__IECM_PROMISC_MC,
					      adapter->flags))
				dev_info(&adapter->pdev->dev, "Entering multicast promiscuous mode\n");
		}
		if (!(netdev->flags & IFF_PROMISC) &&
		    test_and_clear_bit(__IECM_PROMISC_UC,
				       adapter->config_data.user_flags)) {
			changed = true;
			dev_info(&adapter->pdev->dev, "Leaving promiscuous mode\n");
		}
		if (netdev->flags & IFF_ALLMULTI &&
		    !test_and_set_bit(__IECM_PROMISC_MC,
				      adapter->config_data.user_flags)) {
			changed = true;
			dev_info(&adapter->pdev->dev, "Entering multicast promiscuous mode\n");
		}
		if (!(netdev->flags & (IFF_ALLMULTI | IFF_PROMISC)) &&
		    test_and_clear_bit(__IECM_PROMISC_MC,
				       adapter->config_data.user_flags)) {
			changed = true;
			dev_info(&adapter->pdev->dev, "Leaving multicast promiscuous mode\n");
		}

		if (changed) {
			int err = iecm_set_promiscuous(adapter);

			if (err) {
				dev_info(&adapter->pdev->dev, "Failed to set promiscuous mode: %d\n",
					 err);
			}
		}
	}
}

/**
 * iecm_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 */
static int iecm_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);
	struct iecm_adapter *adapter = vport->adapter;
	int err = 0;

	if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_VLAN) ||
	    iecm_is_cap_ena(adapter, IECM_BASE_CAPS, VIRTCHNL2_CAP_VLAN)) {
		err = iecm_set_vlan_offload_features(netdev, netdev->features,
						     features);
		if (err)
			return err;
	}

	if ((netdev->features ^ features) & NETIF_F_GRO_HW) {
		netdev->features ^= NETIF_F_GRO_HW;
		err = iecm_initiate_soft_reset(vport, __IECM_SR_RSC_CHANGE);
	}

	return err;
}

/**
 * iecm_fix_features - fix up the netdev feature bits
 * @netdev: our net device
 * @features: desired feature bits
 *
 * Returns fixed-up features bits
 */
static netdev_features_t iecm_fix_features(struct net_device *netdev,
					   netdev_features_t features)
{
	return features;
}

/**
 * iecm_open - Called when a network interface becomes active
 * @netdev: network interface device structure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the netdev watchdog is enabled,
 * and the stack is notified that the interface is ready.
 *
 * Returns 0 on success, negative value on failure
 */
static int iecm_open(struct net_device *netdev)
{
	struct iecm_netdev_priv *np = netdev_priv(netdev);

	return iecm_vport_open(np->vport, true);
}

/**
 * iecm_change_mtu - NDO callback to change the MTU
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 */
static int iecm_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct iecm_vport *vport =  iecm_netdev_to_vport(netdev);

	netdev->mtu = new_mtu;

	return iecm_initiate_soft_reset(vport, __IECM_SR_MTU_CHANGE);
}

static int iecm_offload_txtime(struct iecm_vport *vport,
			       struct tc_etf_qopt_offload *qopt)
{
	return -EOPNOTSUPP;
}

/**
 * iecm_validate_tx_bandwidth - validate the max Tx bandwidth
 * @vport: vport structure
 * @max_tx_rate: max Tx bandwidth for a tc
 **/
static int iecm_validate_tx_bandwidth(struct iecm_vport *vport,
				      u64 max_tx_rate)
{
	struct iecm_adapter *adapter = vport->adapter;
	int speed = 0, ret = 0;

	if (adapter->link_speed_mbps) {
		if (adapter->link_speed_mbps < U32_MAX) {
			speed = adapter->link_speed_mbps;
			goto validate_bw;
		} else {
			dev_err(&adapter->pdev->dev, "Unknown link speed\n");
			return -EINVAL;
		}
	}

	switch (adapter->link_speed) {
	case VIRTCHNL_LINK_SPEED_40GB:
		speed = SPEED_40000;
		break;
	case VIRTCHNL_LINK_SPEED_25GB:
		speed = SPEED_25000;
		break;
	case VIRTCHNL_LINK_SPEED_20GB:
		speed = SPEED_20000;
		break;
	case VIRTCHNL_LINK_SPEED_10GB:
		speed = SPEED_10000;
		break;
	case VIRTCHNL_LINK_SPEED_5GB:
		speed = SPEED_5000;
		break;
	case VIRTCHNL_LINK_SPEED_2_5GB:
		speed = SPEED_2500;
		break;
	case VIRTCHNL_LINK_SPEED_1GB:
		speed = SPEED_1000;
		break;
	case VIRTCHNL_LINK_SPEED_100MB:
		speed = SPEED_100;
		break;
	default:
		break;
	}

validate_bw:
	if (max_tx_rate > speed) {
		dev_err(&adapter->pdev->dev, "Invalid tx rate specified\n");
		ret = -EINVAL;
	}

	return ret;
}

/**
 * iecm_validate_ch_config - validate queue mapping info
 * @vport: vport structure
 * @mqprio_qopt: queue parameters
 * @max_tc_allowed: MAX TC allowed, it could be 4 or 16 depends.
 *
 * This function validates if the configuration provided by the user to
 * configure queue channels is valid or not.
 *
 * Returns 0 on a valid config and negative on invalid config.
 **/
static int iecm_validate_ch_config(struct iecm_vport *vport,
				   struct tc_mqprio_qopt_offload *mqprio_qopt,
				   u8 max_tc_allowed)
{
	struct iecm_adapter *adapter = vport->adapter;
	u32 tc, qcount, non_power_2_qcount = 0;
	u64 total_max_rate = 0;
	int i, num_qs = 0;

	if (mqprio_qopt->qopt.num_tc > max_tc_allowed ||
	    mqprio_qopt->qopt.num_tc < 1)
		return -EINVAL;

	/* For ADQ there are few rules on queue allocation for each TC
	 *     1. Number of queues for TC0 should always be a power of 2
	 *     2. Number of queues for rest of TCs can be non-power of 2
	 *     3. If the previous TC has non-power of 2 queues, then all the
	 *        following TCs should be either
	 *        a. same number of queues as that of the previous non-power
	 *           of 2 or
	 *        b. less than previous non-power of 2 and power of 2
	 *        ex: 1@0 2@1 3@3 4@6 - Invalid
	 *            1@0 2@1 3@3 3@6 - Valid
	 *            1@0 2@1 3@3 2@6 - Valid
	 *            1@0 2@1 3@3 1@6 - Valid
	 */
	for (tc = 0; tc < mqprio_qopt->qopt.num_tc; tc++) {
		qcount = mqprio_qopt->qopt.count[tc];

		/* case 1. check for first TC to be always power of 2 in ADQ */
		if (!tc && !is_power_of_2(qcount)) {
			dev_err(&adapter->pdev->dev,
				"TC0:qcount[%d] must be a power of 2\n",
				qcount);
			return -EINVAL;
		}
		/* case 2 & 3, check for non-power of 2 number of queues */
		if (tc && non_power_2_qcount) {
			if (qcount > non_power_2_qcount) {
				dev_err(&adapter->pdev->dev,
					"TC%d has %d qcount cannot be > non_power_of_2 qcount [%d]\n",
					tc, qcount, non_power_2_qcount);
				return -EINVAL;
			} else if (qcount < non_power_2_qcount) {
				/* it must be power of 2, otherwise fail */
				if (!is_power_of_2(qcount)) {
					dev_err(&adapter->pdev->dev,
						"TC%d has %d qcount must be a power of 2 < non_power_of_2 qcount [%d]\n",
						tc, qcount, non_power_2_qcount);
					return -EINVAL;
				}
			}
		} else if (tc && !is_power_of_2(qcount)) {
			/* this is the first TC to have a non-power of 2 queue
			 * count and the code is going to enter this section
			 * only once. The qcount for this TC will serve as
			 * our reference/guide to allocate number of queues
			 * for all the further TCs as per section a. and b. in
			 * case 3 mentioned above.
			 */
			non_power_2_qcount = qcount;
			dev_dbg(&adapter->pdev->dev,
				"TC%d:count[%d] non power of 2\n", tc,
				qcount);
			}
	}

	for (i = 0; i <= mqprio_qopt->qopt.num_tc - 1; i++) {
		u64 tx_rate = 0;

		if (!mqprio_qopt->qopt.count[i] ||
		    mqprio_qopt->qopt.offset[i] != num_qs)
			return -EINVAL;
		if (mqprio_qopt->min_rate[i]) {
			dev_err(&adapter->pdev->dev,
				"Invalid min tx rate (greater than 0) specified\n");
			return -EINVAL;
		}
		/*convert to Mbps */
		tx_rate = div_u64(mqprio_qopt->max_rate[i], IECM_MBPS_DIVISOR);
		total_max_rate += tx_rate;
		num_qs += mqprio_qopt->qopt.count[i];
	}
	/* Comparing with num_txq as num_txq and num_rxq are equal for single
	 * queue model
	 */
	if (num_qs > vport->num_txq) {
		dev_err(&adapter->pdev->dev,
			"Cannot support requested number of queues\n");
		return -EINVAL;
	}
	/* no point in validating TX bandwidth rate limit if the user hasn't
	 * specified any rate limit for any TCs, so validate only if it's set.
	 */
	if (total_max_rate)
		return iecm_validate_tx_bandwidth(vport, total_max_rate);
	else
		return 0;
}

/**
 * __iecm_setup_tc - configure multiple traffic classes
 * @vport: vport structure
 * @type_data: tc offload data
 *
 * This function processes the config information provided by the
 * user to configure traffic classes/queue channels and packages the
 * information to request the PF to setup traffic classes.
 *
 * Returns 0 on success.
 **/
static int __iecm_setup_tc(struct iecm_vport *vport, void *type_data)
{
	struct iecm_adapter *adapter = vport->adapter;
	struct tc_mqprio_qopt_offload *mqprio_qopt;
	struct net_device *netdev = vport->netdev;
	struct iecm_channel_config *ch_config;
	u8 num_tc = 0, total_qs = 0;
	int ret = 0;
	u8 max_tc_allowed;
	u64 max_tx_rate;
	u16 mode;

	mqprio_qopt = (struct tc_mqprio_qopt_offload *)type_data;
	ch_config = &adapter->config_data.ch_config;
	num_tc = mqprio_qopt->qopt.num_tc;
	mode = mqprio_qopt->mode;

	/* delete queue_channel */
	if (!mqprio_qopt->qopt.hw) {
		if (ch_config->tc_running) {
			/* reset the tc configuration */
			netdev_reset_tc(netdev);
			ch_config->num_tc = 0;
			netif_tx_stop_all_queues(netdev);
			netif_tx_disable(netdev);
			ret = iecm_send_disable_channels_msg(vport);
			netif_tx_start_all_queues(netdev);
			if (!test_bit(__IECM_REL_RES_IN_PROG, adapter->flags) &&
			    !ret) {
				ch_config->tc_running = false;
				set_bit(__IECM_HR_FUNC_RESET, adapter->flags);
				queue_delayed_work(adapter->vc_event_wq,
						   &adapter->vc_event_task,
						   msecs_to_jiffies(10));
			}
			return ret;
		} else {
			return -EINVAL;
		}
	}

	if (mode == TC_MQPRIO_MODE_CHANNEL) {
		int i, netdev_tc = 0;

		if (!iecm_is_cap_ena(adapter, IECM_BASE_CAPS,
				     VIRTCHNL2_CAP_ADQ) &&
		    !iecm_is_cap_ena(adapter, IECM_OTHER_CAPS,
					     VIRTCHNL2_CAP_ADQ)) {
			dev_info(&adapter->pdev->dev, "ADQ not supported\n");
			return -EOPNOTSUPP;
		}

		if (ch_config->tc_running) {
			dev_info(&adapter->pdev->dev, "TC configuration already exists\n");
			return -EINVAL;
		}

		/* If negotiated capability between VF and PF indicated that
		 * ADQ_V2 is enabled, means it's OK to allow max_tc
		 * to be 16. This is needed to handle the case where iAVF
		 * is newer but PF is older or different generation
		 */
		if (iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_ADQ))
			max_tc_allowed = VIRTCHNL_MAX_ADQ_V2_CHANNELS;
		else
			max_tc_allowed = VIRTCHNL_MAX_ADQ_CHANNELS;

		ret = iecm_validate_ch_config(vport, mqprio_qopt,
					      max_tc_allowed);
		if (ret)
			return ret;
		/* Return if same TC config is requested */
		if (ch_config->num_tc == num_tc)
			return 0;
		ch_config->num_tc = num_tc;

		for (i = 0; i < max_tc_allowed; i++) {
			if (i < num_tc) {
				ch_config->ch_info[i].count =
					mqprio_qopt->qopt.count[i];
				ch_config->ch_info[i].offset =
					mqprio_qopt->qopt.offset[i];
				total_qs += mqprio_qopt->qopt.count[i];
				max_tx_rate = mqprio_qopt->max_rate[i];
				/* convert to Mbps */
				max_tx_rate = div_u64(max_tx_rate,
						      IECM_MBPS_DIVISOR);
				ch_config->ch_info[i].max_tx_rate =
								max_tx_rate;
			} else {
				ch_config->ch_info[i].count = 1;
				ch_config->ch_info[i].offset = 0;
			}
		}

		/* Store queue info based on TC so that, VF gets configured
		 * with correct number of queues when VF completes ADQ config
		 * flow
		 */
		ch_config->total_qs = total_qs;

		netif_tx_stop_all_queues(netdev);
		netif_tx_disable(netdev);
		ret = iecm_send_enable_channels_msg(vport);
		if (ret)
			return ret;
		netdev_reset_tc(netdev);
		/* Report the tc mapping up the stack */
		netdev_set_num_tc(netdev, num_tc);
		for (i = 0; i < max_tc_allowed; i++) {
			u16 qcount = mqprio_qopt->qopt.count[i];
			u16 qoffset = mqprio_qopt->qopt.offset[i];

			if (i < num_tc)
				netdev_set_tc_queue(netdev, netdev_tc++, qcount,
						    qoffset);
		}
		/* Start all queues */
		netif_tx_start_all_queues(netdev);
		ch_config->tc_running = true;
		set_bit(__IECM_HR_FUNC_RESET, adapter->flags);
		queue_delayed_work(adapter->vc_event_wq,
				   &adapter->vc_event_task,
				   msecs_to_jiffies(10));
	}
	return ret;
}

/**
 * iecm_setup_tc - ndo callback to setup up TC schedulers
 * @netdev: pointer to net_device struct
 * @type: TC type
 * @type_data: TC type specific data
 */
static int iecm_setup_tc(struct net_device *netdev, enum tc_setup_type type,
			 void *type_data)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);
	struct iecm_adapter *adapter = vport->adapter;
	int err = 0;

	switch (type) {
	case TC_SETUP_QDISC_ETF:
		if (iecm_is_queue_model_split(vport->txq_model))
			err =
			iecm_offload_txtime(vport,
					    (struct tc_etf_qopt_offload *)
					     type_data);
		break;
	case TC_SETUP_BLOCK:
		break;
	case TC_SETUP_QDISC_MQPRIO:
		if (iecm_is_cap_ena(adapter, IECM_BASE_CAPS,
				    VIRTCHNL2_CAP_ADQ) ||
		    iecm_is_cap_ena(adapter, IECM_OTHER_CAPS, VIRTCHNL2_CAP_ADQ))
			__iecm_setup_tc(vport, type_data);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

/**
 * iecm_set_mac - NDO callback to set port mac address
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int iecm_set_mac(struct net_device *netdev, void *p)
{
	struct iecm_vport *vport = iecm_netdev_to_vport(netdev);
	struct iecm_mac_filter *f;
	struct sockaddr *addr = p;

	if (!iecm_is_cap_ena(vport->adapter, IECM_OTHER_CAPS,
			     VIRTCHNL2_CAP_MACFILTER)) {
		dev_info(&vport->adapter->pdev->dev, "Setting MAC address is not supported\n");
		return -EOPNOTSUPP;
	}

	if (!is_valid_ether_addr(addr->sa_data)) {
		dev_info(&vport->adapter->pdev->dev, "Invalid MAC address: %pM\n",
			 addr->sa_data);
		return -EADDRNOTAVAIL;
	}

	if (ether_addr_equal(netdev->dev_addr, addr->sa_data))
		return 0;

	/* Delete the current filter */
	if (is_valid_ether_addr(vport->default_mac_addr))
		iecm_del_mac_filter(vport, vport->default_mac_addr);

	/* Add new filter */
	f = iecm_add_mac_filter(vport, addr->sa_data);

	if (f) {
		ether_addr_copy(vport->default_mac_addr, addr->sa_data);
		dev_addr_mod(netdev, 0, addr->sa_data, ETH_ALEN);
	}

	return f ? 0 : -ENOMEM;
}

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

static const struct net_device_ops iecm_netdev_ops_splitq = {
	.ndo_open = iecm_open,
	.ndo_stop = iecm_stop,
	.ndo_start_xmit = iecm_tx_splitq_start,
	.ndo_set_rx_mode = iecm_set_rx_mode,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = iecm_set_mac,
	.ndo_change_mtu = iecm_change_mtu,
	.ndo_get_stats64 = iecm_get_stats64,
	.ndo_fix_features = iecm_fix_features,
	.ndo_set_features = iecm_set_features,
	.ndo_vlan_rx_add_vid = iecm_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = iecm_vlan_rx_kill_vid,
	.ndo_setup_tc = iecm_setup_tc,
};

static const struct net_device_ops iecm_netdev_ops_singleq = {
	.ndo_open = iecm_open,
	.ndo_stop = iecm_stop,
	.ndo_start_xmit = NULL,
	.ndo_set_rx_mode = iecm_set_rx_mode,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = iecm_set_mac,
	.ndo_change_mtu = iecm_change_mtu,
	.ndo_get_stats64 = iecm_get_stats64,
	.ndo_fix_features = iecm_fix_features,
	.ndo_set_features = iecm_set_features,
	.ndo_vlan_rx_add_vid = iecm_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = iecm_vlan_rx_kill_vid,
	.ndo_setup_tc = iecm_setup_tc,
};
