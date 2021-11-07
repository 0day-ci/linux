/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Network device features.
 */
#ifndef _LINUX_NETDEV_FEATURES_H
#define _LINUX_NETDEV_FEATURES_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>

enum {
	NETIF_F_SG_BIT,			/* Scatter/gather IO. */
	NETIF_F_IP_CSUM_BIT,		/* Can checksum TCP/UDP over IPv4. */
	__UNUSED_NETIF_F_1,
	NETIF_F_HW_CSUM_BIT,		/* Can checksum all the packets. */
	NETIF_F_IPV6_CSUM_BIT,		/* Can checksum TCP/UDP over IPV6 */
	NETIF_F_HIGHDMA_BIT,		/* Can DMA to high memory. */
	NETIF_F_FRAGLIST_BIT,		/* Scatter/gather IO. */
	NETIF_F_HW_VLAN_CTAG_TX_BIT,	/* Transmit VLAN CTAG HW acceleration */
	NETIF_F_HW_VLAN_CTAG_RX_BIT,	/* Receive VLAN CTAG HW acceleration */
	NETIF_F_HW_VLAN_CTAG_FILTER_BIT,/* Receive filtering on VLAN CTAGs */
	NETIF_F_VLAN_CHALLENGED_BIT,	/* Device cannot handle VLAN packets */
	NETIF_F_GSO_BIT,		/* Enable software GSO. */
	NETIF_F_LLTX_BIT,		/* LockLess TX - deprecated. Please */
					/* do not use LLTX in new drivers */
	NETIF_F_NETNS_LOCAL_BIT,	/* Does not change network namespaces */
	NETIF_F_GRO_BIT,		/* Generic receive offload */
	NETIF_F_LRO_BIT,		/* large receive offload */

	/**/NETIF_F_GSO_SHIFT,		/* keep the order of SKB_GSO_* bits */
	NETIF_F_TSO_BIT			/* ... TCPv4 segmentation */
		= NETIF_F_GSO_SHIFT,
	NETIF_F_GSO_ROBUST_BIT,		/* ... ->SKB_GSO_DODGY */
	NETIF_F_TSO_ECN_BIT,		/* ... TCP ECN support */
	NETIF_F_TSO_MANGLEID_BIT,	/* ... IPV4 ID mangling allowed */
	NETIF_F_TSO6_BIT,		/* ... TCPv6 segmentation */
	NETIF_F_FSO_BIT,		/* ... FCoE segmentation */
	NETIF_F_GSO_GRE_BIT,		/* ... GRE with TSO */
	NETIF_F_GSO_GRE_CSUM_BIT,	/* ... GRE with csum with TSO */
	NETIF_F_GSO_IPXIP4_BIT,		/* ... IP4 or IP6 over IP4 with TSO */
	NETIF_F_GSO_IPXIP6_BIT,		/* ... IP4 or IP6 over IP6 with TSO */
	NETIF_F_GSO_UDP_TUNNEL_BIT,	/* ... UDP TUNNEL with TSO */
	NETIF_F_GSO_UDP_TUNNEL_CSUM_BIT,/* ... UDP TUNNEL with TSO & CSUM */
	NETIF_F_GSO_PARTIAL_BIT,	/* ... Only segment inner-most L4
					 *     in hardware and all other
					 *     headers in software.
					 */
	NETIF_F_GSO_TUNNEL_REMCSUM_BIT, /* ... TUNNEL with TSO & REMCSUM */
	NETIF_F_GSO_SCTP_BIT,		/* ... SCTP fragmentation */
	NETIF_F_GSO_ESP_BIT,		/* ... ESP with TSO */
	NETIF_F_GSO_UDP_BIT,		/* ... UFO, deprecated except tuntap */
	NETIF_F_GSO_UDP_L4_BIT,		/* ... UDP payload GSO (not UFO) */
	NETIF_F_GSO_FRAGLIST_BIT,		/* ... Fraglist GSO */
	/**/NETIF_F_GSO_LAST =		/* last bit, see GSO_MASK */
		NETIF_F_GSO_FRAGLIST_BIT,

	NETIF_F_FCOE_CRC_BIT,		/* FCoE CRC32 */
	NETIF_F_SCTP_CRC_BIT,		/* SCTP checksum offload */
	NETIF_F_FCOE_MTU_BIT,		/* Supports max FCoE MTU, 2158 bytes*/
	NETIF_F_NTUPLE_BIT,		/* N-tuple filters supported */
	NETIF_F_RXHASH_BIT,		/* Receive hashing offload */
	NETIF_F_RXCSUM_BIT,		/* Receive checksumming offload */
	NETIF_F_NOCACHE_COPY_BIT,	/* Use no-cache copyfromuser */
	NETIF_F_LOOPBACK_BIT,		/* Enable loopback */
	NETIF_F_RXFCS_BIT,		/* Append FCS to skb pkt data */
	NETIF_F_RXALL_BIT,		/* Receive errored frames too */
	NETIF_F_HW_VLAN_STAG_TX_BIT,	/* Transmit VLAN STAG HW acceleration */
	NETIF_F_HW_VLAN_STAG_RX_BIT,	/* Receive VLAN STAG HW acceleration */
	NETIF_F_HW_VLAN_STAG_FILTER_BIT,/* Receive filtering on VLAN STAGs */
	NETIF_F_HW_L2FW_DOFFLOAD_BIT,	/* Allow L2 Forwarding in Hardware */

	NETIF_F_HW_TC_BIT,		/* Offload TC infrastructure */
	NETIF_F_HW_ESP_BIT,		/* Hardware ESP transformation offload */
	NETIF_F_HW_ESP_TX_CSUM_BIT,	/* ESP with TX checksum offload */
	NETIF_F_RX_UDP_TUNNEL_PORT_BIT, /* Offload of RX port for UDP tunnels */
	NETIF_F_HW_TLS_TX_BIT,		/* Hardware TLS TX offload */
	NETIF_F_HW_TLS_RX_BIT,		/* Hardware TLS RX offload */

	NETIF_F_GRO_HW_BIT,		/* Hardware Generic receive offload */
	NETIF_F_HW_TLS_RECORD_BIT,	/* Offload TLS record */
	NETIF_F_GRO_FRAGLIST_BIT,	/* Fraglist GRO */

	NETIF_F_HW_MACSEC_BIT,		/* Offload MACsec operations */
	NETIF_F_GRO_UDP_FWD_BIT,	/* Allow UDP GRO for forwarding */

	NETIF_F_HW_HSR_TAG_INS_BIT,	/* Offload HSR tag insertion */
	NETIF_F_HW_HSR_TAG_RM_BIT,	/* Offload HSR tag removal */
	NETIF_F_HW_HSR_FWD_BIT,		/* Offload HSR forwarding */
	NETIF_F_HW_HSR_DUP_BIT,		/* Offload HSR duplication */

	/*
	 * Add your fresh new feature above and remember to update
	 * netdev_features_strings[] in net/ethtool/common.c and maybe
	 * some feature mask #defines below. Please also describe it
	 * in Documentation/networking/netdev-features.rst.
	 */

	/**/NETDEV_FEATURE_COUNT
};

typedef struct {
	DECLARE_BITMAP(bits, NETDEV_FEATURE_COUNT);
} netdev_features_t;

/* This goes for the MSB to the LSB through the set feature bits,
 * mask_addr should be a u64 and bit an int
 */
#define for_each_netdev_feature(mask_addr, bit)				\
	for_each_set_bit(bit, (unsigned long *)(mask_addr.bits), NETDEV_FEATURE_COUNT)

extern netdev_features_t netdev_ethtool_features __ro_after_init;
extern netdev_features_t netdev_never_change_features __ro_after_init;
extern netdev_features_t netdev_gso_mask_features __ro_after_init;
extern netdev_features_t netdev_ip_csum_features __ro_after_init;
extern netdev_features_t netdev_csum_mask_features __ro_after_init;
extern netdev_features_t netdev_all_tso_features __ro_after_init;
extern netdev_features_t netdev_tso_ecn_features __ro_after_init;
extern netdev_features_t netdev_all_fcoe_features __ro_after_init;
extern netdev_features_t netdev_gso_software_features __ro_after_init;
extern netdev_features_t netdev_one_for_all_features __ro_after_init;
extern netdev_features_t netdev_all_for_all_features __ro_after_init;
extern netdev_features_t netdev_upper_disable_features __ro_after_init;
extern netdev_features_t netdev_soft_features __ro_after_init;
extern netdev_features_t netdev_soft_off_features __ro_after_init;
extern netdev_features_t netdev_vlan_features __ro_after_init;
extern netdev_features_t netdev_tx_vlan_features __ro_after_init;
extern netdev_features_t netdev_gso_encap_all_features __ro_after_init;

/* Features valid for ethtool to change */
/* = all defined minus driver/device-class-related */
#define NETIF_F_NEVER_CHANGE	netdev_never_change_features

#define NETIF_F_ETHTOOL_BITS	netdev_ethtool_features

/* Segmentation offload feature mask */
#define NETIF_F_GSO_MASK	netdev_gso_mask_features

/* List of IP checksum features. Note that NETIF_F_HW_CSUM should not be
 * set in features when NETIF_F_IP_CSUM or NETIF_F_IPV6_CSUM are set--
 * this would be contradictory
 */
#define NETIF_F_CSUM_MASK	netdev_csum_mask_features

#define NETIF_F_ALL_TSO		netdev_all_tso_features

#define NETIF_F_ALL_FCOE	netdev_all_fcoe_features

/* List of features with software fallbacks. */
#define NETIF_F_GSO_SOFTWARE	netdev_gso_software_features

/*
 * If one device supports one of these features, then enable them
 * for all in netdev_increment_features.
 */
#define NETIF_F_ONE_FOR_ALL	netdev_one_for_all_features

/*
 * If one device doesn't support one of these features, then disable it
 * for all in netdev_increment_features.
 */
#define NETIF_F_ALL_FOR_ALL	netdev_all_for_all_features

/*
 * If upper/master device has these features disabled, they must be disabled
 * on all lower/slave devices as well.
 */
#define NETIF_F_UPPER_DISABLES	netdev_upper_disable_features

/* changeable features with no special hardware requirements */
#define NETIF_F_SOFT_FEATURES	netdev_soft_features

/* Changeable features with no special hardware requirements that defaults to off. */
#define NETIF_F_SOFT_FEATURES_OFF	netdev_soft_off_features

#define NETIF_F_VLAN_FEATURES	netdev_vlan_features

#define NETIF_F_GSO_ENCAP_ALL	netdev_gso_encap_all_features

static inline void netdev_features_zero(netdev_features_t *dst)
{
	bitmap_zero(dst->bits, NETDEV_FEATURE_COUNT);
}

static inline void netdev_features_fill(netdev_features_t *dst)
{
	bitmap_fill(dst->bits, NETDEV_FEATURE_COUNT);
}

static inline bool netdev_features_empty(netdev_features_t src)
{
	return bitmap_empty(src.bits, NETDEV_FEATURE_COUNT);
}

static inline bool netdev_features_equal(netdev_features_t src1,
					 netdev_features_t src2)
{
	return bitmap_equal(src1.bits, src2.bits, NETDEV_FEATURE_COUNT);
}

static inline netdev_features_t
netdev_features_and(netdev_features_t a, netdev_features_t b)
{
	netdev_features_t dst;

	bitmap_and(dst.bits, a.bits, b.bits, NETDEV_FEATURE_COUNT);
	return dst;
}

static inline netdev_features_t
netdev_features_or(netdev_features_t a, netdev_features_t b)
{
	netdev_features_t dst;

	bitmap_or(dst.bits, a.bits, b.bits, NETDEV_FEATURE_COUNT);
	return dst;
}

static inline netdev_features_t
netdev_features_xor(netdev_features_t a, netdev_features_t b)
{
	netdev_features_t dst;

	bitmap_xor(dst.bits, a.bits, b.bits, NETDEV_FEATURE_COUNT);
	return dst;
}

static inline netdev_features_t
netdev_features_andnot(netdev_features_t a, netdev_features_t b)
{
	netdev_features_t dst;

	bitmap_andnot(dst.bits, a.bits, b.bits, NETDEV_FEATURE_COUNT);
	return dst;
}

static inline void netdev_features_set_bit(int nr, netdev_features_t *src)
{
	__set_bit(nr, src->bits);
}

static inline void netdev_features_clear_bit(int nr, netdev_features_t *src)
{
	__clear_bit(nr, src->bits);
}

static inline void netdev_features_mod_bit(int nr, netdev_features_t *src,
					   int set)
{
	if (set)
		netdev_features_set_bit(nr, src);
	else
		netdev_features_clear_bit(nr, src);
}

static inline void netdev_features_change_bit(int nr, netdev_features_t *src)
{
	__change_bit(nr, src->bits);
}

static inline bool netdev_features_test_bit(int nr, netdev_features_t src)
{
	return test_bit(nr, src.bits);
}

static inline void netdev_features_set_array(const int *array, int array_size,
					     netdev_features_t *src)
{
	int i;

	for (i = 0; i < array_size; i++)
		netdev_features_set_bit(array[i], src);
}

static inline bool netdev_features_intersects(netdev_features_t src1,
					      netdev_features_t src2)
{
	return bitmap_intersects(src1.bits, src2.bits, NETDEV_FEATURE_COUNT);
}

static inline bool netdev_features_subset(netdev_features_t src1,
					  netdev_features_t src2)
{
	return bitmap_subset(src1.bits, src2.bits, NETDEV_FEATURE_COUNT);
}

#endif	/* _LINUX_NETDEV_FEATURES_H */
