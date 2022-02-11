// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights
 */

#include <linux/relay.h>
#include "core.h"
#include "debug.h"

static int ath11k_cfr_process_data(struct ath11k *ar,
				   struct ath11k_dbring_data *param)
{
	return 0;
}

/* Helper function to check whether the given peer mac address
 * is in unassociated peer pool or not.
 */
bool ath11k_cfr_peer_is_in_cfr_unassoc_pool(struct ath11k *ar, const u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	int i;

	if (!ar->cfr_enabled)
		return false;

	spin_lock_bh(&cfr->lock);
	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];
		if (!entry->is_valid)
			continue;

		if (ether_addr_equal(peer_mac, entry->peer_mac)) {
			spin_unlock_bh(&cfr->lock);
			return true;
		}
	}

	spin_unlock_bh(&cfr->lock);

	return false;
}

void ath11k_cfr_update_unassoc_pool_entry(struct ath11k *ar,
					  const u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	int i;

	spin_lock_bh(&cfr->lock);
	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];
		if (!entry->is_valid)
			continue;

		if (ether_addr_equal(peer_mac, entry->peer_mac) &&
		    entry->period == 0) {
			memset(entry->peer_mac, 0, ETH_ALEN);
			entry->is_valid = false;
			cfr->cfr_enabled_peer_cnt--;
			break;
		}
	}

	spin_unlock_bh(&cfr->lock);
}

void ath11k_cfr_decrement_peer_count(struct ath11k *ar,
				     struct ath11k_sta *arsta)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	spin_lock_bh(&cfr->lock);

	if (arsta->cfr_capture.cfr_enable)
		cfr->cfr_enabled_peer_cnt--;

	spin_unlock_bh(&cfr->lock);
}

static enum ath11k_wmi_cfr_capture_bw
ath11k_cfr_bw_to_fw_cfr_bw(enum ath11k_cfr_capture_bw bw)
{
	switch (bw) {
	case ATH11K_CFR_CAPTURE_BW_20:
		return WMI_PEER_CFR_CAPTURE_BW_20;
	case ATH11K_CFR_CAPTURE_BW_40:
		return WMI_PEER_CFR_CAPTURE_BW_40;
	case ATH11K_CFR_CAPTURE_BW_80:
		return WMI_PEER_CFR_CAPTURE_BW_80;
	default:
		return WMI_PEER_CFR_CAPTURE_BW_MAX;
	}
}

static enum ath11k_wmi_cfr_capture_method
ath11k_cfr_method_to_fw_cfr_method(enum ath11k_cfr_capture_method method)
{
	switch (method) {
	case ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME:
		return WMI_CFR_CAPTURE_METHOD_NULL_FRAME;
	case ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME_WITH_PHASE:
		return WMI_CFR_CAPTURE_METHOD_NULL_FRAME_WITH_PHASE;
	case ATH11K_CFR_CAPTURE_METHOD_PROBE_RESP:
		return WMI_CFR_CAPTURE_METHOD_PROBE_RESP;
	default:
		return WMI_CFR_CAPTURE_METHOD_MAX;
	}
}

int ath11k_cfr_send_peer_cfr_capture_cmd(struct ath11k *ar,
					 struct ath11k_sta *arsta,
					 struct ath11k_per_peer_cfr_capture *params,
					 const u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct wmi_peer_cfr_capture_conf_arg arg;
	enum ath11k_wmi_cfr_capture_bw bw;
	enum ath11k_wmi_cfr_capture_method method;
	int ret = 0;

	if (cfr->cfr_enabled_peer_cnt >= ATH11K_MAX_CFR_ENABLED_CLIENTS &&
	    !arsta->cfr_capture.cfr_enable) {
		ath11k_err(ar->ab, "CFR enable peer threshold reached %u\n",
			   cfr->cfr_enabled_peer_cnt);
		return -ENOSPC;
	}

	if (params->cfr_enable == arsta->cfr_capture.cfr_enable &&
	    params->cfr_period == arsta->cfr_capture.cfr_period &&
	    params->cfr_method == arsta->cfr_capture.cfr_method &&
	    params->cfr_bw == arsta->cfr_capture.cfr_bw)
		return ret;

	if (!params->cfr_enable && !arsta->cfr_capture.cfr_enable)
		return ret;

	bw = ath11k_cfr_bw_to_fw_cfr_bw(params->cfr_bw);
	if (bw >= WMI_PEER_CFR_CAPTURE_BW_MAX) {
		ath11k_warn(ar->ab, "FW doesn't support configured bw %d\n",
			    params->cfr_bw);
		return -EINVAL;
	}

	method = ath11k_cfr_method_to_fw_cfr_method(params->cfr_method);
	if (method >= WMI_CFR_CAPTURE_METHOD_MAX) {
		ath11k_warn(ar->ab, "FW doesn't support configured method %d\n",
			    params->cfr_method);
		return -EINVAL;
	}

	arg.request = params->cfr_enable;
	arg.periodicity = params->cfr_period;
	arg.bw = bw;
	arg.method = method;

	ret = ath11k_wmi_peer_set_cfr_capture_conf(ar, arsta->arvif->vdev_id,
						   peer_mac, &arg);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to send cfr capture info: vdev_id %u peer %pM\n",
			    arsta->arvif->vdev_id, peer_mac);
		return ret;
	}

	spin_lock_bh(&cfr->lock);

	if (params->cfr_enable &&
	    params->cfr_enable != arsta->cfr_capture.cfr_enable)
		cfr->cfr_enabled_peer_cnt++;
	else if (!params->cfr_enable)
		cfr->cfr_enabled_peer_cnt--;

	spin_unlock_bh(&cfr->lock);

	arsta->cfr_capture.cfr_enable = params->cfr_enable;
	arsta->cfr_capture.cfr_period = params->cfr_period;
	arsta->cfr_capture.cfr_method = params->cfr_method;
	arsta->cfr_capture.cfr_bw = params->cfr_bw;

	return ret;
}

void ath11k_cfr_update_unassoc_pool(struct ath11k *ar,
				    struct ath11k_per_peer_cfr_capture *params,
				    u8 *peer_mac)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct cfr_unassoc_pool_entry *entry;
	int i;
	int available_idx = -1;

	spin_lock_bh(&cfr->lock);

	if (!params->cfr_enable) {
		for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
			entry = &cfr->unassoc_pool[i];
			if (ether_addr_equal(peer_mac, entry->peer_mac)) {
				memset(entry->peer_mac, 0, ETH_ALEN);
				entry->is_valid = false;
				cfr->cfr_enabled_peer_cnt--;
				break;
			}
		}

		goto exit;
	}

	if (cfr->cfr_enabled_peer_cnt >= ATH11K_MAX_CFR_ENABLED_CLIENTS) {
		ath11k_info(ar->ab, "Max cfr peer threshold reached\n");
		goto exit;
	}

	for (i = 0; i < ATH11K_MAX_CFR_ENABLED_CLIENTS; i++) {
		entry = &cfr->unassoc_pool[i];

		if (ether_addr_equal(peer_mac, entry->peer_mac)) {
			ath11k_info(ar->ab,
				    "peer entry already present updating params\n");
			entry->period = params->cfr_period;
			available_idx = -1;
			break;
		}

		if (available_idx < 0 && !entry->is_valid)
			available_idx = i;
	}

	if (available_idx >= 0) {
		entry = &cfr->unassoc_pool[available_idx];
		ether_addr_copy(entry->peer_mac, peer_mac);
		entry->period = params->cfr_period;
		entry->is_valid = true;
		cfr->cfr_enabled_peer_cnt++;
	}

exit:
	spin_unlock_bh(&cfr->lock);
}

static struct dentry *create_buf_file_handler(const char *filename,
					      struct dentry *parent,
					      umode_t mode,
					      struct rchan_buf *buf,
					      int *is_global)
{
	struct dentry *buf_file;

	buf_file = debugfs_create_file(filename, mode, parent, buf,
				       &relay_file_operations);
	*is_global = 1;
	return buf_file;
}

static int remove_buf_file_handler(struct dentry *dentry)
{
	debugfs_remove(dentry);

	return 0;
}

static const struct rchan_callbacks rfs_cfr_capture_cb = {
	.create_buf_file = create_buf_file_handler,
	.remove_buf_file = remove_buf_file_handler,
};

void ath11k_cfr_lut_update_paddr(struct ath11k *ar, dma_addr_t paddr,
				 u32 buf_id)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct ath11k_look_up_table *lut;

	if (cfr->lut) {
		lut = &cfr->lut[buf_id];
		lut->dbr_address = paddr;
	}
}

void ath11k_cfr_ring_free(struct ath11k *ar)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	ath11k_dbring_buf_cleanup(ar, &cfr->rx_ring);
	ath11k_dbring_srng_cleanup(ar, &cfr->rx_ring);
}

static int ath11k_cfr_ring_alloc(struct ath11k *ar,
				 struct ath11k_dbring_cap *db_cap)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	int ret;

	ret = ath11k_dbring_srng_setup(ar, &cfr->rx_ring,
				       1, db_cap->min_elem);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring\n");
		return ret;
	}

	ath11k_dbring_set_cfg(ar, &cfr->rx_ring,
			      ATH11K_CFR_NUM_RESP_PER_EVENT,
			      ATH11K_CFR_EVENT_TIMEOUT_MS,
			      ath11k_cfr_process_data);

	ret = ath11k_dbring_buf_setup(ar, &cfr->rx_ring, db_cap);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring buffer\n");
		goto srng_cleanup;
	}

	ret = ath11k_dbring_wmi_cfg_setup(ar, &cfr->rx_ring, WMI_DIRECT_BUF_CFR);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring cfg\n");
		goto buffer_cleanup;
	}

	return 0;

buffer_cleanup:
	ath11k_dbring_buf_cleanup(ar, &cfr->rx_ring);
srng_cleanup:
	ath11k_dbring_srng_cleanup(ar, &cfr->rx_ring);
	return ret;
}

void ath11k_cfr_deinit(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_cfr *cfr;
	int i;

	if (!test_bit(WMI_TLV_SERVICE_CFR_CAPTURE_SUPPORT, ab->wmi_ab.svc_map) ||
	    !ab->hw_params.cfr_support)
		return;

	for (i = 0; i <  ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		cfr = &ar->cfr;

		if (ar->cfr.rfs_cfr_capture) {
			relay_close(ar->cfr.rfs_cfr_capture);
			ar->cfr.rfs_cfr_capture = NULL;
		}

		ath11k_cfr_ring_free(ar);

		spin_lock_bh(&cfr->lut_lock);
		kfree(cfr->lut);
		cfr->lut = NULL;
		spin_unlock_bh(&cfr->lut_lock);
		ar->cfr_enabled = false;
	}
}

int ath11k_cfr_init(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_cfr *cfr;
	struct ath11k_dbring_cap db_cap;
	u32 num_lut_entries;
	int ret = 0;
	int i;

	if (!test_bit(WMI_TLV_SERVICE_CFR_CAPTURE_SUPPORT, ab->wmi_ab.svc_map) ||
	    !ab->hw_params.cfr_support)
		return ret;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		cfr = &ar->cfr;

		ret = ath11k_dbring_get_cap(ar->ab, ar->pdev_idx,
					    WMI_DIRECT_BUF_CFR, &db_cap);
		if (ret)
			continue;

		idr_init(&cfr->rx_ring.bufs_idr);
		spin_lock_init(&cfr->rx_ring.idr_lock);
		spin_lock_init(&cfr->lock);
		spin_lock_init(&cfr->lut_lock);

		num_lut_entries = min_t(u32, CFR_MAX_LUT_ENTRIES, db_cap.min_elem);

		cfr->lut = kcalloc(num_lut_entries, sizeof(*cfr->lut),
				   GFP_KERNEL);

		if (!cfr->lut) {
			ath11k_warn(ab, "failed to allocate lut for pdev %d\n", i);
			return -ENOMEM;
		}

		ret = ath11k_cfr_ring_alloc(ar, &db_cap);
		if (ret) {
			ath11k_warn(ab, "failed to init cfr ring for pdev %d\n", i);
			goto deinit;
		}

		cfr->lut_num = num_lut_entries;

		ret = ath11k_wmi_pdev_set_param(ar, WMI_PDEV_PARAM_PER_PEER_CFR_ENABLE,
						1, ar->pdev->pdev_id);
		if (ret) {
			ath11k_warn(ab, "failed to enable cfr capture on pdev %d ret %d\n",
				    i, ret);
			goto deinit;
		}

		ar->cfr_enabled = true;

		ar->cfr.rfs_cfr_capture =
				relay_open("cfr_capture",
					   ar->debug.debugfs_pdev,
					   ar->ab->hw_params.cfr_stream_buf_size,
					   ar->ab->hw_params.cfr_num_stream_bufs,
					   &rfs_cfr_capture_cb, NULL);
		if (!ar->cfr.rfs_cfr_capture) {
			ath11k_warn(ar->ab, "failed to open relay for cfr in pdev %d\n",
				    ar->pdev_idx);
			return -EINVAL;
		}
	}

	return 0;

deinit:
	ath11k_cfr_deinit(ab);
	return ret;
}
