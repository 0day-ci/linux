// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#include <linux/can/dev.h>

#include "netlink-tdc.h"

static const struct nla_policy can_tdc_policy[IFLA_CAN_TDC_MAX + 1] = {
	[IFLA_CAN_TDC_TDCV_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCO_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCF_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCV] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCO] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCF] = { .type = NLA_U32 },
};

size_t can_tdc_get_size(const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	size_t size = nla_total_size(0) /* IFLA_CAN_TDC */;

	if (priv->tdc_const) {
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCV_MAX */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCO_MAX */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCF_MAX */
	}
	if (priv->tdc.tdco) {
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCV */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCO */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCF */
	}

	return size;
}

int can_tdc_changelink(struct net_device *dev, const struct nlattr *nla,
		       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_CAN_TDC_MAX + 1];
	struct can_priv *priv = netdev_priv(dev);
	struct can_tdc *tdc = &priv->tdc;
	const struct can_tdc_const *tdc_const = priv->tdc_const;
	int err;

	if (!tdc_const)
		return -EOPNOTSUPP;

	if (dev->flags & IFF_UP)
		return -EBUSY;

	err = nla_parse_nested(tb, IFLA_CAN_TDC_MAX, nla,
			       can_tdc_policy, extack);
	if (err)
		return err;

	if (tb[IFLA_CAN_TDC_TDCV]) {
		u32 tdcv = nla_get_u32(tb[IFLA_CAN_TDC_TDCV]);

		if (tdcv && !tdc_const->tdcv_max)
			return -EOPNOTSUPP;

		if (tdcv > tdc_const->tdcv_max)
			return -EINVAL;

		tdc->tdcv = tdcv;
	}

	if (tb[IFLA_CAN_TDC_TDCO]) {
		u32 tdco = nla_get_u32(tb[IFLA_CAN_TDC_TDCO]);

		if (tdco && !tdc_const->tdco_max)
			return -EOPNOTSUPP;

		if (tdco > tdc_const->tdco_max)
			return -EINVAL;

		tdc->tdco = tdco;
	}

	if (tb[IFLA_CAN_TDC_TDCF]) {
		u32 tdcf = nla_get_u32(tb[IFLA_CAN_TDC_TDCF]);

		if (tdcf && !tdc_const->tdcf_max)
			return -EOPNOTSUPP;

		if (tdcf > tdc_const->tdcf_max)
			return -EINVAL;

		tdc->tdcf = tdcf;
	}

	return 0;
}

int can_tdc_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct nlattr *nest;
	struct can_priv *priv = netdev_priv(dev);
	struct can_tdc *tdc = &priv->tdc;
	const struct can_tdc_const *tdc_const = priv->tdc_const;

	if (!tdc_const)
		return 0;

	nest = nla_nest_start(skb, IFLA_CAN_TDC);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(skb, IFLA_CAN_TDC_TDCV_MAX, tdc_const->tdcv_max) ||
	    nla_put_u32(skb, IFLA_CAN_TDC_TDCO_MAX, tdc_const->tdco_max) ||
	    nla_put_u32(skb, IFLA_CAN_TDC_TDCF_MAX, tdc_const->tdcf_max) ||
	    nla_put_u32(skb, IFLA_CAN_TDC_TDCV, tdc->tdcv) ||
	    nla_put_u32(skb, IFLA_CAN_TDC_TDCO, tdc->tdco) ||
	    nla_put_u32(skb, IFLA_CAN_TDC_TDCF, tdc->tdcf))
		return -EMSGSIZE;

	nla_nest_end(skb, nest);

	return 0;
}
