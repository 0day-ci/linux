// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights
 */

#include <net/netlink.h>
#include <net/mac80211.h>
#include <uapi/linux/nl80211-vnd-qca.h>
#include "core.h"
#include "debug.h"
#include "peer.h"

static const struct nla_policy
ath11k_vendor_cfr_config_policy[QCA_WLAN_VENDOR_ATTR_PEER_CFR_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_CFR_PEER_MAC_ADDR] = NLA_POLICY_ETH_ADDR,
	[QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE] = { .type = NLA_FLAG },
	[QCA_WLAN_VENDOR_ATTR_PEER_CFR_BANDWIDTH] =
		NLA_POLICY_RANGE(NLA_U8, 0, NL80211_CHAN_WIDTH_80),
	[QCA_WLAN_VENDOR_ATTR_PEER_CFR_PERIODICITY] = { .type = NLA_U32},
		 NLA_POLICY_MIN(NLA_U32, 1),
	[QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD] =
		NLA_POLICY_RANGE(NLA_U8, 0, QCA_WLAN_VENDOR_CFR_PROBE_RESPONSE),
	[QCA_WLAN_VENDOR_ATTR_PERIODIC_CFR_CAPTURE_ENABLE] = { .type = NLA_FLAG },
};

static enum ath11k_cfr_capture_bw
vendor_cfr_bw_to_ath11k_cfr_bw(enum nl80211_chan_width bw)
{
	switch (bw) {
	case NL80211_CHAN_WIDTH_20:
		return ATH11K_CFR_CAPTURE_BW_20;
	case NL80211_CHAN_WIDTH_40:
		return ATH11K_CFR_CAPTURE_BW_40;
	case NL80211_CHAN_WIDTH_80:
		return ATH11K_CFR_CAPTURE_BW_80;
	default:
		return ATH11K_CFR_CAPTURE_BW_MAX;
	}
}

static enum ath11k_cfr_capture_method
vendor_cfr_method_to_ath11k_cfr_method(enum qca_wlan_vendor_cfr_method method)
{
	switch (method) {
	case QCA_WLAN_VENDOR_CFR_METHOD_QOS_NULL:
		return ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME;
	case QCA_WLAN_VENDOR_CFR_QOS_NULL_WITH_PHASE:
		return ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME_WITH_PHASE;
	case QCA_WLAN_VENDOR_CFR_PROBE_RESPONSE:
		return ATH11K_CFR_CAPTURE_METHOD_PROBE_RESP;
	default:
		return ATH11K_CFR_CAPTURE_METHOD_MAX;
	}
}

static int ath11k_vendor_parse_cfr_config(struct wiphy *wihpy,
					  struct wireless_dev *wdev,
					  const void *data,
					  int data_len)
{
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_MAX + 1];
	struct ieee80211_vif *vif;
	struct ath11k_vif *arvif;
	struct ath11k *ar;
	struct ath11k_peer *peer;
	struct ath11k_sta *arsta = NULL;
	struct ieee80211_sta *sta = NULL;
	struct ath11k_per_peer_cfr_capture params;
	enum qca_wlan_vendor_cfr_method method = QCA_WLAN_VENDOR_CFR_METHOD_QOS_NULL;
	enum nl80211_chan_width bw = NL80211_CHAN_WIDTH_20;
	enum ath11k_cfr_capture_method cfr_method;
	enum ath11k_cfr_capture_bw cfr_bw;
	u8 *mac_addr;
	u32 periodicity = 0;
	bool enable_cfr;
	bool unassoc_peer = false;
	int ret = 0;

	if (!wdev)
		return -EINVAL;

	vif = wdev_to_ieee80211_vif(wdev);
	if (!vif)
		return -EINVAL;

	arvif = (struct ath11k_vif *)vif->drv_priv;
	ar = arvif->ar;

	mutex_lock(&ar->conf_mutex);

	ret = nla_parse(tb, QCA_WLAN_VENDOR_ATTR_PEER_CFR_MAX, data, data_len,
			ath11k_vendor_cfr_config_policy, NULL);
	if (ret) {
		ath11k_warn(ar->ab, "invalid cfr config policy attribute\n");
		goto exit;
	}

	/* MAC address is mandatory to enable/disable cfr capture*/
	if (!tb[QCA_WLAN_VENDOR_ATTR_CFR_PEER_MAC_ADDR]) {
		ret = -EINVAL;
		goto exit;
	}

	enable_cfr = nla_get_flag(tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE]);
	mac_addr = nla_data(tb[QCA_WLAN_VENDOR_ATTR_CFR_PEER_MAC_ADDR]);

	if (enable_cfr &&
	    (!tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_BANDWIDTH] ||
	     !tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD] ||
	     !tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_PERIODICITY])) {
		ret = -EINVAL;
		goto exit;
	}

	if (enable_cfr) {
		periodicity = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_PERIODICITY]);
		bw = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_BANDWIDTH]);
		method = nla_get_u8(tb[QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD]);
	}

	if (periodicity > WMI_PEER_CFR_PERIODICITY_MAX) {
		ath11k_warn(ar->ab, "Invalid periodicity %u max supported %u\n",
			    periodicity, WMI_PEER_CFR_PERIODICITY_MAX);
		ret = -EINVAL;
		goto exit;
	}

	cfr_bw = vendor_cfr_bw_to_ath11k_cfr_bw(bw);
	if (cfr_bw >= ATH11K_CFR_CAPTURE_BW_MAX) {
		ath11k_warn(ar->ab, "Driver doesn't support configured bw %d\n", bw);
		ret = -EINVAL;
		goto exit;
	}

	cfr_method = vendor_cfr_method_to_ath11k_cfr_method(method);
	if (cfr_method >= ATH11K_CFR_CAPTURE_METHOD_MAX) {
		ath11k_warn(ar->ab, "Driver doesn't support configured method %d\n",
			    method);
		ret = -EINVAL;
		goto exit;
	}

	spin_lock_bh(&ar->ab->base_lock);
	peer = ath11k_peer_find_by_addr(ar->ab, mac_addr);
	if (!peer || !peer->sta) {
		unassoc_peer = true;
	} else {
		sta = peer->sta;
		arsta = (struct ath11k_sta *)sta->drv_priv;
	}
	spin_unlock_bh(&ar->ab->base_lock);

	if (unassoc_peer && cfr_method != ATH11K_CFR_CAPTURE_METHOD_PROBE_RESP) {
		ath11k_warn(ar->ab, "invalid capture method for an unassoc sta");
		ret = -EINVAL;
		goto exit;
	}

	params.cfr_enable = enable_cfr;
	params.cfr_period = periodicity;
	params.cfr_bw = cfr_bw;
	params.cfr_method = cfr_method;

	if (unassoc_peer)
		ath11k_cfr_update_unassoc_pool(ar, &params, mac_addr);
	else
		ret = ath11k_cfr_send_peer_cfr_capture_cmd(ar, arsta,
							   &params, mac_addr);
exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static struct wiphy_vendor_command ath11k_vendor_commands[] = {
	{
		.info.vendor_id = OUI_QCA,
		.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG,
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = ath11k_vendor_parse_cfr_config,
		.policy = ath11k_vendor_cfr_config_policy,
		.maxattr = QCA_WLAN_VENDOR_ATTR_PEER_CFR_MAX
	}
};

int ath11k_vendor_register(struct ath11k *ar)
{
	ar->hw->wiphy->vendor_commands = ath11k_vendor_commands;
	ar->hw->wiphy->n_vendor_commands = ARRAY_SIZE(ath11k_vendor_commands);

	return 0;
}
