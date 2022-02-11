// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights
 */

#include <linux/relay.h>
#include "core.h"
#include "debug.h"

struct ath11k_dbring *ath11k_cfr_get_dbring(struct ath11k *ar)
{
	if (ar->cfr_enabled)
		return &ar->cfr.rx_ring;

	return NULL;
}

static int cfr_calculate_tones_from_dma_hdr(struct ath11k_cfir_dma_hdr *hdr)
{
	u8 bw = FIELD_GET(CFIR_DMA_HDR_INFO1_UPLOAD_PKT_BW, hdr->info1);
	u8 preamble = FIELD_GET(CFIR_DMA_HDR_INFO1_PREAMBLE_TYPE, hdr->info1);

	switch (preamble) {
	case ATH11K_CFR_PREAMBLE_TYPE_LEGACY:
	case ATH11K_CFR_PREAMBLE_TYPE_VHT:
		switch (bw) {
		case 0:
			return TONES_IN_20MHZ;
		case 1: /* DUP40/VHT40 */
			return TONES_IN_40MHZ;
		case 2: /* DUP80/VHT80 */
			return TONES_IN_80MHZ;
		case 3: /* DUP160/VHT160 */
			return TONES_IN_160MHZ;
		default:
			break;
		}

	case ATH11K_CFR_PREAMBLE_TYPE_HT:
		switch (bw) {
		case 0:
			return TONES_IN_20MHZ;
		case 1:
			return TONES_IN_40MHZ;
		}
	}

	return TONES_INVALID;
}

void ath11k_cfr_release_lut_entry(struct ath11k_look_up_table *lut)
{
	memset(lut, 0, sizeof(*lut));
}

static void ath11k_cfr_rfs_write(struct ath11k *ar, const void *head,
				 u32 head_len, const void *data, u32 data_len,
				 const void *tail, int tail_data)
{
	struct ath11k_cfr *cfr = &ar->cfr;

	if (!ar->cfr.rfs_cfr_capture)
		return;

	relay_write(cfr->rfs_cfr_capture, head, head_len);
	relay_write(cfr->rfs_cfr_capture, data, data_len);
	relay_write(cfr->rfs_cfr_capture, tail, tail_data);
	relay_flush(cfr->rfs_cfr_capture);
}

static void ath11k_cfr_free_pending_dbr_events(struct ath11k *ar)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	struct ath11k_look_up_table *lut;
	int i;

	if (!cfr->lut)
		return;

	for (i = 0; i < cfr->lut_num; i++) {
		lut = &cfr->lut[i];
		if (lut->dbr_recv && !lut->tx_recv &&
		    lut->dbr_tstamp < cfr->last_success_tstamp) {
			ath11k_dbring_bufs_replenish(ar, &cfr->rx_ring, lut->buff,
						     WMI_DIRECT_BUF_CFR);
			ath11k_cfr_release_lut_entry(lut);
			cfr->flush_dbr_cnt++;
		}
	}
}

/* Correlate and relay: This function correlate the data coming from
 * WMI_PDEV_DMA_RING_BUF_RELEASE_EVENT(DBR event) and
 * WMI_PEER_CFR_CAPTURE_EVENT(Tx capture event).
 * If both the events are received and PPDU id matches from both the
 * events, return CORRELATE_STATUS_RELEASE which means relay the
 * correlated data to user space. Otherwise return CORRELATE_STATUS_HOLD
 * which means wait for the second event to come.
 *
 * It also check for the pending DBR events and clear those events
 * in case of corresponding TX capture event is not received for
 * the PPDU.
 */

static enum ath11k_cfr_correlate_status
ath11k_cfr_correlate_and_relay(struct ath11k *ar,
			       struct ath11k_look_up_table *lut,
			       u8 event_type)
{
	struct ath11k_cfr *cfr = &ar->cfr;
	enum ath11k_cfr_correlate_status status;
	u64 diff;

	if (event_type == ATH11K_CORRELATE_TX_EVENT) {
		if (lut->tx_recv)
			cfr->cfr_dma_aborts++;
		cfr->tx_evt_cnt++;
		lut->tx_recv = true;
	} else if (event_type == ATH11K_CORRELATE_DBR_EVENT) {
		cfr->dbr_evt_cnt++;
		lut->dbr_recv = true;
	}

	if (lut->dbr_recv && lut->tx_recv) {
		if (lut->dbr_ppdu_id == lut->tx_ppdu_id) {
			/* We are using 64-bit counters here. So, it may take
			 * several year to hit wraparound. Hence, not handling
			 * the wraparound condition.
			 */
			cfr->last_success_tstamp = lut->dbr_tstamp;
			if (lut->dbr_tstamp > lut->txrx_tstamp) {
				diff = lut->dbr_tstamp - lut->txrx_tstamp;
				ath11k_dbg(ar->ab, ATH11K_DBG_CFR,
					   "txrx event -> dbr event delay = %u ms",
					   jiffies_to_msecs(diff));
			} else if (lut->txrx_tstamp > lut->dbr_tstamp) {
				diff = lut->txrx_tstamp - lut->dbr_tstamp;
				ath11k_dbg(ar->ab, ATH11K_DBG_CFR,
					   "dbr event -> txrx event delay = %u ms",
					   jiffies_to_msecs(diff));
			}

			ath11k_cfr_free_pending_dbr_events(ar);

			cfr->release_cnt++;
			status = ATH11K_CORRELATE_STATUS_RELEASE;
		} else {
			/* When there is a ppdu id mismatch, discard the TXRX
			 * event since multiple PPDUs are likely to have same
			 * dma addr, due to ucode aborts.
			 */

			ath11k_dbg(ar->ab, ATH11K_DBG_CFR,
				   "Received dbr event twice for the same lut entry");
			lut->tx_recv = false;
			lut->tx_ppdu_id = 0;
			cfr->clear_txrx_event++;
			cfr->cfr_dma_aborts++;
			status = ATH11K_CORRELATE_STATUS_HOLD;
		}
	} else {
		status = ATH11K_CORRELATE_STATUS_HOLD;
	}

	return status;
}

static int ath11k_cfr_process_data(struct ath11k *ar,
				   struct ath11k_dbring_data *param)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_cfr *cfr = &ar->cfr;
	struct ath11k_look_up_table *lut;
	struct ath11k_csi_cfr_header *header;
	struct ath11k_cfir_dma_hdr *dma_hdr;
	u8 *data;
	u32 end_magic = ATH11K_CFR_END_MAGIC;
	u32 buf_id;
	u32 tones;
	u32 length;
	int status;
	u8 num_chains;

	data = param->data;
	buf_id = param->buf_id;

	if (param->data_sz < sizeof(*dma_hdr))
		return -EINVAL;

	dma_hdr = (struct ath11k_cfir_dma_hdr *)data;

	tones = cfr_calculate_tones_from_dma_hdr(dma_hdr);
	if (tones == TONES_INVALID) {
		ath11k_warn(ar->ab, "Number of tones received is invalid");
		return -EINVAL;
	}

	num_chains = FIELD_GET(CFIR_DMA_HDR_INFO1_NUM_CHAINS,
			       dma_hdr->info1);

	length = sizeof(*dma_hdr);
	length += tones * (num_chains + 1);

	spin_lock_bh(&cfr->lut_lock);

	if (!cfr->lut) {
		spin_unlock_bh(&cfr->lut_lock);
		return -EINVAL;
	}

	lut = &cfr->lut[buf_id];

	ath11k_dbg_dump(ab, ATH11K_DBG_CFR_DUMP, "data_from_buf_rel:", "",
			data, length);

	lut->buff = param->buff;
	lut->data = data;
	lut->data_len = length;
	lut->dbr_ppdu_id = dma_hdr->phy_ppdu_id;
	lut->dbr_tstamp = jiffies;

	memcpy(&lut->hdr, dma_hdr, sizeof(*dma_hdr));

	header = &lut->header;
	header->meta_data.channel_bw = FIELD_GET(CFIR_DMA_HDR_INFO1_UPLOAD_PKT_BW,
						 dma_hdr->info1);
	header->meta_data.length = length;

	status = ath11k_cfr_correlate_and_relay(ar, lut,
						ATH11K_CORRELATE_DBR_EVENT);
	if (status == ATH11K_CORRELATE_STATUS_RELEASE) {
		ath11k_dbg(ab, ATH11K_DBG_CFR,
			   "releasing CFR data to user space");
		ath11k_cfr_rfs_write(ar, &lut->header,
				     sizeof(struct ath11k_csi_cfr_header),
				     lut->data, lut->data_len,
				     &end_magic, sizeof(u32));
		ath11k_cfr_release_lut_entry(lut);
	} else if (status == ATH11K_CORRELATE_STATUS_HOLD) {
		ath11k_dbg(ab, ATH11K_DBG_CFR,
			   "tx event is not yet received holding the buf");
	}

	spin_unlock_bh(&cfr->lut_lock);

	return status;
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
