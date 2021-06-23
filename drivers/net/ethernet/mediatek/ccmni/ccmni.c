// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 MediaTek Inc.
 *
 * CCMNI Data virtual netwotrk driver
 */

#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <net/sch_generic.h>

#include "ccmni.h"

static struct ccmni_ctl_block *s_ccmni_ctlb;
static int ccmni_hook_ready;

/* Network Device Operations */

static int ccmni_open(struct net_device *ccmni_dev)
{
	struct ccmni_inst *ccmni = netdev_priv(ccmni_dev);

	netif_tx_start_all_queues(ccmni_dev);
	netif_carrier_on(ccmni_dev);

	if (atomic_inc_return(&ccmni->usage) > 1) {
		atomic_dec(&ccmni->usage);
		netdev_err(ccmni_dev, "dev already open\n");
		return -EINVAL;
	}

	return 0;
}

static int ccmni_close(struct net_device *ccmni_dev)
{
	struct ccmni_inst *ccmni = netdev_priv(ccmni_dev);

	atomic_dec(&ccmni->usage);
	netif_tx_disable(ccmni_dev);

	return 0;
}

static netdev_tx_t
ccmni_start_xmit(struct sk_buff *skb, struct net_device *ccmni_dev)
{
	struct ccmni_inst *ccmni = NULL;

	if (unlikely(!ccmni_hook_ready))
		goto tx_ok;

	if (!skb || !ccmni_dev)
		goto tx_ok;

	ccmni = netdev_priv(ccmni_dev);

	/* some process can modify ccmni_dev->mtu */
	if (skb->len > ccmni_dev->mtu) {
		netdev_err(ccmni_dev, "xmit fail: len(0x%x) > MTU(0x%x, 0x%x)",
			   skb->len, CCMNI_MTU, ccmni_dev->mtu);
		goto tx_ok;
	}

	/* hardware driver send packet will return a negative value
	 * ask the Linux netdevice to stop the tx queue
	 */
	if ((s_ccmni_ctlb->xmit_pkt(ccmni->index, skb, 0)) < 0)
		return NETDEV_TX_BUSY;

	return NETDEV_TX_OK;
tx_ok:
	dev_kfree_skb(skb);
	ccmni_dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static int ccmni_change_mtu(struct net_device *ccmni_dev, int new_mtu)
{
	if (new_mtu < 0 || new_mtu > CCMNI_MTU)
		return -EINVAL;

	if (unlikely(!ccmni_dev))
		return -EINVAL;

	ccmni_dev->mtu = new_mtu;
	return 0;
}

static void ccmni_tx_timeout(struct net_device *ccmni_dev, unsigned int txqueue)
{
	struct ccmni_inst *ccmni = netdev_priv(ccmni_dev);

	ccmni_dev->stats.tx_errors++;
	if (atomic_read(&ccmni->usage) > 0)
		netif_tx_wake_all_queues(ccmni_dev);
}

static const struct net_device_ops ccmni_netdev_ops = {
	.ndo_open		= ccmni_open,
	.ndo_stop		= ccmni_close,
	.ndo_start_xmit		= ccmni_start_xmit,
	.ndo_tx_timeout		= ccmni_tx_timeout,
	.ndo_change_mtu		= ccmni_change_mtu,
};

/* init ccmni network device */
static inline void ccmni_dev_init(struct net_device *ccmni_dev, unsigned int idx)
{
	ccmni_dev->mtu = CCMNI_MTU;
	ccmni_dev->tx_queue_len = CCMNI_TX_QUEUE;
	ccmni_dev->watchdog_timeo = CCMNI_NETDEV_WDT_TO;
	ccmni_dev->flags = IFF_NOARP &
			(~IFF_BROADCAST & ~IFF_MULTICAST);

	/* not support VLAN */
	ccmni_dev->features = NETIF_F_VLAN_CHALLENGED;
	ccmni_dev->features |= NETIF_F_SG;
	ccmni_dev->hw_features |= NETIF_F_SG;

	/* pure ip mode */
	ccmni_dev->type = ARPHRD_PUREIP;
	ccmni_dev->header_ops = NULL;
	ccmni_dev->hard_header_len = 0;
	ccmni_dev->addr_len = 0;
	ccmni_dev->priv_destructor = free_netdev;
	ccmni_dev->netdev_ops = &ccmni_netdev_ops;
	random_ether_addr((u8 *)ccmni_dev->dev_addr);
	sprintf(ccmni_dev->name, "ccmni%d", idx);
}

/* init ccmni instance */
static inline void ccmni_inst_init(struct net_device *netdev, unsigned int idx)
{
	struct ccmni_inst *ccmni = netdev_priv(netdev);

	ccmni->index = idx;
	ccmni->dev = netdev;
	atomic_set(&ccmni->usage, 0);

	s_ccmni_ctlb->ccmni_inst[idx] = ccmni;
}

/* ccmni driver module startup/shutdown */

static int __init ccmni_init(void)
{
	struct net_device *dev = NULL;
	unsigned int i, j;
	int ret = 0;

	s_ccmni_ctlb = kzalloc(sizeof(*s_ccmni_ctlb), GFP_KERNEL);
	if (!s_ccmni_ctlb)
		return -ENOMEM;

	s_ccmni_ctlb->max_num = MAX_CCMNI_NUM;
	for (i = 0; i < MAX_CCMNI_NUM; i++) {
		/* alloc multiple tx queue, 2 txq and 1 rxq */
		dev = alloc_etherdev_mqs(sizeof(struct ccmni_inst), 2, 1);
		if (unlikely(!dev)) {
			ret = -ENOMEM;
			goto alloc_netdev_fail;
		}
		ccmni_dev_init(dev, i);
		ccmni_inst_init(dev, i);
		ret = register_netdev(dev);
		if (ret)
			goto alloc_netdev_fail;
	}
	return ret;

alloc_netdev_fail:
	if (dev) {
		free_netdev(dev);
		s_ccmni_ctlb->ccmni_inst[i] = NULL;
	}
	for (j = i - 1; j >= 0; j--) {
		unregister_netdev(s_ccmni_ctlb->ccmni_inst[j]->dev);
		s_ccmni_ctlb->ccmni_inst[j] = NULL;
	}
	kfree(s_ccmni_ctlb);
	s_ccmni_ctlb = NULL;

	return ret;
}

static void __exit ccmni_exit(void)
{
	struct ccmni_ctl_block *ctlb = NULL;
	struct ccmni_inst *ccmni = NULL;
	int i;

	ctlb = s_ccmni_ctlb;
	if (!s_ccmni_ctlb)
		return;
	for (i = 0; i < s_ccmni_ctlb->max_num; i++) {
		ccmni = s_ccmni_ctlb->ccmni_inst[i];
		if (ccmni) {
			unregister_netdev(ccmni->dev);
			s_ccmni_ctlb->ccmni_inst[i] = NULL;
		}
	}
	kfree(s_ccmni_ctlb);
	s_ccmni_ctlb = NULL;
}

/* exposed API
 * receive incoming datagrams from the Modem and push them to the
 * kernel networking system
 */
int ccmni_rx_push(unsigned int ccmni_idx, struct sk_buff *skb)
{
	struct ccmni_inst *ccmni = NULL;
	struct net_device *dev = NULL;
	int pkt_type, skb_len;

	if (unlikely(!ccmni_hook_ready))
		return -EINVAL;

	/* Some hardware can send us error index. Catch them */
	if (unlikely(ccmni_idx >= s_ccmni_ctlb->max_num))
		return -EINVAL;

	ccmni = s_ccmni_ctlb->ccmni_inst[ccmni_idx];
	dev = ccmni->dev;

	pkt_type = skb->data[0] & 0xF0;
	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);
	skb_set_mac_header(skb, 0);
	skb_reset_mac_len(skb);
	skb->dev = dev;

	if (pkt_type == IPV6_VERSION)
		skb->protocol = htons(ETH_P_IPV6);
	else if (pkt_type == IPV4_VERSION)
		skb->protocol = htons(ETH_P_IP);

	skb_len = skb->len;

	if (!in_interrupt())
		netif_rx_ni(skb);
	else
		netif_rx(skb);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb_len;

	return 0;
}
EXPORT_SYMBOL(ccmni_rx_push);

/* exposed API
 * hardware driver can init the struct ccmni_hif_ops and implement specific
 * xmnit function to send UL packets to the specific hardware
 */
int ccmni_hif_hook(struct ccmni_hif_ops *hif_ops)
{
	if (unlikely(!hif_ops)) {
		pr_err("ccmni: %s fail: argument is NULL\n", __func__);
		return -EINVAL;
	}
	if (unlikely(!s_ccmni_ctlb)) {
		pr_err("ccmni: %s fail: s_ccmni_ctlb is NULL\n", __func__);
		return -EINVAL;
	}
	if (unlikely(s_ccmni_ctlb->hif_ops)) {
		pr_err("ccmni: %s fail: hif_ops already hooked\n", __func__);
		return -EINVAL;
	}

	s_ccmni_ctlb->hif_ops = hif_ops;
	if (!hif_ops->xmit_pkt) {
		pr_err("ccmni: %s fail: key hook func: xmit is NULL\n",
		       __func__);
		return -EINVAL;
	}

	s_ccmni_ctlb->xmit_pkt = hif_ops->xmit_pkt;
	ccmni_hook_ready = 1;

	return 0;
}
EXPORT_SYMBOL(ccmni_hif_hook);

module_init(ccmni_init);
module_exit(ccmni_exit);
MODULE_AUTHOR("MediaTek, Inc.");
MODULE_DESCRIPTION("ccmni driver v1.0");
MODULE_LICENSE("GPL");
