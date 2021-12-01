// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>
#include "netlink.h"

struct rclk_out_pin_info {
	u32 idx;
	bool valid;
};

struct rclk_request_data {
	struct ethnl_req_info base;
	struct rclk_out_pin_info out_pin;
};

struct rclk_pin_state_info {
	u32 range_min;
	u32 range_max;
	u32 flags;
	u32 idx;
};

struct rclk_reply_data {
	struct ethnl_reply_data	base;
	struct rclk_pin_state_info pin_state;
};

#define RCLK_REPDATA(__reply_base) \
	container_of(__reply_base, struct rclk_reply_data, base)

#define RCLK_REQDATA(__req_base) \
	container_of(__req_base, struct rclk_request_data, base)

/* RCLK_GET */

const struct nla_policy
ethnl_rclk_get_policy[ETHTOOL_A_RCLK_OUT_PIN_IDX + 1] = {
	[ETHTOOL_A_RCLK_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RCLK_OUT_PIN_IDX] = { .type = NLA_U32 },
};

static int rclk_parse_request(struct ethnl_req_info *req_base,
			      struct nlattr **tb,
			      struct netlink_ext_ack *extack)
{
	struct rclk_request_data *req = RCLK_REQDATA(req_base);

	if (tb[ETHTOOL_A_RCLK_OUT_PIN_IDX]) {
		req->out_pin.idx = nla_get_u32(tb[ETHTOOL_A_RCLK_OUT_PIN_IDX]);
		req->out_pin.valid = true;
	}

	return 0;
}

static int rclk_state_get(struct net_device *dev,
			  struct rclk_reply_data *data,
			  struct netlink_ext_ack *extack,
			  u32 out_idx)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	bool pin_state;
	int ret;

	if (!ops->get_rclk_state)
		return -EOPNOTSUPP;

	ret = ops->get_rclk_state(dev, out_idx, &pin_state, extack);
	if (ret)
		return ret;

	data->pin_state.flags = pin_state ? ETHTOOL_RCLK_PIN_FLAGS_ENA : 0;
	data->pin_state.idx = out_idx;

	return ret;
}

static int rclk_range_get(struct net_device *dev,
			  struct rclk_reply_data *data,
			  struct netlink_ext_ack *extack)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u32 min_idx, max_idx;
	int ret;

	if (!ops->get_rclk_range)
		return -EOPNOTSUPP;

	ret = ops->get_rclk_range(dev, &min_idx, &max_idx, extack);
	if (ret)
		return ret;

	data->pin_state.range_min = min_idx;
	data->pin_state.range_max = max_idx;

	return ret;
}

static int rclk_prepare_data(const struct ethnl_req_info *req_base,
			     struct ethnl_reply_data *reply_base,
			     struct genl_info *info)
{
	struct rclk_reply_data *reply = RCLK_REPDATA(reply_base);
	struct rclk_request_data *request = RCLK_REQDATA(req_base);
	struct netlink_ext_ack *extack = info ? info->extack : NULL;
	struct net_device *dev = reply_base->dev;
	int ret;

	memset(&reply->pin_state, 0, sizeof(reply->pin_state));
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	if (request->out_pin.valid)
		ret = rclk_state_get(dev, reply, extack,
				     request->out_pin.idx);
	else
		ret = rclk_range_get(dev, reply, extack);

	ethnl_ops_complete(dev);

	return ret;
}

static int rclk_fill_reply(struct sk_buff *skb,
			   const struct ethnl_req_info *req_base,
			   const struct ethnl_reply_data *reply_base)
{
	const struct rclk_reply_data *reply = RCLK_REPDATA(reply_base);
	const struct rclk_request_data *request = RCLK_REQDATA(req_base);

	if (request->out_pin.valid) {
		if (nla_put_u32(skb, ETHTOOL_A_RCLK_PIN_FLAGS,
				reply->pin_state.flags))
			return -EMSGSIZE;
		if (nla_put_u32(skb, ETHTOOL_A_RCLK_OUT_PIN_IDX,
				reply->pin_state.idx))
			return -EMSGSIZE;
	} else {
		if (nla_put_u32(skb, ETHTOOL_A_RCLK_PIN_MIN,
				reply->pin_state.range_min))
			return -EMSGSIZE;
		if (nla_put_u32(skb, ETHTOOL_A_RCLK_PIN_MAX,
				reply->pin_state.range_max))
			return -EMSGSIZE;
	}

	return 0;
}

static int rclk_reply_size(const struct ethnl_req_info *req_base,
			   const struct ethnl_reply_data *reply_base)
{
	const struct rclk_request_data *request = RCLK_REQDATA(req_base);

	if (request->out_pin.valid)
		return nla_total_size(sizeof(u32)) +	/* ETHTOOL_A_RCLK_PIN_FLAGS */
		       nla_total_size(sizeof(u32));	/* ETHTOOL_A_RCLK_OUT_PIN_IDX */
	else
		return nla_total_size(sizeof(u32)) +	/* ETHTOOL_A_RCLK_PIN_MIN */
		       nla_total_size(sizeof(u32));	/* ETHTOOL_A_RCLK_PIN_MAX */
}

const struct ethnl_request_ops ethnl_rclk_request_ops = {
	.request_cmd		= ETHTOOL_MSG_RCLK_GET,
	.reply_cmd		= ETHTOOL_MSG_RCLK_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_RCLK_HEADER,
	.req_info_size		= sizeof(struct rclk_request_data),
	.reply_data_size	= sizeof(struct rclk_reply_data),

	.parse_request		= rclk_parse_request,
	.prepare_data		= rclk_prepare_data,
	.reply_size		= rclk_reply_size,
	.fill_reply		= rclk_fill_reply,
};

/* RCLK SET */

const struct nla_policy
ethnl_rclk_set_policy[ETHTOOL_A_RCLK_PIN_FLAGS + 1] = {
	[ETHTOOL_A_RCLK_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RCLK_OUT_PIN_IDX] = { .type = NLA_U32 },
	[ETHTOOL_A_RCLK_PIN_FLAGS] = { .type = NLA_U32 },
};

static int rclk_set_state(struct net_device *dev, struct nlattr **tb,
			  bool *p_mod, struct netlink_ext_ack *extack)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	bool old_state, new_state;
	u32 min_idx, max_idx;
	u32 out_idx;
	int ret;

	if (!tb[ETHTOOL_A_RCLK_PIN_FLAGS] &&
	    !tb[ETHTOOL_A_RCLK_OUT_PIN_IDX])
		return 0;

	if (!ops->set_rclk_out || !ops->get_rclk_range) {
		NL_SET_ERR_MSG_ATTR(extack,
				    tb[ETHTOOL_A_RCLK_PIN_FLAGS],
				    "Setting recovered clock state is not supported by this device");
		return -EOPNOTSUPP;
	}

	ret = ops->get_rclk_range(dev, &min_idx, &max_idx, extack);
	if (ret)
		return ret;

	out_idx = nla_get_u32(tb[ETHTOOL_A_RCLK_OUT_PIN_IDX]);
	if (out_idx < min_idx || out_idx > max_idx) {
		NL_SET_ERR_MSG_ATTR(extack,
				    tb[ETHTOOL_A_RCLK_OUT_PIN_IDX],
				    "Requested recovered clock pin index is out of range");
		return -EINVAL;
	}

	ret = ops->get_rclk_state(dev, out_idx, &old_state, extack);
	if (ret < 0)
		return ret;

	new_state = !!(nla_get_u32(tb[ETHTOOL_A_RCLK_PIN_FLAGS]) &
		       ETHTOOL_RCLK_PIN_FLAGS_ENA);

	/* If state changed - flag need for sending the notification */
	*p_mod = old_state != new_state;

	return ops->set_rclk_out(dev, out_idx, new_state, extack);
}

int ethnl_set_rclk(struct sk_buff *skb, struct genl_info *info)
{
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	struct net_device *dev;
	bool mod = false;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info, tb[ETHTOOL_A_RCLK_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = rclk_set_state(dev, tb, &mod, info->extack);
	if (ret < 0)
		goto out_ops;

	if (!mod)
		goto out_ops;

	ethtool_notify(dev, ETHTOOL_MSG_RCLK_NTF, NULL);

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}

