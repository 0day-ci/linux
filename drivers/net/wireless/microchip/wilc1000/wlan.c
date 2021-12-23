// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <net/dsfield.h>
#include "cfg80211.h"
#include "wlan_cfg.h"

#define WAKE_UP_TRIAL_RETRY		10000

#define NOT_TCP_ACK			(-1)

static const u8 factors[NQUEUES] = {1, 1, 1, 1};

static void tcp_process(struct net_device *, struct sk_buff *);

static inline bool is_wilc1000(u32 id)
{
	return (id & (~WILC_CHIP_REV_FIELD)) == WILC_1000_BASE_ID;
}

static inline void acquire_bus(struct wilc *wilc, enum bus_acquire acquire)
{
	mutex_lock(&wilc->hif_cs);
	if (acquire == WILC_BUS_ACQUIRE_AND_WAKEUP && wilc->power_save_mode)
		chip_wakeup(wilc);
}

static inline void release_bus(struct wilc *wilc, enum bus_release release)
{
	if (release == WILC_BUS_RELEASE_ALLOW_SLEEP && wilc->power_save_mode)
		chip_allow_sleep(wilc);
	mutex_unlock(&wilc->hif_cs);
}

static void init_txq_entry(struct sk_buff *tqe,
			   u8 type, enum ip_pkt_priority q_num)
{
	struct wilc_skb_tx_cb *tx_cb = WILC_SKB_TX_CB(tqe);

	tx_cb->type = type;
	tx_cb->q_num = q_num;
	tx_cb->ack_idx = NOT_TCP_ACK;
}

static void wilc_wlan_txq_add_to_tail(struct net_device *dev, u8 type, u8 q_num,
				      struct sk_buff *tqe)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	init_txq_entry(tqe, type, q_num);
	if (type == WILC_NET_PKT && vif->ack_filter.enabled)
		tcp_process(dev, tqe);

	skb_queue_tail(&wilc->txq[q_num], tqe);
	atomic_inc(&wilc->txq_entries);

	wake_up_interruptible(&wilc->txq_event);
}

static void wilc_wlan_txq_add_to_head(struct wilc_vif *vif, u8 type, u8 q_num,
				      struct sk_buff *tqe)
{
	struct wilc *wilc = vif->wilc;

	init_txq_entry(tqe, type, q_num);

	skb_queue_head(&wilc->txq[q_num], tqe);
	atomic_inc(&wilc->txq_entries);

	wake_up_interruptible(&wilc->txq_event);
}

static inline void add_tcp_session(struct wilc_vif *vif, u32 src_prt,
				   u32 dst_prt, u32 seq)
{
	struct tcp_ack_filter *f = &vif->ack_filter;

	if (f->tcp_session < 2 * MAX_TCP_SESSION) {
		f->ack_session_info[f->tcp_session].seq_num = seq;
		f->ack_session_info[f->tcp_session].bigger_ack_num = 0;
		f->ack_session_info[f->tcp_session].src_port = src_prt;
		f->ack_session_info[f->tcp_session].dst_port = dst_prt;
		f->tcp_session++;
	}
}

static inline void update_tcp_session(struct wilc_vif *vif, u32 index, u32 ack)
{
	struct tcp_ack_filter *f = &vif->ack_filter;

	if (index < 2 * MAX_TCP_SESSION &&
	    ack > f->ack_session_info[index].bigger_ack_num)
		f->ack_session_info[index].bigger_ack_num = ack;
}

static inline void add_tcp_pending_ack(struct wilc_vif *vif, u32 ack,
				       u32 session_index,
				       struct sk_buff *txqe)
{
	struct wilc_skb_tx_cb *tx_cb = WILC_SKB_TX_CB(txqe);
	struct tcp_ack_filter *f = &vif->ack_filter;
	u32 i = f->pending_base + f->pending_acks_idx;

	if (i < MAX_PENDING_ACKS) {
		f->pending_acks[i].ack_num = ack;
		f->pending_acks[i].txqe = txqe;
		f->pending_acks[i].session_index = session_index;
		tx_cb->ack_idx = i;
		f->pending_acks_idx++;
	}
}

static inline void tcp_process(struct net_device *dev, struct sk_buff *tqe)
{
	void *buffer = tqe->data;
	const struct ethhdr *eth_hdr_ptr = buffer;
	int i;
	struct wilc_vif *vif = netdev_priv(dev);
	struct tcp_ack_filter *f = &vif->ack_filter;
	const struct iphdr *ip_hdr_ptr;
	const struct tcphdr *tcp_hdr_ptr;
	u32 ihl, total_length, data_offset;

	if (eth_hdr_ptr->h_proto != htons(ETH_P_IP))
		return;

	ip_hdr_ptr = buffer + ETH_HLEN;

	if (ip_hdr_ptr->protocol != IPPROTO_TCP)
		return;

	ihl = ip_hdr_ptr->ihl << 2;
	tcp_hdr_ptr = buffer + ETH_HLEN + ihl;
	total_length = ntohs(ip_hdr_ptr->tot_len);

	data_offset = tcp_hdr_ptr->doff << 2;
	if (total_length == (ihl + data_offset)) {
		u32 seq_no, ack_no;

		seq_no = ntohl(tcp_hdr_ptr->seq);
		ack_no = ntohl(tcp_hdr_ptr->ack_seq);

		mutex_lock(&vif->ack_filter_lock);

		for (i = 0; i < f->tcp_session; i++) {
			u32 j = f->ack_session_info[i].seq_num;

			if (i < 2 * MAX_TCP_SESSION &&
			    j == seq_no) {
				update_tcp_session(vif, i, ack_no);
				break;
			}
		}
		if (i == f->tcp_session)
			add_tcp_session(vif, 0, 0, seq_no);

		add_tcp_pending_ack(vif, ack_no, i, tqe);

		mutex_unlock(&vif->ack_filter_lock);
	}
}

static void wilc_wlan_tx_packet_done(struct sk_buff *tqe, int status)
{
	struct wilc_vif *vif = netdev_priv(tqe->dev);
	struct wilc_skb_tx_cb *tx_cb = WILC_SKB_TX_CB(tqe);
	int ack_idx = tx_cb->ack_idx;

	if (ack_idx != NOT_TCP_ACK && ack_idx < MAX_PENDING_ACKS)
		vif->ack_filter.pending_acks[ack_idx].txqe = NULL;
	if (status)
		dev_consume_skb_any(tqe);
	else
		dev_kfree_skb_any(tqe);
}

static void wilc_wlan_txq_drop_net_pkt(struct sk_buff *tqe)
{
	struct wilc_vif *vif = netdev_priv(tqe->dev);
	struct wilc *wilc = vif->wilc;
	struct wilc_skb_tx_cb *tx_cb = WILC_SKB_TX_CB(tqe);

	vif->ndev->stats.tx_dropped++;

	skb_unlink(tqe, &wilc->txq[tx_cb->q_num]);
	atomic_dec(&wilc->txq_entries);
	wilc_wlan_tx_packet_done(tqe, 1);
}

static void wilc_wlan_txq_filter_dup_tcp_ack(struct net_device *dev)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct tcp_ack_filter *f = &vif->ack_filter;
	u32 i = 0;

	mutex_lock(&vif->ack_filter_lock);
	for (i = f->pending_base;
	     i < (f->pending_base + f->pending_acks_idx); i++) {
		u32 index;
		u32 bigger_ack_num;

		if (i >= MAX_PENDING_ACKS)
			break;

		index = f->pending_acks[i].session_index;

		if (index >= 2 * MAX_TCP_SESSION)
			break;

		bigger_ack_num = f->ack_session_info[index].bigger_ack_num;

		if (f->pending_acks[i].ack_num < bigger_ack_num) {
			struct sk_buff *tqe;

			tqe = f->pending_acks[i].txqe;
			if (tqe)
				wilc_wlan_txq_drop_net_pkt(tqe);
		}
	}
	f->pending_acks_idx = 0;
	f->tcp_session = 0;

	if (f->pending_base == 0)
		f->pending_base = MAX_TCP_SESSION;
	else
		f->pending_base = 0;

	mutex_unlock(&vif->ack_filter_lock);
}

void wilc_enable_tcp_ack_filter(struct wilc_vif *vif, bool value)
{
	vif->ack_filter.enabled = value;
}

static int wilc_wlan_txq_add_cfg_pkt(struct wilc_vif *vif, struct sk_buff *tqe)
{
	struct wilc *wilc = vif->wilc;

	netdev_dbg(vif->ndev, "Adding config packet ...\n");
	if (wilc->quit) {
		netdev_dbg(vif->ndev, "Return due to clear function\n");
		dev_kfree_skb_any(tqe);
		return 0;
	}

	wilc_wlan_txq_add_to_head(vif, WILC_CFG_PKT, AC_VO_Q, tqe);

	return 1;
}

static void init_q_limits(struct wilc *wl)
{
	struct wilc_tx_queue_status *q = &wl->tx_q_limit;
	int i;

	for (i = 0; i < AC_BUFFER_SIZE; i++)
		q->buffer[i] = i % NQUEUES;

	for (i = 0; i < NQUEUES; i++) {
		q->cnt[i] = AC_BUFFER_SIZE * factors[i] / NQUEUES;
		q->sum += q->cnt[i];
	}
	q->end_index = AC_BUFFER_SIZE - 1;
}

static bool is_ac_q_limit(struct wilc *wl, u8 q_num)
{
	struct wilc_tx_queue_status *q = &wl->tx_q_limit;
	u8 end_index;
	u8 q_limit;
	bool ret = false;

	mutex_lock(&wl->tx_q_limit_lock);

	end_index = q->end_index;
	q->cnt[q->buffer[end_index]] -= factors[q->buffer[end_index]];
	q->cnt[q_num] += factors[q_num];
	q->sum += (factors[q_num] - factors[q->buffer[end_index]]);

	q->buffer[end_index] = q_num;
	if (end_index > 0)
		q->end_index--;
	else
		q->end_index = AC_BUFFER_SIZE - 1;

	if (!q->sum)
		q_limit = 1;
	else
		q_limit = (q->cnt[q_num] * FLOW_CONTROL_UPPER_THRESHOLD / q->sum) + 1;

	if (skb_queue_len(&wl->txq[q_num]) <= q_limit)
		ret = true;

	mutex_unlock(&wl->tx_q_limit_lock);

	return ret;
}

static inline u8 ac_classify(struct wilc *wilc, struct sk_buff *skb)
{
	u8 q_num = AC_BE_Q;
	u8 dscp;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		dscp = ipv4_get_dsfield(ip_hdr(skb)) & 0xfc;
		break;
	case htons(ETH_P_IPV6):
		dscp = ipv6_get_dsfield(ipv6_hdr(skb)) & 0xfc;
		break;
	default:
		return q_num;
	}

	switch (dscp) {
	case 0x08:
	case 0x20:
	case 0x40:
		q_num = AC_BK_Q;
		break;
	case 0x80:
	case 0xA0:
	case 0x28:
		q_num = AC_VI_Q;
		break;
	case 0xC0:
	case 0xD0:
	case 0xE0:
	case 0x88:
	case 0xB8:
		q_num = AC_VO_Q;
		break;
	}

	return q_num;
}

/**
 * ac_balance() - balance queues by favoring ones with fewer packets pending
 * @wl: Pointer to the wilc structure.
 * @ratio: Pointer to array of length NQUEUES in which this function
 *	returns the number of packets that may be scheduled for each
 *	access category.
 */
static inline void ac_balance(const struct wilc *wl, u8 *ratio)
{
	u8 i, max_count = 0;

	for (i = 0; i < NQUEUES; i++)
		if (wl->fw[i].count > max_count)
			max_count = wl->fw[i].count;

	for (i = 0; i < NQUEUES; i++)
		ratio[i] = max_count - wl->fw[i].count;
}

static inline void ac_update_fw_ac_pkt_info(struct wilc *wl, u32 reg)
{
	wl->fw[AC_BK_Q].count = FIELD_GET(BK_AC_COUNT_FIELD, reg);
	wl->fw[AC_BE_Q].count = FIELD_GET(BE_AC_COUNT_FIELD, reg);
	wl->fw[AC_VI_Q].count = FIELD_GET(VI_AC_COUNT_FIELD, reg);
	wl->fw[AC_VO_Q].count = FIELD_GET(VO_AC_COUNT_FIELD, reg);

	wl->fw[AC_BK_Q].acm = FIELD_GET(BK_AC_ACM_STAT_FIELD, reg);
	wl->fw[AC_BE_Q].acm = FIELD_GET(BE_AC_ACM_STAT_FIELD, reg);
	wl->fw[AC_VI_Q].acm = FIELD_GET(VI_AC_ACM_STAT_FIELD, reg);
	wl->fw[AC_VO_Q].acm = FIELD_GET(VO_AC_ACM_STAT_FIELD, reg);
}

static inline u8 ac_change(struct wilc *wilc, u8 *ac)
{
	do {
		if (wilc->fw[*ac].acm == 0)
			return 0;
		(*ac)++;
	} while (*ac < NQUEUES);

	return 1;
}

int wilc_wlan_txq_add_net_pkt(struct net_device *dev, struct sk_buff *tqe)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc;
	u8 q_num;

	wilc = vif->wilc;

	if (wilc->quit) {
		dev_kfree_skb_any(tqe);
		return 0;
	}

	if (!wilc->initialized) {
		dev_kfree_skb_any(tqe);
		return 0;
	}

	q_num = ac_classify(wilc, tqe);
	if (ac_change(wilc, &q_num)) {
		dev_kfree_skb_any(tqe);
		return 0;
	}

	if (is_ac_q_limit(wilc, q_num)) {
		wilc_wlan_txq_add_to_tail(dev, WILC_NET_PKT, q_num, tqe);
	} else {
		dev_kfree_skb(tqe);
	}

	return atomic_read(&wilc->txq_entries);
}

int wilc_wlan_txq_add_mgmt_pkt(struct net_device *dev, struct sk_buff *tqe)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc;

	wilc = vif->wilc;

	if (wilc->quit) {
		dev_kfree_skb_any(tqe);
		return 0;
	}

	if (!wilc->initialized) {
		dev_kfree_skb_any(tqe);
		return 0;
	}
	wilc_wlan_txq_add_to_tail(dev, WILC_MGMT_PKT, AC_VO_Q, tqe);
	return 1;
}

static void wilc_wlan_rxq_add(struct wilc *wilc, struct rxq_entry_t *rqe)
{
	if (wilc->quit)
		return;

	mutex_lock(&wilc->rxq_cs);
	list_add_tail(&rqe->list, &wilc->rxq_head.list);
	mutex_unlock(&wilc->rxq_cs);
}

static struct rxq_entry_t *wilc_wlan_rxq_remove(struct wilc *wilc)
{
	struct rxq_entry_t *rqe = NULL;

	mutex_lock(&wilc->rxq_cs);
	if (!list_empty(&wilc->rxq_head.list)) {
		rqe = list_first_entry(&wilc->rxq_head.list, struct rxq_entry_t,
				       list);
		list_del(&rqe->list);
	}
	mutex_unlock(&wilc->rxq_cs);
	return rqe;
}

void chip_allow_sleep(struct wilc *wilc)
{
	u32 reg = 0;
	const struct wilc_hif_func *hif_func = wilc->hif_func;
	u32 wakeup_reg, wakeup_bit;
	u32 to_host_from_fw_reg, to_host_from_fw_bit;
	u32 from_host_to_fw_reg, from_host_to_fw_bit;
	u32 trials = 100;
	int ret;

	if (wilc->io_type == WILC_HIF_SDIO) {
		wakeup_reg = WILC_SDIO_WAKEUP_REG;
		wakeup_bit = WILC_SDIO_WAKEUP_BIT;
		from_host_to_fw_reg = WILC_SDIO_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SDIO_HOST_TO_FW_BIT;
		to_host_from_fw_reg = WILC_SDIO_FW_TO_HOST_REG;
		to_host_from_fw_bit = WILC_SDIO_FW_TO_HOST_BIT;
	} else {
		wakeup_reg = WILC_SPI_WAKEUP_REG;
		wakeup_bit = WILC_SPI_WAKEUP_BIT;
		from_host_to_fw_reg = WILC_SPI_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SPI_HOST_TO_FW_BIT;
		to_host_from_fw_reg = WILC_SPI_FW_TO_HOST_REG;
		to_host_from_fw_bit = WILC_SPI_FW_TO_HOST_BIT;
	}

	while (--trials) {
		ret = hif_func->hif_read_reg(wilc, to_host_from_fw_reg, &reg);
		if (ret)
			return;
		if ((reg & to_host_from_fw_bit) == 0)
			break;
	}
	if (!trials)
		pr_warn("FW not responding\n");

	/* Clear bit 1 */
	ret = hif_func->hif_read_reg(wilc, wakeup_reg, &reg);
	if (ret)
		return;
	if (reg & wakeup_bit) {
		reg &= ~wakeup_bit;
		ret = hif_func->hif_write_reg(wilc, wakeup_reg, reg);
		if (ret)
			return;
	}

	ret = hif_func->hif_read_reg(wilc, from_host_to_fw_reg, &reg);
	if (ret)
		return;
	if (reg & from_host_to_fw_bit) {
		reg &= ~from_host_to_fw_bit;
		ret = hif_func->hif_write_reg(wilc, from_host_to_fw_reg, reg);
		if (ret)
			return;

	}
}
EXPORT_SYMBOL_GPL(chip_allow_sleep);

void chip_wakeup(struct wilc *wilc)
{
	u32 ret = 0;
	u32 clk_status_val = 0, trials = 0;
	u32 wakeup_reg, wakeup_bit;
	u32 clk_status_reg, clk_status_bit;
	u32 from_host_to_fw_reg, from_host_to_fw_bit;
	const struct wilc_hif_func *hif_func = wilc->hif_func;

	if (wilc->io_type == WILC_HIF_SDIO) {
		wakeup_reg = WILC_SDIO_WAKEUP_REG;
		wakeup_bit = WILC_SDIO_WAKEUP_BIT;
		clk_status_reg = WILC_SDIO_CLK_STATUS_REG;
		clk_status_bit = WILC_SDIO_CLK_STATUS_BIT;
		from_host_to_fw_reg = WILC_SDIO_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SDIO_HOST_TO_FW_BIT;
	} else {
		wakeup_reg = WILC_SPI_WAKEUP_REG;
		wakeup_bit = WILC_SPI_WAKEUP_BIT;
		clk_status_reg = WILC_SPI_CLK_STATUS_REG;
		clk_status_bit = WILC_SPI_CLK_STATUS_BIT;
		from_host_to_fw_reg = WILC_SPI_HOST_TO_FW_REG;
		from_host_to_fw_bit = WILC_SPI_HOST_TO_FW_BIT;
	}

	/* indicate host wakeup */
	ret = hif_func->hif_write_reg(wilc, from_host_to_fw_reg,
				      from_host_to_fw_bit);
	if (ret)
		return;

	/* Set wake-up bit */
	ret = hif_func->hif_write_reg(wilc, wakeup_reg,
				      wakeup_bit);
	if (ret)
		return;

	while (trials < WAKE_UP_TRIAL_RETRY) {
		ret = hif_func->hif_read_reg(wilc, clk_status_reg,
					     &clk_status_val);
		if (ret) {
			pr_err("Bus error %d %x\n", ret, clk_status_val);
			return;
		}
		if (clk_status_val & clk_status_bit)
			break;

		trials++;
	}
	if (trials >= WAKE_UP_TRIAL_RETRY) {
		pr_err("Failed to wake-up the chip\n");
		return;
	}
	/* Sometimes spi fail to read clock regs after reading
	 * writing clockless registers
	 */
	if (wilc->io_type == WILC_HIF_SPI)
		wilc->hif_func->hif_reset(wilc);
}
EXPORT_SYMBOL_GPL(chip_wakeup);

void host_wakeup_notify(struct wilc *wilc)
{
	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
	wilc->hif_func->hif_write_reg(wilc, WILC_CORTUS_INTERRUPT_2, 1);
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
}
EXPORT_SYMBOL_GPL(host_wakeup_notify);

void host_sleep_notify(struct wilc *wilc)
{
	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
	wilc->hif_func->hif_write_reg(wilc, WILC_CORTUS_INTERRUPT_1, 1);
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
}
EXPORT_SYMBOL_GPL(host_sleep_notify);

/**
 * tx_hdr_len() - calculate tx packet header length
 * @type: The packet type for which to return the header length.
 *
 * Calculate the total header size for a given packet type.  This size
 * includes the 4 bytes required to hold the VMM header.
 *
 * Return: The total size of the header in bytes.
 */
static u32 tx_hdr_len(u8 type)
{
	switch (type) {
	case WILC_NET_PKT:
		return ETH_ETHERNET_HDR_OFFSET;

	case WILC_CFG_PKT:
		return ETH_CONFIG_PKT_HDR_OFFSET;

	case WILC_MGMT_PKT:
		return HOST_HDR_OFFSET;

	default:
		pr_err("%s: Invalid packet type %d.", __func__, type);
		return 4;
	}
}

static u32 vmm_table_entry(struct sk_buff *tqe)
{
	struct wilc_skb_tx_cb *tx_cb = WILC_SKB_TX_CB(tqe);
	u32 entry;

	entry = tqe->len / 4;
	if (tx_cb->type == WILC_CFG_PKT)
		entry |= WILC_VMM_CFG_PKT;
	return cpu_to_le32(entry);
}

/**
 * add_hdr_and_pad() - prepare a packet for the chip queue
 * @wilc: Pointer to the wilc structure.
 * @tqe: The packet to add to the chip queue.
 * @hdr_len: The size of the header to add.
 * @vmm_sz: The final size of the packet, including VMM header and padding.
 *
 * Bring a packet into the form required by the chip by adding a
 * header and padding as needed.
 */
static void add_hdr_and_pad(struct wilc *wilc, struct sk_buff *tqe,
			    u32 hdr_len, u32 vmm_sz)
{
	struct wilc_skb_tx_cb *tx_cb = WILC_SKB_TX_CB(tqe);
	u32 mgmt_pkt = 0, vmm_hdr, prio, data_len = tqe->len;
	struct wilc_vif *vif;
	void *hdr;

	/* grow skb with header and pad bytes, all initialized to 0: */
	hdr = skb_push(tqe, hdr_len);
	if (vmm_sz > tqe->len)
		skb_put(tqe, vmm_sz - tqe->len);

	/* add the VMM header word: */
	if (tx_cb->type == WILC_MGMT_PKT)
		mgmt_pkt = FIELD_PREP(WILC_VMM_HDR_MGMT_FIELD, 1);
	vmm_hdr = cpu_to_le32(mgmt_pkt |
			      FIELD_PREP(WILC_VMM_HDR_TYPE, tx_cb->type) |
			      FIELD_PREP(WILC_VMM_HDR_PKT_SIZE, data_len) |
			      FIELD_PREP(WILC_VMM_HDR_BUFF_SIZE, vmm_sz));
	memcpy(hdr, &vmm_hdr, 4);

	if (tx_cb->type == WILC_NET_PKT) {
		vif = netdev_priv(tqe->dev);
		prio = cpu_to_le32(tx_cb->q_num);
		memcpy(hdr + 4, &prio, sizeof(prio));
		memcpy(hdr + 8, vif->bssid, ETH_ALEN);
	}
}

/**
 * schedule_packets() - schedule packets for transmission
 * @wilc: Pointer to the wilc structure.
 * @vmm_table_len: Current length of the VMM table.
 * @vmm_table: Pointer to the VMM table to fill.
 *
 * Schedule packets from the access-category queues for transmission.
 * The scheduling is primarily in order of priority, but also takes
 * fairness into account.  As many packets as possible are moved to
 * the chip queue.  The chip queue has space for up to
 * WILC_VMM_TBL_SIZE packets or up to WILC_TX_BUFF_SIZE bytes.
 */
static int schedule_packets(struct wilc *wilc,
			    int vmm_table_len, u32 vmm_table[WILC_VMM_TBL_SIZE])
{
	u8 k, ac;
	static const u8 ac_preserve_ratio[NQUEUES] = {1, 1, 1, 1};
	u8 ac_desired_ratio[NQUEUES];
	const u8 *num_pkts_to_add;
	u32 vmm_sz, hdr_len;
	bool ac_exist = 0;
	struct sk_buff *tqe;
	struct wilc_skb_tx_cb *tx_cb;

	ac_balance(wilc, ac_desired_ratio);
	num_pkts_to_add = ac_desired_ratio;
	do {
		ac_exist = 0;
		for (ac = 0; ac < NQUEUES; ac++) {
			if (skb_queue_len(&wilc->txq[ac]) < 1)
				continue;

			ac_exist = 1;
			for (k = 0; k < num_pkts_to_add[ac]; k++) {
				if (vmm_table_len >= WILC_VMM_TBL_SIZE - 1)
					return vmm_table_len;

				tqe = skb_dequeue(&wilc->txq[ac]);
				if (!tqe)
					continue;

				tx_cb = WILC_SKB_TX_CB(tqe);
				hdr_len = tx_hdr_len(tx_cb->type);
				vmm_sz = hdr_len + tqe->len;
				vmm_sz = ALIGN(vmm_sz, 4);

				if (wilc->chipq_bytes + vmm_sz > WILC_TX_BUFF_SIZE) {
					/* return packet to its queue */
					skb_queue_head(&wilc->txq[ac], tqe);
					return vmm_table_len;
				}
				atomic_dec(&wilc->txq_entries);

				add_hdr_and_pad(wilc, tqe, hdr_len, vmm_sz);

				__skb_queue_tail(&wilc->chipq, tqe);
				wilc->chipq_bytes += tqe->len;

				vmm_table[vmm_table_len] = vmm_table_entry(tqe);
				vmm_table_len++;
			}
		}
		num_pkts_to_add = ac_preserve_ratio;
	} while (ac_exist);
	return vmm_table_len;
}

/**
 * fill_vmm_table() - fill VMM table with packets to be sent
 * @wilc: Pointer to the wilc structure.
 * @vmm_table: Pointer to the VMM table to fill.
 *
 * Fill VMM table with packets waiting to be sent.
 *
 * Return: The number of VMM entries filled in.  The table is
 *	0-terminated so the returned number is at most
 *	WILC_VMM_TBL_SIZE-1.
 */
static int fill_vmm_table(struct wilc *wilc, u32 vmm_table[WILC_VMM_TBL_SIZE])
{
	int vmm_table_len = 0;
	struct sk_buff *tqe;

	if (unlikely(wilc->chipq_bytes > 0))
		/* fill in packets that are already on the chipq: */
		skb_queue_walk(&wilc->chipq, tqe)
			vmm_table[vmm_table_len++] = vmm_table_entry(tqe);

	vmm_table_len = schedule_packets(wilc, vmm_table_len, vmm_table);
	if (vmm_table_len > 0) {
		WARN_ON(vmm_table_len >= WILC_VMM_TBL_SIZE);
		vmm_table[vmm_table_len] = 0x0;
	}
	return vmm_table_len;
}

/**
 * send_vmm_table() - send the VMM table to the chip
 * @wilc: Pointer to the wilc structure.
 * @vmm_table_len: The number of entries in the VMM table.
 * @vmm_table: The VMM table to send.
 *
 * Send the VMM table to the chip and get back the number of entries
 * that the chip can accept.
 *
 * Context: The bus must have been acquired before calling this
 * function.
 *
 * Return: The number of VMM table entries the chip can accept.
 */
static int send_vmm_table(struct wilc *wilc,
			  int vmm_table_len, const u32 *vmm_table)
{
	const struct wilc_hif_func *func;
	int ret, counter, entries, timeout;
	u32 reg;

	counter = 0;
	func = wilc->hif_func;
	do {
		ret = func->hif_read_reg(wilc, WILC_HOST_TX_CTRL, &reg);
		if (ret)
			break;

		if ((reg & WILC_HOST_TX_CTRL_BUSY) == 0) {
			ac_update_fw_ac_pkt_info(wilc, reg);
			break;
		}

		counter++;
		if (counter > 200) {
			counter = 0;
			ret = func->hif_write_reg(wilc, WILC_HOST_TX_CTRL, 0);
			break;
		}
	} while (!wilc->quit);

	if (ret)
		return ret;

	timeout = 200;
	do {
		ret = func->hif_block_tx(wilc, WILC_VMM_TBL_RX_SHADOW_BASE,
					 (u8 *)vmm_table,
					 (vmm_table_len + 1) * 4);
		if (ret)
			break;

		ret = func->hif_write_reg(wilc, WILC_HOST_VMM_CTL,
					  WILC_VMM_TABLE_UPDATED);
		if (ret)
			break;

		do {
			ret = func->hif_read_reg(wilc, WILC_HOST_VMM_CTL, &reg);
			if (ret)
				break;
			if (FIELD_GET(WILC_VMM_ENTRY_AVAILABLE, reg)) {
				entries = FIELD_GET(WILC_VMM_ENTRY_COUNT, reg);
				break;
			}
		} while (--timeout);
		if (timeout <= 0) {
			ret = func->hif_write_reg(wilc, WILC_HOST_VMM_CTL, 0x0);
			break;
		}

		if (ret)
			break;

		if (entries == 0) {
			ret = func->hif_read_reg(wilc, WILC_HOST_TX_CTRL, &reg);
			if (ret)
				break;
			reg &= ~WILC_HOST_TX_CTRL_BUSY;
			ret = func->hif_write_reg(wilc, WILC_HOST_TX_CTRL, reg);
		} else {
			ret = entries;
		}
	} while (0);
	return ret;
}

/**
 * copy_packets() - copy packets to the transmit buffer
 * @wilc: Pointer to the wilc structure.
 * @entries: The number of packets to copy from the chip queue.
 *
 * Copy a number of packets to the transmit buffer.
 *
 * Return: Number of bytes copied to the transmit buffer (always
 *	non-negative).
 */
static int copy_packets(struct wilc *wilc, int entries)
{
	u8 ac_pkt_num_to_chip[NQUEUES] = {0, 0, 0, 0};
	struct wilc_skb_tx_cb *tx_cb;
	u8 *txb = wilc->tx_buffer;
	int i;
	struct sk_buff *tqe;
	u32 offset;

	offset = 0;
	do {
		tqe = __skb_dequeue(&wilc->chipq);
		if (WARN_ON(!tqe))
			break;
		wilc->chipq_bytes -= tqe->len;

		tx_cb = WILC_SKB_TX_CB(tqe);
		ac_pkt_num_to_chip[tx_cb->q_num]++;

		memcpy(&txb[offset], tqe->data, tqe->len);
		offset += tqe->len;
		wilc_wlan_tx_packet_done(tqe, 1);
	} while (--entries);
	for (i = 0; i < NQUEUES; i++)
		wilc->fw[i].count += ac_pkt_num_to_chip[i];
	return offset;
}

/**
 * send_packets() - send the transmit buffer to the chip
 * @wilc: Pointer to the wilc structure.
 * @len: The length of the buffer containing the packets to be to the chip.
 *
 * Send the packets in the transmit buffer to the chip.
 *
 * Context: The bus must have been acquired.
 *
 * Return: Negative number on error, 0 on success.
 */
static int send_packets(struct wilc *wilc, int len)
{
	const struct wilc_hif_func *func = wilc->hif_func;
	int ret;

	ret = func->hif_clear_int_ext(wilc, ENABLE_TX_VMM);
	if (ret)
		return ret;

	return func->hif_block_tx_ext(wilc, 0, wilc->tx_buffer, len);
}

static int copy_and_send_packets(struct wilc *wilc, int entries)
{
	int len, ret;

	len = copy_packets(wilc, entries);
	if (len <= 0)
		return len;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
	ret = send_packets(wilc, len);
	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
	return ret;
}

/**
 * zero_copy_send_packets() - send packets to the chip (copy-free).
 * @wilc: Pointer to the wilc structure.
 * @entries: The number of packets to send from the VMM table.
 *
 * Zero-copy version of sending the packets in the VMM table to the
 * chip.
 *
 * Context: The wilc1000 bus must have been released but the chip
 *	must be awake.
 *
 * Return: Negative number on error, 0 on success.
 */
static int zero_copy_send_packets(struct wilc *wilc, int entries)
{
	const struct wilc_hif_func *func = wilc->hif_func;
	struct wilc_skb_tx_cb *tx_cb;
	struct sk_buff *tqe;
	int ret, i = 0;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);

	ret = func->hif_clear_int_ext(wilc, ENABLE_TX_VMM);
	if (ret == 0)
		ret = func->hif_sk_buffs_tx(wilc, 0, entries, &wilc->chipq);

	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

	for (i = 0; i < entries; ++i) {
		tqe = __skb_dequeue(&wilc->chipq);
		tx_cb = WILC_SKB_TX_CB(tqe);
		wilc->fw[tx_cb->q_num].count++;
		wilc->chipq_bytes -= tqe->len;
		wilc_wlan_tx_packet_done(tqe, ret == 0);
	}
	return ret;
}

int wilc_wlan_handle_txq(struct wilc *wilc, u32 *txq_count)
{
	int vmm_table_len, entries;
	int ret = 0;
	u32 vmm_table[WILC_VMM_TBL_SIZE];
	int srcu_idx;
	struct wilc_vif *vif;

	if (wilc->quit)
		goto out_update_cnt;

	srcu_idx = srcu_read_lock(&wilc->srcu);
	list_for_each_entry_rcu(vif, &wilc->vif_list, list)
		wilc_wlan_txq_filter_dup_tcp_ack(vif->ndev);
	srcu_read_unlock(&wilc->srcu, srcu_idx);

	vmm_table_len = fill_vmm_table(wilc, vmm_table);
	if (vmm_table_len == 0)
		goto out_update_cnt;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	entries = send_vmm_table(wilc, vmm_table_len, vmm_table);

	release_bus(wilc, (entries > 0 ?
			   WILC_BUS_RELEASE_ONLY :
			   WILC_BUS_RELEASE_ALLOW_SLEEP));

	if (entries <= 0) {
		ret = entries;
	} else {
		if (wilc->hif_func->hif_sk_buffs_tx)
			ret = zero_copy_send_packets(wilc, entries);
		else
			ret = copy_and_send_packets(wilc, entries);
	}
	if (ret >= 0 && entries < vmm_table_len)
		ret = WILC_VMM_ENTRY_FULL_RETRY;

out_update_cnt:
	*txq_count = atomic_read(&wilc->txq_entries);
	return ret;
}

static void wilc_wlan_handle_rx_buff(struct wilc *wilc, u8 *buffer, int size)
{
	int offset = 0;
	u32 header;
	u32 pkt_len, pkt_offset, tp_len;
	int is_cfg_packet;
	u8 *buff_ptr;

	do {
		buff_ptr = buffer + offset;
		header = get_unaligned_le32(buff_ptr);

		is_cfg_packet = FIELD_GET(WILC_PKT_HDR_CONFIG_FIELD, header);
		pkt_offset = FIELD_GET(WILC_PKT_HDR_OFFSET_FIELD, header);
		tp_len = FIELD_GET(WILC_PKT_HDR_TOTAL_LEN_FIELD, header);
		pkt_len = FIELD_GET(WILC_PKT_HDR_LEN_FIELD, header);

		if (pkt_len == 0 || tp_len == 0)
			break;

		if (pkt_offset & IS_MANAGMEMENT) {
			buff_ptr += HOST_HDR_OFFSET;
			wilc_wfi_mgmt_rx(wilc, buff_ptr, pkt_len);
		} else {
			if (!is_cfg_packet) {
				wilc_frmw_to_host(wilc, buff_ptr, pkt_len,
						  pkt_offset);
			} else {
				struct wilc_cfg_rsp rsp;

				buff_ptr += pkt_offset;

				wilc_wlan_cfg_indicate_rx(wilc, buff_ptr,
							  pkt_len,
							  &rsp);
				if (rsp.type == WILC_CFG_RSP) {
					if (wilc->cfg_seq_no == rsp.seq_no)
						complete(&wilc->cfg_event);
				} else if (rsp.type == WILC_CFG_RSP_STATUS) {
					wilc_mac_indicate(wilc);
				}
			}
		}
		offset += tp_len;
	} while (offset < size);
}

static void wilc_wlan_handle_rxq(struct wilc *wilc)
{
	int size;
	u8 *buffer;
	struct rxq_entry_t *rqe;

	while (!wilc->quit) {
		rqe = wilc_wlan_rxq_remove(wilc);
		if (!rqe)
			break;

		buffer = rqe->buffer;
		size = rqe->buffer_size;
		wilc_wlan_handle_rx_buff(wilc, buffer, size);

		kfree(rqe);
	}
	if (wilc->quit)
		complete(&wilc->cfg_event);
}

static void wilc_unknown_isr_ext(struct wilc *wilc)
{
	wilc->hif_func->hif_clear_int_ext(wilc, 0);
}

static void wilc_wlan_handle_isr_ext(struct wilc *wilc, u32 int_status)
{
	u32 offset = wilc->rx_buffer_offset;
	u8 *buffer = NULL;
	u32 size;
	u32 retries = 0;
	int ret = 0;
	struct rxq_entry_t *rqe;

	size = FIELD_GET(WILC_INTERRUPT_DATA_SIZE, int_status) << 2;

	while (!size && retries < 10) {
		wilc->hif_func->hif_read_size(wilc, &size);
		size = FIELD_GET(WILC_INTERRUPT_DATA_SIZE, size) << 2;
		retries++;
	}

	if (size <= 0)
		return;

	if (WILC_RX_BUFF_SIZE - offset < size)
		offset = 0;

	buffer = &wilc->rx_buffer[offset];

	wilc->hif_func->hif_clear_int_ext(wilc, DATA_INT_CLR | ENABLE_RX_VMM);
	ret = wilc->hif_func->hif_block_rx_ext(wilc, 0, buffer, size);
	if (ret)
		return;

	offset += size;
	wilc->rx_buffer_offset = offset;
	rqe = kmalloc(sizeof(*rqe), GFP_KERNEL);
	if (!rqe)
		return;

	rqe->buffer = buffer;
	rqe->buffer_size = size;
	wilc_wlan_rxq_add(wilc, rqe);
	wilc_wlan_handle_rxq(wilc);
}

void wilc_handle_isr(struct wilc *wilc)
{
	u32 int_status;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);
	wilc->hif_func->hif_read_int(wilc, &int_status);

	if (int_status & DATA_INT_EXT)
		wilc_wlan_handle_isr_ext(wilc, int_status);

	if (!(int_status & (ALL_INT_EXT)))
		wilc_unknown_isr_ext(wilc);

	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
}
EXPORT_SYMBOL_GPL(wilc_handle_isr);

int wilc_wlan_firmware_download(struct wilc *wilc, const u8 *buffer,
				u32 buffer_size)
{
	u32 offset;
	u32 addr, size, size2, blksz;
	u8 *dma_buffer;
	int ret = 0;
	u32 reg = 0;

	blksz = BIT(12);

	dma_buffer = kmalloc(blksz, GFP_KERNEL);
	if (!dma_buffer)
		return -EIO;

	offset = 0;
	pr_debug("%s: Downloading firmware size = %d\n", __func__, buffer_size);

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	reg &= ~BIT(10);
	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GLB_RESET_0, reg);
	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	if (reg & BIT(10))
		pr_err("%s: Failed to reset\n", __func__);

	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
	do {
		addr = get_unaligned_le32(&buffer[offset]);
		size = get_unaligned_le32(&buffer[offset + 4]);
		acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);
		offset += 8;
		while (((int)size) && (offset < buffer_size)) {
			if (size <= blksz)
				size2 = size;
			else
				size2 = blksz;

			memcpy(dma_buffer, &buffer[offset], size2);
			ret = wilc->hif_func->hif_block_tx(wilc, addr,
							   dma_buffer, size2);
			if (ret)
				break;

			addr += size2;
			offset += size2;
			size -= size2;
		}
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

		if (ret) {
			pr_err("%s Bus error\n", __func__);
			goto fail;
		}
		pr_debug("%s Offset = %d\n", __func__, offset);
	} while (offset < buffer_size);

fail:

	kfree(dma_buffer);

	return ret;
}

int wilc_wlan_start(struct wilc *wilc)
{
	u32 reg = 0;
	int ret;
	u32 chipid;

	if (wilc->io_type == WILC_HIF_SDIO) {
		reg = 0;
		reg |= BIT(3);
	} else if (wilc->io_type == WILC_HIF_SPI) {
		reg = 1;
	}
	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
	ret = wilc->hif_func->hif_write_reg(wilc, WILC_VMM_CORE_CFG, reg);
	if (ret)
		goto release;

	reg = 0;
	if (wilc->io_type == WILC_HIF_SDIO && wilc->dev_irq_num)
		reg |= WILC_HAVE_SDIO_IRQ_GPIO;

	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GP_REG_1, reg);
	if (ret)
		goto release;

	wilc->hif_func->hif_sync_ext(wilc, NUM_INT_EXT);

	ret = wilc->hif_func->hif_read_reg(wilc, WILC_CHIPID, &chipid);
	if (ret)
		goto release;

	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	if ((reg & BIT(10)) == BIT(10)) {
		reg &= ~BIT(10);
		wilc->hif_func->hif_write_reg(wilc, WILC_GLB_RESET_0, reg);
		wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);
	}

	reg |= BIT(10);
	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GLB_RESET_0, reg);
	wilc->hif_func->hif_read_reg(wilc, WILC_GLB_RESET_0, &reg);

release:
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);
	return ret;
}

int wilc_wlan_stop(struct wilc *wilc, struct wilc_vif *vif)
{
	u32 reg = 0;
	int ret;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	ret = wilc->hif_func->hif_read_reg(wilc, WILC_GP_REG_0, &reg);
	if (ret) {
		netdev_err(vif->ndev, "Error while reading reg\n");
		goto release;
	}

	ret = wilc->hif_func->hif_write_reg(wilc, WILC_GP_REG_0,
					(reg | WILC_ABORT_REQ_BIT));
	if (ret) {
		netdev_err(vif->ndev, "Error while writing reg\n");
		goto release;
	}

	ret = wilc->hif_func->hif_read_reg(wilc, WILC_FW_HOST_COMM, &reg);
	if (ret) {
		netdev_err(vif->ndev, "Error while reading reg\n");
		goto release;
	}
	reg = BIT(0);

	ret = wilc->hif_func->hif_write_reg(wilc, WILC_FW_HOST_COMM, reg);
	if (ret) {
		netdev_err(vif->ndev, "Error while writing reg\n");
		goto release;
	}

	ret = 0;
release:
	/* host comm is disabled - we can't issue sleep command anymore: */
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);

	return ret;
}

void wilc_wlan_cleanup(struct net_device *dev)
{
	struct sk_buff *tqe, *cfg_skb;
	struct rxq_entry_t *rqe;
	u8 ac;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	wilc->quit = 1;

	while ((tqe = __skb_dequeue(&wilc->chipq)))
		wilc_wlan_tx_packet_done(tqe, 0);
	wilc->chipq_bytes = 0;

	for (ac = 0; ac < NQUEUES; ac++) {
		while ((tqe = skb_dequeue(&wilc->txq[ac])))
			wilc_wlan_tx_packet_done(tqe, 0);
	}
	atomic_set(&wilc->txq_entries, 0);
	cfg_skb = wilc->cfg_skb;
	if (cfg_skb) {
		wilc->cfg_skb = NULL;
		dev_kfree_skb_any(cfg_skb);
	}

	while ((rqe = wilc_wlan_rxq_remove(wilc)))
		kfree(rqe);

	kfree(wilc->rx_buffer);
	wilc->rx_buffer = NULL;
	kfree(wilc->tx_buffer);
	wilc->tx_buffer = NULL;
	wilc->hif_func->hif_deinit(wilc);
}

struct sk_buff *wilc_wlan_alloc_skb(struct wilc_vif *vif, size_t len)
{
	size_t size, headroom;
	struct sk_buff *skb;

	headroom = vif->ndev->needed_headroom;
	size = headroom + len + vif->ndev->needed_tailroom;
	skb = netdev_alloc_skb(vif->ndev, size);
	if (!skb) {
		netdev_err(vif->ndev, "Failed to alloc skb");
		return NULL;
	}
	skb_reserve(skb, headroom);
	return skb;
}

static struct sk_buff *alloc_cfg_skb(struct wilc_vif *vif)
{
	struct sk_buff *skb;

	skb = wilc_wlan_alloc_skb(vif, (sizeof(struct wilc_cfg_cmd_hdr)
					+ WILC_MAX_CFG_FRAME_SIZE));
	if (!skb)
		return NULL;
	skb_reserve(skb, sizeof(struct wilc_cfg_cmd_hdr));
	return skb;
}

static int wilc_wlan_cfg_commit(struct wilc_vif *vif, int type,
				u32 drv_handler)
{
	struct wilc *wilc = vif->wilc;
	struct wilc_cfg_cmd_hdr *hdr;
	struct sk_buff *cfg_skb = wilc->cfg_skb;

	hdr = skb_push(cfg_skb, sizeof(*hdr));
	hdr->cmd_type = (type == WILC_CFG_SET) ? 'W' : 'Q';
	hdr->seq_no = wilc->cfg_seq_no;
	hdr->total_len = cpu_to_le16(cfg_skb->len);
	hdr->driver_handler = cpu_to_le32(drv_handler);
	/* We are about to pass ownership of cfg_skb to the tx queue
	 * (or it'll be destroyed, in case the queue is full):
	 */
	wilc->cfg_skb = NULL;

	if (!wilc_wlan_txq_add_cfg_pkt(vif, cfg_skb))
		return -1;

	return 0;
}

/**
 * wilc_wlan_cfg_apply_wid() - Add a config set or get (query).
 * @vif: The virtual interface to which the set/get applies.
 * @start: Should be 1 if a new config packet should be initialized,
 *	0 otherwise.
 * @wid: The WID to use.
 * @buffer: For a set, the bytes to include in the request,
 *	for a get, the buffer in which to return the result.
 * @buffer_size: The size of the buffer in bytes.
 * @commit: Should be 1 if the config packet should be sent after
 *	adding this request/query.
 * @drv_handler: An opaque cookie that will be sent in the config header.
 * @set: Should be true if a set, false for get.
 *
 * Add a WID set/query to the current config packet and optionally
 * submit the resulting packet to the chip and wait for its reply.
 *
 * Return: Zero on failure, positive number on success.
 */
static int wilc_wlan_cfg_apply_wid(struct wilc_vif *vif, int start, u16 wid,
				   u8 *buffer, u32 buffer_size, int commit,
				   u32 drv_handler, bool set)
{
	int ret_size;
	struct wilc *wilc = vif->wilc;

	mutex_lock(&wilc->cfg_cmd_lock);

	if (start) {
		WARN_ON(wilc->cfg_skb);
		wilc->cfg_skb = alloc_cfg_skb(vif);
		if (!wilc->cfg_skb) {
			netdev_dbg(vif->ndev, "Failed to alloc cfg_skb");
			mutex_unlock(&wilc->cfg_cmd_lock);
			return 0;
		}
	}

	if (set)
		ret_size = wilc_wlan_cfg_set_wid(skb_tail_pointer(wilc->cfg_skb), 0,
						 wid, buffer, buffer_size);
	else
		ret_size = wilc_wlan_cfg_get_wid(skb_tail_pointer(wilc->cfg_skb), 0, wid);
	if (ret_size == 0)
		netdev_dbg(vif->ndev,
			   "Failed to add WID 0x%x to %s cfg packet\n",
			   wid, set ? "set" : "query");

	skb_put(wilc->cfg_skb, ret_size);

	if (!commit) {
		mutex_unlock(&wilc->cfg_cmd_lock);
		return ret_size;
	}

	netdev_dbg(vif->ndev, "%s: %s seqno[%d]\n",
		   __func__, set ? "set" : "get", wilc->cfg_seq_no);

	if (wilc_wlan_cfg_commit(vif, set ? WILC_CFG_SET : WILC_CFG_QUERY,
				 drv_handler))
		ret_size = 0;

	if (!wait_for_completion_timeout(&wilc->cfg_event,
					 WILC_CFG_PKTS_TIMEOUT)) {
		netdev_dbg(vif->ndev, "%s: Timed Out\n", __func__);
		ret_size = 0;
	}

	wilc->cfg_seq_no = (wilc->cfg_seq_no + 1) % 256;
	mutex_unlock(&wilc->cfg_cmd_lock);

	return ret_size;
}

int wilc_wlan_cfg_set(struct wilc_vif *vif, int start, u16 wid, u8 *buffer,
		      u32 buffer_size, int commit, u32 drv_handler)
{
	return wilc_wlan_cfg_apply_wid(vif, start, wid, buffer, buffer_size,
				       commit, drv_handler, true);
}

int wilc_wlan_cfg_get(struct wilc_vif *vif, int start, u16 wid, int commit,
		      u32 drv_handler)
{
	return wilc_wlan_cfg_apply_wid(vif, start, wid, NULL, 0,
				       commit, drv_handler, false);
}

int wilc_send_config_pkt(struct wilc_vif *vif, u8 mode, struct wid *wids,
			 u32 count)
{
	int i;
	int ret = 0;
	u32 drv = wilc_get_vif_idx(vif);

	if (mode == WILC_GET_CFG) {
		for (i = 0; i < count; i++) {
			if (!wilc_wlan_cfg_get(vif, !i,
					       wids[i].id,
					       (i == count - 1),
					       drv)) {
				ret = -ETIMEDOUT;
				break;
			}
		}
		for (i = 0; i < count; i++) {
			wids[i].size = wilc_wlan_cfg_get_val(vif->wilc,
							     wids[i].id,
							     wids[i].val,
							     wids[i].size);
		}
	} else if (mode == WILC_SET_CFG) {
		for (i = 0; i < count; i++) {
			if (!wilc_wlan_cfg_set(vif, !i,
					       wids[i].id,
					       wids[i].val,
					       wids[i].size,
					       (i == count - 1),
					       drv)) {
				ret = -ETIMEDOUT;
				break;
			}
		}
	}

	return ret;
}

static int init_chip(struct net_device *dev)
{
	u32 chipid;
	u32 reg;
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc = vif->wilc;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);

	chipid = wilc_get_chipid(wilc, true);

	if ((chipid & 0xfff) != 0xa0) {
		ret = wilc->hif_func->hif_read_reg(wilc,
						   WILC_CORTUS_RESET_MUX_SEL,
						   &reg);
		if (ret) {
			netdev_err(dev, "fail read reg 0x1118\n");
			goto release;
		}
		reg |= BIT(0);
		ret = wilc->hif_func->hif_write_reg(wilc,
						    WILC_CORTUS_RESET_MUX_SEL,
						    reg);
		if (ret) {
			netdev_err(dev, "fail write reg 0x1118\n");
			goto release;
		}
		ret = wilc->hif_func->hif_write_reg(wilc,
						    WILC_CORTUS_BOOT_REGISTER,
						    WILC_CORTUS_BOOT_FROM_IRAM);
		if (ret) {
			netdev_err(dev, "fail write reg 0xc0000\n");
			goto release;
		}
	}

release:
	release_bus(wilc, WILC_BUS_RELEASE_ONLY);

	return ret;
}

u32 wilc_get_chipid(struct wilc *wilc, bool update)
{
	u32 chipid = 0;
	u32 rfrevid = 0;

	if (wilc->chipid == 0 || update) {
		wilc->hif_func->hif_read_reg(wilc, WILC_CHIPID, &chipid);
		wilc->hif_func->hif_read_reg(wilc, WILC_RF_REVISION_ID,
					     &rfrevid);
		if (!is_wilc1000(chipid)) {
			wilc->chipid = 0;
			return wilc->chipid;
		}
		if (chipid == WILC_1000_BASE_ID_2A) { /* 0x1002A0 */
			if (rfrevid != 0x1)
				chipid = WILC_1000_BASE_ID_2A_REV1;
		} else if (chipid == WILC_1000_BASE_ID_2B) { /* 0x1002B0 */
			if (rfrevid == 0x4)
				chipid = WILC_1000_BASE_ID_2B_REV1;
			else if (rfrevid != 0x3)
				chipid = WILC_1000_BASE_ID_2B_REV2;
		}

		wilc->chipid = chipid;
	}
	return wilc->chipid;
}

int wilc_wlan_init(struct net_device *dev)
{
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wilc;

	wilc = vif->wilc;

	wilc->quit = 0;

	if (wilc->hif_func->hif_init(wilc, false)) {
		ret = -EIO;
		goto fail;
	}

	init_q_limits(wilc);

	if (!wilc->tx_buffer)
		wilc->tx_buffer = kmalloc(WILC_TX_BUFF_SIZE, GFP_KERNEL);

	if (!wilc->tx_buffer) {
		ret = -ENOBUFS;
		goto fail;
	}

	if (!wilc->rx_buffer)
		wilc->rx_buffer = kmalloc(WILC_RX_BUFF_SIZE, GFP_KERNEL);

	if (!wilc->rx_buffer) {
		ret = -ENOBUFS;
		goto fail;
	}

	if (init_chip(dev)) {
		ret = -EIO;
		goto fail;
	}

	return 0;

fail:

	kfree(wilc->rx_buffer);
	wilc->rx_buffer = NULL;
	kfree(wilc->tx_buffer);
	wilc->tx_buffer = NULL;

	return ret;
}
