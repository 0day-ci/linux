/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights
 */

#ifndef ATH11K_CFR_H
#define ATH11K_CFR_H

#include "dbring.h"
#include "wmi.h"

#define ATH11K_CFR_NUM_RESP_PER_EVENT   1
#define ATH11K_CFR_EVENT_TIMEOUT_MS     1

#define CFR_MAX_LUT_ENTRIES 136

#define HOST_MAX_CHAINS 8

struct ath11k_cfir_dma_hdr {
	u16 info0;
	u16 info1;
	u16 sw_peer_id;
	u16 phy_ppdu_id;
};

struct ath11k_look_up_table {
	bool dbr_recv;
	bool tx_recv;
	u8 *data;
	u32 data_len;
	u16 dbr_ppdu_id;
	u16 tx_ppdu_id;
	dma_addr_t dbr_address;
	u32 tx_address1;
	u32 tx_address2;
	struct ath11k_cfir_dma_hdr hdr;
	u64 txrx_tstamp;
	u64 dbr_tstamp;
	u32 header_length;
	u32 payload_length;
	struct ath11k_dbring_element *buff;
};

struct ath11k_cfr {
	struct ath11k_dbring rx_ring;
	/* Protects cfr data */
	spinlock_t lock;
	struct ath11k_look_up_table *lut;
	u32 lut_num;
	u32 dbr_buf_size;
	u32 dbr_num_bufs;
	u32 max_mu_users;
	/* Protect for lut entries */
	spinlock_t lut_lock;
	u64 tx_evt_cnt;
	u64 dbr_evt_cnt;
	u64 total_tx_evt_cnt;
	u64 release_cnt;
	u64 tx_peer_status_cfr_fail;
	u64 tx_evt_status_cfr_fail;
	u64 tx_dbr_lookup_fail;
	u64 last_success_tstamp;
	u64 flush_dbr_cnt;
	u64 invalid_dma_length_cnt;
	u64 clear_txrx_event;
	u64 cfr_dma_aborts;
	u64 flush_timeout_dbr_cnt;
};

#ifdef CONFIG_ATH11K_CFR
int ath11k_cfr_init(struct ath11k_base *ab);
void ath11k_cfr_deinit(struct ath11k_base *ab);
void ath11k_cfr_lut_update_paddr(struct ath11k *ar, dma_addr_t paddr,
				 u32 buf_id);
#else
static inline int ath11k_cfr_init(struct ath11k_base *ab)
{
	return 0;
}

static inline void ath11k_cfr_deinit(struct ath11k_base *ab)
{
}

static inline void ath11k_cfr_lut_update_paddr(struct ath11k *ar,
					       dma_addr_t paddr, u32 buf_id)
{
}
#endif /* CONFIG_ATH11K_CFR */
#endif /* ATH11K_CFR_H */
