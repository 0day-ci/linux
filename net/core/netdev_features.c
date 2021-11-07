// SPDX-License-Identifier: GPL-2.0
/*
 * Network device features.
 */

#include <netdev_features.h>

netdev_features_t netdev_ethtool_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_ethtool_features);

netdev_features_t netdev_never_change_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_never_change_features);

netdev_features_t netdev_gso_mask_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_gso_mask_features);

netdev_features_t netdev_ip_csum_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_ip_csum_features);

netdev_features_t netdev_csum_mask_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_csum_mask_features);

netdev_features_t netdev_all_tso_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_all_tso_features);

netdev_features_t netdev_tso_ecn_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_tso_ecn_features);

netdev_features_t netdev_all_fcoe_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_all_fcoe_features);

netdev_features_t netdev_gso_software_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_gso_software_features);

netdev_features_t netdev_one_for_all_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_one_for_all_features);

netdev_features_t netdev_all_for_all_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_all_for_all_features);

netdev_features_t netdev_upper_disable_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_upper_disable_features);

netdev_features_t netdev_soft_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_soft_features);

netdev_features_t netdev_soft_off_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_soft_off_features);

netdev_features_t netdev_vlan_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_vlan_features);

netdev_features_t netdev_tx_vlan_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_tx_vlan_features);

netdev_features_t netdev_gso_encap_all_features __ro_after_init;
EXPORT_SYMBOL_GPL(netdev_gso_encap_all_features);

const u8 netif_f_never_change_array[] = {
	NETIF_F_VLAN_CHALLENGED_BIT,
	NETIF_F_LLTX_BIT,
	NETIF_F_NETNS_LOCAL_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_never_change_array);

const u8 netif_f_gso_mask_array[] = {
	NETIF_F_TSO_BIT,
	NETIF_F_GSO_ROBUST_BIT,
	NETIF_F_TSO_ECN_BIT,
	NETIF_F_TSO_MANGLEID_BIT,
	NETIF_F_TSO6_BIT,
	NETIF_F_FSO_BIT,
	NETIF_F_GSO_GRE_BIT,
	NETIF_F_GSO_GRE_CSUM_BIT,
	NETIF_F_GSO_IPXIP4_BIT,
	NETIF_F_GSO_IPXIP6_BIT,
	NETIF_F_GSO_UDP_TUNNEL_BIT,
	NETIF_F_GSO_UDP_TUNNEL_CSUM_BIT,
	NETIF_F_GSO_PARTIAL_BIT,
	NETIF_F_GSO_TUNNEL_REMCSUM_BIT,
	NETIF_F_GSO_SCTP_BIT,
	NETIF_F_GSO_ESP_BIT,
	NETIF_F_GSO_UDP_BIT,
	NETIF_F_GSO_UDP_L4_BIT,
	NETIF_F_GSO_FRAGLIST_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_gso_mask_array);

const u8 netif_f_ip_csum_array[] = {
	NETIF_F_IP_CSUM_BIT,
	NETIF_F_IPV6_CSUM_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_ip_csum_array);

const u8 netif_f_csum_mask_array[] = {
	NETIF_F_IP_CSUM_BIT,
	NETIF_F_IPV6_CSUM_BIT,
	NETIF_F_HW_CSUM_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_csum_mask_array);

const u8 netif_f_all_tso_array[] = {
	NETIF_F_TSO_BIT,
	NETIF_F_TSO6_BIT,
	NETIF_F_TSO_ECN_BIT,
	NETIF_F_TSO_MANGLEID_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_all_tso_array);

const u8 netif_f_tso_ecn_array[] = {
	NETIF_F_TSO_ECN_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_tso_ecn_array);

const u8 netif_f_all_fcoe_array[] = {
	NETIF_F_FCOE_CRC_BIT,
	NETIF_F_FCOE_MTU_BIT,
	NETIF_F_FSO_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_all_fcoe_array);

const u8 netif_f_gso_soft_array[] = {
	NETIF_F_TSO_BIT,
	NETIF_F_TSO6_BIT,
	NETIF_F_TSO_ECN_BIT,
	NETIF_F_TSO_MANGLEID_BIT,
	NETIF_F_GSO_SCTP_BIT,
	NETIF_F_GSO_UDP_L4_BIT,
	NETIF_F_GSO_FRAGLIST_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_gso_soft_array);

const u8 netif_f_one_for_all_array[] = {
	NETIF_F_TSO_BIT,
	NETIF_F_TSO6_BIT,
	NETIF_F_TSO_ECN_BIT,
	NETIF_F_TSO_MANGLEID_BIT,
	NETIF_F_GSO_SCTP_BIT,
	NETIF_F_GSO_UDP_L4_BIT,
	NETIF_F_GSO_FRAGLIST_BIT,
	NETIF_F_GSO_ROBUST_BIT,
	NETIF_F_SG_BIT,
	NETIF_F_HIGHDMA_BIT,
	NETIF_F_FRAGLIST_BIT,
	NETIF_F_VLAN_CHALLENGED,
};
EXPORT_SYMBOL_GPL(netif_f_one_for_all_array);

const u8 netif_f_all_for_all_array[] = {
	NETIF_F_NOCACHE_COPY_BIT,
	NETIF_F_FSO_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_all_for_all_array);

const u8 netif_f_upper_disables_array[] = {
	NETIF_F_LRO_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_upper_disables_array);

const u8 netif_f_soft_array[] = {
	NETIF_F_GSO_BIT,
	NETIF_F_GRO_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_soft_array);

const u8 netif_f_soft_off_array[] = {
	NETIF_F_GRO_FRAGLIST_BIT,
	NETIF_F_GRO_UDP_FWD_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_soft_off_array);

const u8 netif_f_vlan_array[] = {
	NETIF_F_HW_VLAN_CTAG_FILTER_BIT,
	NETIF_F_HW_VLAN_CTAG_RX_BIT,
	NETIF_F_HW_VLAN_CTAG_TX_BIT,
	NETIF_F_HW_VLAN_STAG_FILTER_BIT,
	NETIF_F_HW_VLAN_STAG_RX_BIT,
	NETIF_F_HW_VLAN_STAG_TX_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_vlan_array);

const u8 netif_f_tx_vlan_array[] = {
	NETIF_F_HW_VLAN_CTAG_TX_BIT,
	NETIF_F_HW_VLAN_STAG_TX_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_tx_vlan_array);

const u8 netif_f_rx_vlan_array[] = {
	NETIF_F_HW_VLAN_CTAG_RX_BIT,
	NETIF_F_HW_VLAN_STAG_RX_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_rx_vlan_array);

const u8 netif_f_vlan_filter_array[] = {
	NETIF_F_HW_VLAN_CTAG_FILTER_BIT,
	NETIF_F_HW_VLAN_STAG_FILTER_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_vlan_filter_array);

const u8 netif_f_gso_encap_array[] = {
	NETIF_F_GSO_GRE_BIT,
	NETIF_F_GSO_GRE_CSUM_BIT,
	NETIF_F_GSO_IPXIP4_BIT,
	NETIF_F_GSO_IPXIP6_BIT,
	NETIF_F_GSO_UDP_TUNNEL_BIT,
	NETIF_F_GSO_UDP_TUNNEL_CSUM_BIT,
};
EXPORT_SYMBOL_GPL(netif_f_gso_encap_array);

