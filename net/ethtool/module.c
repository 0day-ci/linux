// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct module_req_info {
	struct ethnl_req_info		base;
};

struct module_reply_data {
	struct ethnl_reply_data		base;
	u8 low_power:1,
	   low_power_valid:1;
};

#define MODULE_REPDATA(__reply_base) \
	container_of(__reply_base, struct module_reply_data, base)

/* MODULE_GET */

const struct nla_policy ethnl_module_get_policy[ETHTOOL_A_MODULE_HEADER + 1] = {
	[ETHTOOL_A_MODULE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
};

static int module_get_low_power(struct net_device *dev,
				struct module_reply_data *data,
				struct netlink_ext_ack *extack)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	bool low_power;
	int ret;

	if (!ops->get_module_low_power)
		return 0;

	ret = ops->get_module_low_power(dev, &low_power, extack);
	if (ret < 0)
		return ret;

	data->low_power = low_power;
	data->low_power_valid = true;

	return 0;
}

static int module_prepare_data(const struct ethnl_req_info *req_base,
			       struct ethnl_reply_data *reply_base,
			       struct genl_info *info)
{
	struct module_reply_data *data = MODULE_REPDATA(reply_base);
	struct netlink_ext_ack *extack = info ? info->extack : NULL;
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	ret = module_get_low_power(dev, data, extack);
	if (ret < 0)
		goto out_complete;

out_complete:
	ethnl_ops_complete(dev);
	return ret;
}

static int module_reply_size(const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	struct module_reply_data *data = MODULE_REPDATA(reply_base);
	int len = 0;

	if (data->low_power_valid)
		len += nla_total_size(sizeof(u8)); /* _MODULE_LOW_POWER_ENABLED */

	return len;
}

static int module_fill_reply(struct sk_buff *skb,
			     const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	const struct module_reply_data *data = MODULE_REPDATA(reply_base);

	if (data->low_power_valid &&
	    nla_put_u8(skb, ETHTOOL_A_MODULE_LOW_POWER_ENABLED,
		       data->low_power))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_module_request_ops = {
	.request_cmd		= ETHTOOL_MSG_MODULE_GET,
	.reply_cmd		= ETHTOOL_MSG_MODULE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_MODULE_HEADER,
	.req_info_size		= sizeof(struct module_req_info),
	.reply_data_size	= sizeof(struct module_reply_data),

	.prepare_data		= module_prepare_data,
	.reply_size		= module_reply_size,
	.fill_reply		= module_fill_reply,
};

/* MODULE_SET */

const struct nla_policy ethnl_module_set_policy[ETHTOOL_A_MODULE_LOW_POWER_ENABLED + 1] = {
	[ETHTOOL_A_MODULE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_MODULE_LOW_POWER_ENABLED] = NLA_POLICY_MAX(NLA_U8, 1),
};

static int module_set_low_power(struct net_device *dev, struct nlattr **tb,
				bool *p_mod, struct netlink_ext_ack *extack)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	bool low_power_new, low_power;
	int ret;

	if (!tb[ETHTOOL_A_MODULE_LOW_POWER_ENABLED])
		return 0;

	if (!ops->get_module_low_power || !ops->set_module_low_power) {
		NL_SET_ERR_MSG_ATTR(extack,
				    tb[ETHTOOL_A_MODULE_LOW_POWER_ENABLED],
				    "Setting low power mode is not supported by this device");
		return -EOPNOTSUPP;
	}

	if (netif_running(dev)) {
		NL_SET_ERR_MSG_ATTR(extack,
				    tb[ETHTOOL_A_MODULE_LOW_POWER_ENABLED],
				    "Cannot set low power mode when port is administratively up");
		return -EINVAL;
	}

	low_power_new = !!nla_get_u8(tb[ETHTOOL_A_MODULE_LOW_POWER_ENABLED]);
	ret = ops->get_module_low_power(dev, &low_power, extack);
	if (ret < 0)
		return ret;
	*p_mod = low_power_new != low_power;

	return ops->set_module_low_power(dev, low_power_new, extack);
}

int ethnl_set_module(struct sk_buff *skb, struct genl_info *info)
{
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	struct net_device *dev;
	bool mod = false;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info, tb[ETHTOOL_A_MODULE_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = module_set_low_power(dev, tb, &mod, info->extack);
	if (ret < 0)
		goto out_ops;

	if (!mod)
		goto out_ops;

	ethtool_notify(dev, ETHTOOL_MSG_MODULE_NTF, NULL);

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}

/* MODULE_RESET_ACT */

const struct nla_policy ethnl_module_reset_act_policy[ETHTOOL_A_MODULE_HEADER + 1] = {
	[ETHTOOL_A_MODULE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
};

static void ethnl_module_reset_done(struct net_device *dev)
{
	struct sk_buff *skb;
	void *ehdr;
	int ret;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return;

	ehdr = ethnl_bcastmsg_put(skb, ETHTOOL_MSG_MODULE_RESET_NTF);
	if (!ehdr)
		goto out;

	ret = ethnl_fill_reply_header(skb, dev, ETHTOOL_A_MODULE_HEADER);
	if (ret < 0)
		goto out;

	genlmsg_end(skb, ehdr);
	ethnl_multicast(skb, dev);
	return;

out:
	nlmsg_free(skb);
}

int ethnl_act_module_reset(struct sk_buff *skb, struct genl_info *info)
{
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	const struct ethtool_ops *ops;
	struct net_device *dev;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_MODULE_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;

	dev = req_info.dev;

	rtnl_lock();
	ops = dev->ethtool_ops;
	if (!ops->reset_module) {
		ret = -EOPNOTSUPP;
		goto out_rtnl;
	}

	if (netif_running(dev)) {
		NL_SET_ERR_MSG(info->extack,
			       "Cannot reset module when port is administratively up");
		ret = -EINVAL;
		goto out_rtnl;
	}

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = ops->reset_module(dev, info->extack);

	ethnl_ops_complete(dev);

	if (!ret)
		ethnl_module_reset_done(dev);

out_rtnl:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}
