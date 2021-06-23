/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 MediaTek Inc.
 *
 * CCMNI Data virtual netwotrk driver
 */

#ifndef __CCMNI_NET_H__
#define __CCMNI_NET_H__

#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#define CCMNI_MTU		1500
#define CCMNI_TX_QUEUE		1000
#define CCMNI_NETDEV_WDT_TO	(1 * HZ)

#define IPV4_VERSION		0x40
#define IPV6_VERSION		0x60

#define MAX_CCMNI_NUM		22

/* One instance of this structure is instantiated for each
 * real_dev associated with ccmni
 */
struct ccmni_inst {
	int			index;
	atomic_t		usage;
	struct net_device	*dev;
	unsigned char		name[16];
};

/* an export struct of ccmni hardware interface operations
 */
struct ccmni_hif_ops {
	int (*xmit_pkt)(int index, void *data, int ref_flag);
};

struct ccmni_ctl_block {
	int (*xmit_pkt)(int index, void *data, int ref_flag);
	struct ccmni_hif_ops	*hif_ops;
	struct ccmni_inst	*ccmni_inst[MAX_CCMNI_NUM];
	int max_num;
};

int ccmni_hif_hook(struct ccmni_hif_ops *hif_ops);
int ccmni_rx_push(unsigned int ccmni_idx, struct sk_buff *skb);

#endif /* __CCMNI_NET_H__ */
