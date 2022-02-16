/* SPDX-License-Identifier: ISC */
/*
 * Qualcomm Atheros OUI and vendor specific assignments
 * Copyright (c) 2014-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2020, The Linux Foundation
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights
 */

#ifndef _UAPI_NL80211_VND_QCA_H
#define _UAPI_NL80211_VND_QCA_H


/* Vendor id to be used in vendor specific command and events to user space
 * NOTE: The authoritative place for definition of QCA_NL80211_VENDOR_ID,
 * vendor subcmd definitions prefixed with QCA_NL80211_VENDOR_SUBCMD, and
 * qca_wlan_vendor_attr is open source file src/common/qca-vendor.h in
 * git://w1.fi/srv/git/hostap.git; the values here are just a copy of that
 */
#define OUI_QCA 0x001374

/**
 * enum qca_nl80211_vendor_subcmds - QCA nl80211 vendor command identifiers
 * @QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG: This command is used to
 * configure parameters per peer to capture Channel Frequency Response
 * (CFR) and enable Periodic CFR capture. The attributes for this command
 * are defined in enum qca_wlan_vendor_peer_cfr_capture_attr. This command
 * can also be used to send CFR data from the driver to userspace when
 * netlink events are used to send CFR data.
 *
 */
enum qca_nl80211_vendor_subcmds {
	QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG = 173,
};

/**
 * enum qca_wlan_vendor_cfr_method - QCA vendor CFR methods used by
 * attribute QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD as part of vendor
 * command QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG.
 * @QCA_WLAN_VENDOR_CFR_METHOD_QOS_NULL: CFR method using QoS Null frame
 * @QCA_WLAN_VENDOR_CFR_QOS_NULL_WITH_PHASE: CFR method using QoS Null frame
 * with phase
 * @QCA_WLAN_VENDOR_CFR_PROBE_RESPONSE: CFR method using Probe Response frame
 */
enum qca_wlan_vendor_cfr_method {
	QCA_WLAN_VENDOR_CFR_METHOD_QOS_NULL = 0,
	QCA_WLAN_VENDOR_CFR_QOS_NULL_WITH_PHASE = 1,
	QCA_WLAN_VENDOR_CFR_PROBE_RESPONSE = 2,
};

/**
 * enum qca_wlan_vendor_peer_cfr_capture_attr - Used by the vendor command
 * QCA_NL80211_VENDOR_SUBCMD_PEER_CFR_CAPTURE_CFG to configure peer
 * Channel Frequency Response capture parameters and enable periodic CFR
 * capture.
 *
 * @QCA_WLAN_VENDOR_ATTR_CFR_PEER_MAC_ADDR: Optional (6-byte MAC address)
 * MAC address of peer. This is for CFR version 1 only.
 *
 * @QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE: Required (flag)
 * Enable peer CFR capture. This attribute is mandatory to enable peer CFR
 * capture. If this attribute is not present, peer CFR capture is disabled.
 *
 * @QCA_WLAN_VENDOR_ATTR_PEER_CFR_BANDWIDTH: Optional (u8)
 * BW of measurement, attribute uses the values in enum nl80211_chan_width
 * Supported values: 20, 40, 80, 80+80, 160.
 * Note that all targets may not support all bandwidths.
 * This attribute is mandatory for version 1 if attribute
 * QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE is used.
 *
 * @QCA_WLAN_VENDOR_ATTR_PEER_CFR_PERIODICITY: Optional (u32)
 * Periodicity of CFR measurement in milliseconds.
 * Periodicity should be a multiple of Base timer.
 * Current Base timer value supported is 10 milliseconds (default).
 * 0 for one shot capture.
 * This attribute is mandatory for version 1 if attribute
 * QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE is used.
 *
 * @QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD: Optional (u8)
 * Method used to capture Channel Frequency Response.
 * Attribute uses the values defined in enum qca_wlan_vendor_cfr_method.
 * This attribute is mandatory for version 1 if attribute
 * QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE is used.
 *
 * @QCA_WLAN_VENDOR_ATTR_PERIODIC_CFR_CAPTURE_ENABLE: Optional (flag)
 * Enable periodic CFR capture.
 * This attribute is mandatory for version 1 to enable Periodic CFR capture.
 * If this attribute is not present, periodic CFR capture is disabled.
 */
enum qca_wlan_vendor_peer_cfr_capture_attr {
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_CAPTURE_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_CFR_PEER_MAC_ADDR = 1,
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_ENABLE = 2,
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_BANDWIDTH = 3,
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_PERIODICITY = 4,
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_METHOD = 5,
	QCA_WLAN_VENDOR_ATTR_PERIODIC_CFR_CAPTURE_ENABLE = 6,

	/* Keep last */
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_AFTER_LAST,
	QCA_WLAN_VENDOR_ATTR_PEER_CFR_MAX =
		QCA_WLAN_VENDOR_ATTR_PEER_CFR_AFTER_LAST - 1,
};

#endif /* QCA_VENDOR_H */
