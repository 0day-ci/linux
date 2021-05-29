/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2021 Broadcom Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/timecounter.h>
#include <linux/timekeeping.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_ptp.h"

static int bnxt_ptp_settime(struct ptp_clock_info *ptp_info,
			    const struct timespec64 *ts)
{
	struct bnxt_ptp_cfg *ptp = container_of(ptp_info, struct bnxt_ptp_cfg,
						ptp_info);
	u64 ns = timespec64_to_ns(ts);

	timecounter_init(&ptp->tc, &ptp->cc, ns);
	return 0;
}

static int bnxt_hwrm_port_ts_query(struct bnxt *bp, u32 flags, u64 *ts,
				   struct ptp_system_timestamp *sts)
{
	struct hwrm_port_ts_query_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_port_ts_query_input req = {0};
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_TS_QUERY, -1, -1);
	req.flags = cpu_to_le32(flags);
	if ((flags & PORT_TS_QUERY_REQ_FLAGS_PATH) ==
	    PORT_TS_QUERY_REQ_FLAGS_PATH_TX) {
		req.enables = cpu_to_le16(BNXT_PTP_QTS_TX_ENABLES);
		req.ptp_seq_id = cpu_to_le32(bp->ptp_cfg->tx_seqid);
		req.ts_req_timeout = cpu_to_le16(BNXT_PTP_QTS_TIMEOUT);
	}
	mutex_lock(&bp->hwrm_cmd_lock);
	ptp_read_system_prets(sts);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	ptp_read_system_postts(sts);
	if (!rc)
		*ts = le64_to_cpu(resp->ptp_msg_ts);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_ptp_gettimex(struct ptp_clock_info *ptp_info,
			     struct timespec64 *ts,
			     struct ptp_system_timestamp *sts)
{
	struct bnxt_ptp_cfg *ptp = container_of(ptp_info, struct bnxt_ptp_cfg,
						ptp_info);
	u32 flags = PORT_TS_QUERY_REQ_FLAGS_CURRENT_TIME;
	u64 ns, cycles;
	int rc;

	rc = bnxt_hwrm_port_ts_query(ptp->bp, flags, &cycles, sts);
	if (rc)
		return rc;

	ns = timecounter_cyc2time(&ptp->tc, cycles);
	*ts = ns_to_timespec64(ns);

	return 0;
}

static int bnxt_ptp_adjtime(struct ptp_clock_info *ptp_info, s64 delta)
{
	struct bnxt_ptp_cfg *ptp = container_of(ptp_info, struct bnxt_ptp_cfg,
						ptp_info);

	timecounter_adjtime(&ptp->tc, delta);
	return 0;
}

static int bnxt_ptp_adjfreq(struct ptp_clock_info *ptp_info, s32 ppb)
{
	struct bnxt_ptp_cfg *ptp = container_of(ptp_info, struct bnxt_ptp_cfg,
						ptp_info);
	struct hwrm_port_mac_cfg_input req = {0};
	struct bnxt *bp = ptp->bp;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_MAC_CFG, -1, -1);
	req.ptp_freq_adj_ppb = ppb;
	req.enables = cpu_to_le32(PORT_MAC_CFG_REQ_ENABLES_PTP_FREQ_ADJ_PPB);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		netdev_err(ptp->bp->dev,
			   "ptp adjfreq failed. rc = %d\n", rc);
	return rc;
}

static int bnxt_ptp_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static int bnxt_hwrm_ptp_cfg(struct bnxt *bp)
{
	struct hwrm_port_mac_cfg_input req = {0};
	struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;
	u32 flags = 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_PORT_MAC_CFG, -1, -1);
	if (ptp->rx_filter)
		flags |= PORT_MAC_CFG_REQ_FLAGS_PTP_RX_TS_CAPTURE_ENABLE;
	else
		flags |= PORT_MAC_CFG_REQ_FLAGS_PTP_RX_TS_CAPTURE_DISABLE;
	if (ptp->tx_tstamp_en)
		flags |= PORT_MAC_CFG_REQ_FLAGS_PTP_TX_TS_CAPTURE_ENABLE;
	else
		flags |= PORT_MAC_CFG_REQ_FLAGS_PTP_TX_TS_CAPTURE_DISABLE;
	req.flags = cpu_to_le32(flags);
	req.enables = cpu_to_le32(PORT_MAC_CFG_REQ_ENABLES_RX_TS_CAPTURE_PTP_MSG_TYPE);
	req.rx_ts_capture_ptp_msg_type = cpu_to_le16(ptp->rxctl);

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

int bnxt_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwtstamp_config stmpconf;
	struct bnxt_ptp_cfg *ptp;
	u16 old_rxctl;
	int old_rx_filter, rc;
	u8 old_tx_tstamp_en;

	ptp = bp->ptp_cfg;
	if (!ptp)
		return -EOPNOTSUPP;

	if (copy_from_user(&stmpconf, ifr->ifr_data, sizeof(stmpconf)))
		return -EFAULT;

	if (stmpconf.flags)
		return -EINVAL;

	if (stmpconf.tx_type != HWTSTAMP_TX_ON &&
	    stmpconf.tx_type != HWTSTAMP_TX_OFF)
		return -ERANGE;

	old_rx_filter = ptp->rx_filter;
	old_rxctl = ptp->rxctl;
	old_tx_tstamp_en = ptp->tx_tstamp_en;
	switch (stmpconf.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		ptp->rxctl = 0;
		ptp->rx_filter = HWTSTAMP_FILTER_NONE;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		ptp->rxctl = BNXT_PTP_MSG_EVENTS;
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		ptp->rxctl = BNXT_PTP_MSG_SYNC;
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_SYNC;
		break;
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		ptp->rxctl = BNXT_PTP_MSG_DELAY_REQ;
		ptp->rx_filter = HWTSTAMP_FILTER_PTP_V2_DELAY_REQ;
		break;
	default:
		return -ERANGE;
	}

	if (stmpconf.tx_type == HWTSTAMP_TX_ON)
		ptp->tx_tstamp_en = 1;
	else
		ptp->tx_tstamp_en = 0;

	rc = bnxt_hwrm_ptp_cfg(bp);
	if (rc)
		goto ts_set_err;

	stmpconf.rx_filter = ptp->rx_filter;
	return copy_to_user(ifr->ifr_data, &stmpconf, sizeof(stmpconf)) ?
		-EFAULT : 0;

ts_set_err:
	ptp->rx_filter = old_rx_filter;
	ptp->rxctl = old_rxctl;
	ptp->tx_tstamp_en = old_tx_tstamp_en;
	return rc;
}

int bnxt_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	struct bnxt *bp = netdev_priv(dev);
	struct hwtstamp_config stmpconf;
	struct bnxt_ptp_cfg *ptp;

	ptp = bp->ptp_cfg;
	if (!ptp)
		return -EOPNOTSUPP;

	stmpconf.flags = 0;
	stmpconf.tx_type = ptp->tx_tstamp_en ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;

	stmpconf.rx_filter = ptp->rx_filter;
	return copy_to_user(ifr->ifr_data, &stmpconf, sizeof(stmpconf)) ?
		-EFAULT : 0;
}

static u64 bnxt_cc_read(const struct cyclecounter *cc)
{
	struct bnxt_ptp_cfg *ptp = container_of(cc, struct bnxt_ptp_cfg, cc);
	u32 flags = PORT_TS_QUERY_REQ_FLAGS_CURRENT_TIME;
	struct bnxt *bp = ptp->bp;
	int rc;
	u64 ns;

	rc = bnxt_hwrm_port_ts_query(bp, flags, &ns, NULL);
	if (rc)
		netdev_err(bp->dev, "TS query for cc_read failed rc = %x\n",
			   rc);
	return ns;
}

static const struct ptp_clock_info bnxt_ptp_caps = {
	.owner		= THIS_MODULE,
	.name		= "bnxt clock",
	.max_adj	= BNXT_MAX_PHC_DRIFT,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= bnxt_ptp_adjfreq,
	.adjtime	= bnxt_ptp_adjtime,
	.gettimex64	= bnxt_ptp_gettimex,
	.settime64	= bnxt_ptp_settime,
	.enable		= bnxt_ptp_enable,
};

int bnxt_ptp_init(struct bnxt *bp)
{
	struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;

	if (!ptp)
		return 0;

	atomic_set(&ptp->tx_avail, BNXT_MAX_TX_TS);

	memset(&ptp->cc, 0, sizeof(ptp->cc));
	ptp->cc.read = bnxt_cc_read;
	ptp->cc.mask = CYCLECOUNTER_MASK(64);
	ptp->cc.shift = 0;
	ptp->cc.mult = 1;

	timecounter_init(&ptp->tc, &ptp->cc, ktime_to_ns(ktime_get_real()));

	ptp->ptp_info = bnxt_ptp_caps;
	ptp->ptp_clock = ptp_clock_register(&ptp->ptp_info, &bp->pdev->dev);
	if (IS_ERR(ptp->ptp_clock))
		ptp->ptp_clock = NULL;

	return 0;
}

void bnxt_ptp_clear(struct bnxt *bp)
{
	struct bnxt_ptp_cfg *ptp = bp->ptp_cfg;

	if (!ptp)
		return;

	if (ptp->ptp_clock)
		ptp_clock_unregister(ptp->ptp_clock);

	ptp->ptp_clock = NULL;
}
