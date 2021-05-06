/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#ifndef _CAN_NETLINK_TDC_H
#define _CAN_NETLINK_TDC_H

#include <net/netlink.h>

size_t can_tdc_get_size(const struct net_device *dev);

int can_tdc_changelink(struct net_device *dev, const struct nlattr *nla,
		       struct netlink_ext_ack *extack);

int can_tdc_fill_info(struct sk_buff *skb, const struct net_device *dev);

#endif /* !_CAN_NETLINK_TDC_H */
