/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef _FUNETH_H
#define _FUNETH_H

#include <uapi/linux/if_ether.h>
#include <uapi/linux/net_tstamp.h>
#include <linux/seqlock.h>
#include <net/devlink.h>
#include "fun_dev.h"

#define ADMIN_SQE_SIZE SZ_128
#define ADMIN_CQE_SIZE SZ_64
#define ADMIN_RSP_MAX_LEN (ADMIN_CQE_SIZE - sizeof(struct fun_cqe_info))

#define FUN_MAX_MTU 9024

#define SQ_DEPTH 512U
#define CQ_DEPTH 1024U
#define RQ_DEPTH (512U / (PAGE_SIZE / 4096))

#define CQ_INTCOAL_USEC 10
#define CQ_INTCOAL_NPKT 16
#define SQ_INTCOAL_USEC 10
#define SQ_INTCOAL_NPKT 16

#define INVALID_LPORT 0xffff

#define FUN_PORT_CAP_PAUSE_MASK (FUN_PORT_CAP_TX_PAUSE | FUN_PORT_CAP_RX_PAUSE)

struct fun_vport_info {
	u8 mac[ETH_ALEN];
	u16 vlan;
	__be16 vlan_proto;
	u8 qos;
	u8 spoofchk:1;
	u8 trusted:1;
	unsigned int max_rate;
};

/* "subclass" of fun_dev for Ethernet functions */
struct fun_ethdev {
	struct fun_dev fdev;

	/* the function's network ports */
	struct net_device **netdevs;
	unsigned int num_ports;

	/* configuration for the function's virtual ports */
	unsigned int num_vports;
	struct fun_vport_info *vport_info;

	unsigned int nsqs_per_port;
};

static inline struct fun_ethdev *to_fun_ethdev(struct fun_dev *p)
{
	return container_of(p, struct fun_ethdev, fdev);
}

/* Per netdevice driver state, i.e., netdev_priv. */
struct funeth_priv {
	struct fun_dev *fdev;
	struct pci_dev *pdev;
	struct net_device *netdev;

	struct funeth_rxq * __rcu *rxqs;
	struct funeth_txq **txqs;
	struct funeth_txq **xdpqs;

	struct fun_irq *irqs;
	unsigned int num_irqs;
	unsigned int num_tx_irqs;

	unsigned int lane_attrs;
	u16 lport;

	/* link settings */
	u64 port_caps;
	u64 advertising;
	u64 lp_advertising;
	unsigned int link_speed;
	u8 xcvr_type;
	u8 active_fc;
	u8 active_fec;
	u8 link_down_reason;
	seqcount_t link_seq;

	u32 msg_enable;

	unsigned int ethid_start;

	unsigned int num_xdpqs;

	/* ethtool, etc. config parameters */
	unsigned int sq_depth;
	unsigned int rq_depth;
	unsigned int cq_depth;
	unsigned int cq_irq_db;
	u8 tx_coal_usec;
	u8 tx_coal_count;
	u8 rx_coal_usec;
	u8 rx_coal_count;

	struct hwtstamp_config hwtstamp_cfg;

	/* cumulative queue stats from earlier queue instances */
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_dropped;
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_dropped;

	/* RSS */
	unsigned int rss_hw_id;
	enum fun_eth_hash_alg hash_algo;
	u8 rss_key[FUN_ETH_RSS_MAX_KEY_SIZE];
	unsigned int indir_table_nentries;
	u32 indir_table[FUN_ETH_RSS_MAX_INDIR_ENT];
	dma_addr_t rss_dma_addr;
	void *rss_cfg;

	/* DMA area for port stats */
	dma_addr_t stats_dma_addr;
	__be64 *stats;

	struct bpf_prog *xdp_prog;

	struct devlink_port dl_port;

	/* kTLS state */
	unsigned int ktls_id;
	atomic64_t tx_tls_add;
	atomic64_t tx_tls_del;
	atomic64_t tx_tls_resync;
};

void fun_set_ethtool_ops(struct net_device *netdev);
int fun_port_write_cmd(struct funeth_priv *fp, int key, u64 data);
int fun_port_read_cmd(struct funeth_priv *fp, int key, u64 *data);
int fun_create_and_bind_tx(struct funeth_priv *fp, u32 ethid, u32 sqid);
void fun_reset_rss_indir(struct net_device *dev);
int fun_config_rss(struct net_device *dev, int algo, const u8 *key,
		   const u32 *qtable, u8 op);

int fun_alloc_rings(struct net_device *netdev);
void fun_free_rings(struct net_device *netdev);
int fun_alloc_queue_irqs(struct net_device *dev);

#endif /* _FUNETH_H */
