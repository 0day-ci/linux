/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Management Component Transport Protocol (MCTP) - device
 * definitions.
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#ifndef __NET_MCTPDEVICE_H
#define __NET_MCTPDEVICE_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/refcount.h>

struct mctp_ifaddr {
	u8			eid;

	struct mctp_dev		*dev;
	struct list_head	dev_list;

	struct rcu_head		rcu;
};

struct mctp_dev {
	struct net_device	*dev;

	unsigned int		net;

	struct list_head	addrs; /* -> mctp_ifaddr.dev_list */

	struct rcu_head		rcu;
};

#define MCTP_INITIAL_DEFAULT_NET	1

struct mctp_dev *mctp_dev_get_rtnl(const struct net_device *dev);
struct mctp_dev *__mctp_dev_get(const struct net_device *dev);
struct mctp_dev *mctp_dev_get_rtnl(const struct net_device *dev);

#endif /* __NET_MCTPDEVICE_H */
