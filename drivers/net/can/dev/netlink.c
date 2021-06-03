// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 */

#include <linux/can/dev.h>
#include <net/rtnetlink.h>

static const struct nla_policy can_policy[IFLA_CAN_MAX + 1] = {
	[IFLA_CAN_STATE] = { .type = NLA_U32 },
	[IFLA_CAN_CTRLMODE] = { .len = sizeof(struct can_ctrlmode) },
	[IFLA_CAN_RESTART_MS] = { .type = NLA_U32 },
	[IFLA_CAN_RESTART] = { .type = NLA_U32 },
	[IFLA_CAN_BITTIMING] = { .len = sizeof(struct can_bittiming) },
	[IFLA_CAN_BITTIMING_CONST] = { .len = sizeof(struct can_bittiming_const) },
	[IFLA_CAN_CLOCK] = { .len = sizeof(struct can_clock) },
	[IFLA_CAN_BERR_COUNTER] = { .len = sizeof(struct can_berr_counter) },
	[IFLA_CAN_DATA_BITTIMING] = { .len = sizeof(struct can_bittiming) },
	[IFLA_CAN_DATA_BITTIMING_CONST]	= { .len = sizeof(struct can_bittiming_const) },
	[IFLA_CAN_TERMINATION] = { .type = NLA_U16 },
	[IFLA_CAN_TDC] = { .type = NLA_NESTED },
};

static const struct nla_policy can_tdc_policy[IFLA_CAN_TDC_MAX + 1] = {
	[IFLA_CAN_TDC_TDCV_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCO_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCF_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCV] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCO] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCF] = { .type = NLA_U32 },
};

static int can_validate(struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	bool is_can_fd = false;

	/* Make sure that valid CAN FD configurations always consist of
	 * - nominal/arbitration bittiming
	 * - data bittiming
	 * - control mode with CAN_CTRLMODE_FD set
	 */

	if (!data)
		return 0;

	if (data[IFLA_CAN_CTRLMODE]) {
		struct can_ctrlmode *cm = nla_data(data[IFLA_CAN_CTRLMODE]);

		is_can_fd = cm->flags & cm->mask & CAN_CTRLMODE_FD;
	}

	if (is_can_fd) {
		if (!data[IFLA_CAN_BITTIMING] || !data[IFLA_CAN_DATA_BITTIMING])
			return -EOPNOTSUPP;
	}

	if (data[IFLA_CAN_DATA_BITTIMING] || data[IFLA_CAN_TDC]) {
		if (!is_can_fd)
			return -EOPNOTSUPP;
	}

	return 0;
}

static int can_tdc_changelink(struct net_device *dev, const struct nlattr *nla,
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

static int can_changelink(struct net_device *dev, struct nlattr *tb[],
			  struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	struct can_priv *priv = netdev_priv(dev);
	int err;

	/* We need synchronization with dev->stop() */
	ASSERT_RTNL();

	if (data[IFLA_CAN_BITTIMING]) {
		struct can_bittiming bt;

		/* Do not allow changing bittiming while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;

		/* Calculate bittiming parameters based on
		 * bittiming_const if set, otherwise pass bitrate
		 * directly via do_set_bitrate(). Bail out if neither
		 * is given.
		 */
		if (!priv->bittiming_const && !priv->do_set_bittiming)
			return -EOPNOTSUPP;

		memcpy(&bt, nla_data(data[IFLA_CAN_BITTIMING]), sizeof(bt));
		err = can_get_bittiming(dev, &bt,
					priv->bittiming_const,
					priv->bitrate_const,
					priv->bitrate_const_cnt);
		if (err)
			return err;

		if (priv->bitrate_max && bt.bitrate > priv->bitrate_max) {
			netdev_err(dev, "arbitration bitrate surpasses transceiver capabilities of %d bps\n",
				   priv->bitrate_max);
			return -EINVAL;
		}

		memcpy(&priv->bittiming, &bt, sizeof(bt));

		if (priv->do_set_bittiming) {
			/* Finally, set the bit-timing registers */
			err = priv->do_set_bittiming(dev);
			if (err)
				return err;
		}
	}

	if (data[IFLA_CAN_CTRLMODE]) {
		struct can_ctrlmode *cm;
		u32 ctrlstatic;
		u32 maskedflags;

		/* Do not allow changing controller mode while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;
		cm = nla_data(data[IFLA_CAN_CTRLMODE]);
		ctrlstatic = priv->ctrlmode_static;
		maskedflags = cm->flags & cm->mask;

		/* check whether provided bits are allowed to be passed */
		if (cm->mask & ~(priv->ctrlmode_supported | ctrlstatic))
			return -EOPNOTSUPP;

		/* do not check for static fd-non-iso if 'fd' is disabled */
		if (!(maskedflags & CAN_CTRLMODE_FD))
			ctrlstatic &= ~CAN_CTRLMODE_FD_NON_ISO;

		/* make sure static options are provided by configuration */
		if ((maskedflags & ctrlstatic) != ctrlstatic)
			return -EOPNOTSUPP;

		/* clear bits to be modified and copy the flag values */
		priv->ctrlmode &= ~cm->mask;
		priv->ctrlmode |= maskedflags;

		/* CAN_CTRLMODE_FD can only be set when driver supports FD */
		if (priv->ctrlmode & CAN_CTRLMODE_FD)
			dev->mtu = CANFD_MTU;
		else
			dev->mtu = CAN_MTU;
	}

	if (data[IFLA_CAN_RESTART_MS]) {
		/* Do not allow changing restart delay while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;
		priv->restart_ms = nla_get_u32(data[IFLA_CAN_RESTART_MS]);
	}

	if (data[IFLA_CAN_RESTART]) {
		/* Do not allow a restart while not running */
		if (!(dev->flags & IFF_UP))
			return -EINVAL;
		err = can_restart_now(dev);
		if (err)
			return err;
	}

	if (data[IFLA_CAN_DATA_BITTIMING]) {
		struct can_bittiming dbt;

		/* Do not allow changing bittiming while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;

		/* Calculate bittiming parameters based on
		 * data_bittiming_const if set, otherwise pass bitrate
		 * directly via do_set_bitrate(). Bail out if neither
		 * is given.
		 */
		if (!priv->data_bittiming_const && !priv->do_set_data_bittiming)
			return -EOPNOTSUPP;

		memcpy(&dbt, nla_data(data[IFLA_CAN_DATA_BITTIMING]),
		       sizeof(dbt));
		err = can_get_bittiming(dev, &dbt,
					priv->data_bittiming_const,
					priv->data_bitrate_const,
					priv->data_bitrate_const_cnt);
		if (err)
			return err;

		if (priv->bitrate_max && dbt.bitrate > priv->bitrate_max) {
			netdev_err(dev, "canfd data bitrate surpasses transceiver capabilities of %d bps\n",
				   priv->bitrate_max);
			return -EINVAL;
		}

		memcpy(&priv->data_bittiming, &dbt, sizeof(dbt));

		can_calc_tdco(dev);

		if (priv->do_set_data_bittiming) {
			/* Finally, set the bit-timing registers */
			err = priv->do_set_data_bittiming(dev);
			if (err)
				return err;
		}
	}

	if (data[IFLA_CAN_TERMINATION]) {
		const u16 termval = nla_get_u16(data[IFLA_CAN_TERMINATION]);
		const unsigned int num_term = priv->termination_const_cnt;
		unsigned int i;

		if (!priv->do_set_termination)
			return -EOPNOTSUPP;

		/* check whether given value is supported by the interface */
		for (i = 0; i < num_term; i++) {
			if (termval == priv->termination_const[i])
				break;
		}
		if (i >= num_term)
			return -EINVAL;

		/* Finally, set the termination value */
		err = priv->do_set_termination(dev, termval);
		if (err)
			return err;

		priv->termination = termval;
	}

	if (data[IFLA_CAN_TDC]) {
		err = can_tdc_changelink(dev, data[IFLA_CAN_TDC], extack);
		if (err)
			return err;
	}

	return 0;
}

static size_t can_tdc_get_size(const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	size_t size;

	if (!priv->tdc_const)
		return 0;

	size = nla_total_size(0);			/* nest IFLA_CAN_TDC */
	size += nla_total_size(sizeof(u32));		/* IFLA_CAN_TDCV_MAX */
	size += nla_total_size(sizeof(u32));		/* IFLA_CAN_TDCO_MAX */
	size += nla_total_size(sizeof(u32));		/* IFLA_CAN_TDCF_MAX */

	if (priv->tdc.tdco) {
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCV */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCO */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCF */
	}

	return size;
}

static size_t can_get_size(const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	size_t size = 0;

	if (priv->bittiming.bitrate)				/* IFLA_CAN_BITTIMING */
		size += nla_total_size(sizeof(struct can_bittiming));
	if (priv->bittiming_const)				/* IFLA_CAN_BITTIMING_CONST */
		size += nla_total_size(sizeof(struct can_bittiming_const));
	size += nla_total_size(sizeof(struct can_clock));	/* IFLA_CAN_CLOCK */
	size += nla_total_size(sizeof(u32));			/* IFLA_CAN_STATE */
	size += nla_total_size(sizeof(struct can_ctrlmode));	/* IFLA_CAN_CTRLMODE */
	size += nla_total_size(sizeof(u32));			/* IFLA_CAN_RESTART_MS */
	if (priv->do_get_berr_counter)				/* IFLA_CAN_BERR_COUNTER */
		size += nla_total_size(sizeof(struct can_berr_counter));
	if (priv->data_bittiming.bitrate)			/* IFLA_CAN_DATA_BITTIMING */
		size += nla_total_size(sizeof(struct can_bittiming));
	if (priv->data_bittiming_const)				/* IFLA_CAN_DATA_BITTIMING_CONST */
		size += nla_total_size(sizeof(struct can_bittiming_const));
	if (priv->termination_const) {
		size += nla_total_size(sizeof(priv->termination));		/* IFLA_CAN_TERMINATION */
		size += nla_total_size(sizeof(*priv->termination_const) *	/* IFLA_CAN_TERMINATION_CONST */
				       priv->termination_const_cnt);
	}
	if (priv->bitrate_const)				/* IFLA_CAN_BITRATE_CONST */
		size += nla_total_size(sizeof(*priv->bitrate_const) *
				       priv->bitrate_const_cnt);
	if (priv->data_bitrate_const)				/* IFLA_CAN_DATA_BITRATE_CONST */
		size += nla_total_size(sizeof(*priv->data_bitrate_const) *
				       priv->data_bitrate_const_cnt);
	size += sizeof(priv->bitrate_max);			/* IFLA_CAN_BITRATE_MAX */
	size += can_tdc_get_size(dev);				/* IFLA_CAN_TDC */
	return size;
}

static int can_tdc_fill_info(struct sk_buff *skb, const struct net_device *dev)
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
	    nla_put_u32(skb, IFLA_CAN_TDC_TDCF_MAX, tdc_const->tdcf_max))
		goto err_cancel;

	if (priv->tdc.tdco)
		if (nla_put_u32(skb, IFLA_CAN_TDC_TDCV, tdc->tdcv) ||
		    nla_put_u32(skb, IFLA_CAN_TDC_TDCO, tdc->tdco) ||
		    nla_put_u32(skb, IFLA_CAN_TDC_TDCF, tdc->tdcf))
			goto err_cancel;

	nla_nest_end(skb, nest);
	return 0;

err_cancel:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int can_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	struct can_ctrlmode cm = {.flags = priv->ctrlmode};
	struct can_berr_counter bec = { };
	enum can_state state = priv->state;

	if (priv->do_get_state)
		priv->do_get_state(dev, &state);

	if ((priv->bittiming.bitrate &&
	     nla_put(skb, IFLA_CAN_BITTIMING,
		     sizeof(priv->bittiming), &priv->bittiming)) ||

	    (priv->bittiming_const &&
	     nla_put(skb, IFLA_CAN_BITTIMING_CONST,
		     sizeof(*priv->bittiming_const), priv->bittiming_const)) ||

	    nla_put(skb, IFLA_CAN_CLOCK, sizeof(priv->clock), &priv->clock) ||
	    nla_put_u32(skb, IFLA_CAN_STATE, state) ||
	    nla_put(skb, IFLA_CAN_CTRLMODE, sizeof(cm), &cm) ||
	    nla_put_u32(skb, IFLA_CAN_RESTART_MS, priv->restart_ms) ||

	    (priv->do_get_berr_counter &&
	     !priv->do_get_berr_counter(dev, &bec) &&
	     nla_put(skb, IFLA_CAN_BERR_COUNTER, sizeof(bec), &bec)) ||

	    (priv->data_bittiming.bitrate &&
	     nla_put(skb, IFLA_CAN_DATA_BITTIMING,
		     sizeof(priv->data_bittiming), &priv->data_bittiming)) ||

	    (priv->data_bittiming_const &&
	     nla_put(skb, IFLA_CAN_DATA_BITTIMING_CONST,
		     sizeof(*priv->data_bittiming_const),
		     priv->data_bittiming_const)) ||

	    (priv->termination_const &&
	     (nla_put_u16(skb, IFLA_CAN_TERMINATION, priv->termination) ||
	      nla_put(skb, IFLA_CAN_TERMINATION_CONST,
		      sizeof(*priv->termination_const) *
		      priv->termination_const_cnt,
		      priv->termination_const))) ||

	    (priv->bitrate_const &&
	     nla_put(skb, IFLA_CAN_BITRATE_CONST,
		     sizeof(*priv->bitrate_const) *
		     priv->bitrate_const_cnt,
		     priv->bitrate_const)) ||

	    (priv->data_bitrate_const &&
	     nla_put(skb, IFLA_CAN_DATA_BITRATE_CONST,
		     sizeof(*priv->data_bitrate_const) *
		     priv->data_bitrate_const_cnt,
		     priv->data_bitrate_const)) ||

	    (nla_put(skb, IFLA_CAN_BITRATE_MAX,
		     sizeof(priv->bitrate_max),
		     &priv->bitrate_max)) ||

	    (can_tdc_fill_info(skb, dev))
	    )

		return -EMSGSIZE;

	return 0;
}

static size_t can_get_xstats_size(const struct net_device *dev)
{
	return sizeof(struct can_device_stats);
}

static int can_fill_xstats(struct sk_buff *skb, const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	if (nla_put(skb, IFLA_INFO_XSTATS,
		    sizeof(priv->can_stats), &priv->can_stats))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int can_newlink(struct net *src_net, struct net_device *dev,
		       struct nlattr *tb[], struct nlattr *data[],
		       struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static void can_dellink(struct net_device *dev, struct list_head *head)
{
}

struct rtnl_link_ops can_link_ops __read_mostly = {
	.kind		= "can",
	.netns_refund	= true,
	.maxtype	= IFLA_CAN_MAX,
	.policy		= can_policy,
	.setup		= can_setup,
	.validate	= can_validate,
	.newlink	= can_newlink,
	.changelink	= can_changelink,
	.dellink	= can_dellink,
	.get_size	= can_get_size,
	.fill_info	= can_fill_info,
	.get_xstats_size = can_get_xstats_size,
	.fill_xstats	= can_fill_xstats,
};

int can_netlink_register(void)
{
	return rtnl_link_register(&can_link_ops);
}

void can_netlink_unregister(void)
{
	rtnl_link_unregister(&can_link_ops);
}
