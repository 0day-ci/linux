// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>
#include "netlink.h"
#include "common.h"

struct eeprom_data_req_info {
	struct ethnl_req_info	base;
	u32			offset;
	u32			length;
	u8			page;
	u8			bank;
	u8			i2c_address;
};

struct eeprom_data_reply_data {
	struct ethnl_reply_data base;
	u32			length;
	u8			*data;
};

#define EEPROM_DATA_REQINFO(__req_base) \
	container_of(__req_base, struct eeprom_data_req_info, base)

#define EEPROM_DATA_REPDATA(__reply_base) \
	container_of(__reply_base, struct eeprom_data_reply_data, base)

static int eeprom_data_prepare_data(const struct ethnl_req_info *req_base,
				    struct ethnl_reply_data *reply_base,
				    struct genl_info *info)
{
	struct eeprom_data_reply_data *reply = EEPROM_DATA_REPDATA(reply_base);
	struct eeprom_data_req_info *request = EEPROM_DATA_REQINFO(req_base);
	struct ethtool_eeprom_data page_data = {0};
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_module_eeprom_data_by_page)
		return -EOPNOTSUPP;

	page_data.offset = request->offset;
	page_data.length = request->length;
	page_data.i2c_address = request->i2c_address;
	page_data.page = request->page;
	page_data.bank = request->bank;
	page_data.data = kmalloc(page_data.length, GFP_KERNEL);
	if (!page_data.data)
		return -ENOMEM;
	ret = ethnl_ops_begin(dev);
	if (ret)
		goto err_free;

	ret = dev->ethtool_ops->get_module_eeprom_data_by_page(dev, &page_data,
							       info->extack);
	if (ret < 0)
		goto err_ops;

	reply->length = ret;
	reply->data = page_data.data;

	ethnl_ops_complete(dev);
	return 0;

err_ops:
	ethnl_ops_complete(dev);
err_free:
	kfree(page_data.data);
	return ret;
}

static int eeprom_data_parse_request(struct ethnl_req_info *req_info, struct nlattr **tb,
				     struct netlink_ext_ack *extack)
{
	struct eeprom_data_req_info *request = EEPROM_DATA_REQINFO(req_info);
	struct net_device *dev = req_info->dev;

	if (!tb[ETHTOOL_A_EEPROM_DATA_OFFSET] ||
	    !tb[ETHTOOL_A_EEPROM_DATA_LENGTH] ||
	    !tb[ETHTOOL_A_EEPROM_DATA_I2C_ADDRESS])
		return -EINVAL;

	request->i2c_address = nla_get_u8(tb[ETHTOOL_A_EEPROM_DATA_I2C_ADDRESS]);
	if (request->i2c_address > ETH_MODULE_MAX_I2C_ADDRESS)
		return -EINVAL;

	request->offset = nla_get_u32(tb[ETHTOOL_A_EEPROM_DATA_OFFSET]);
	request->length = nla_get_u32(tb[ETHTOOL_A_EEPROM_DATA_LENGTH]);
	if (tb[ETHTOOL_A_EEPROM_DATA_PAGE] &&
	    dev->ethtool_ops->get_module_eeprom_data_by_page &&
	    request->offset + request->length > ETH_MODULE_EEPROM_PAGE_LEN)
		return -EINVAL;

	if (tb[ETHTOOL_A_EEPROM_DATA_PAGE])
		request->page = nla_get_u8(tb[ETHTOOL_A_EEPROM_DATA_PAGE]);
	if (tb[ETHTOOL_A_EEPROM_DATA_BANK])
		request->bank = nla_get_u8(tb[ETHTOOL_A_EEPROM_DATA_BANK]);

	return 0;
}

static int eeprom_data_reply_size(const struct ethnl_req_info *req_base,
				  const struct ethnl_reply_data *reply_base)
{
	const struct eeprom_data_req_info *request = EEPROM_DATA_REQINFO(req_base);

	return nla_total_size(sizeof(u32)) + /* _EEPROM_DATA_LENGTH */
	       nla_total_size(sizeof(u8) * request->length); /* _EEPROM_DATA */
}

static int eeprom_data_fill_reply(struct sk_buff *skb,
				  const struct ethnl_req_info *req_base,
				  const struct ethnl_reply_data *reply_base)
{
	struct eeprom_data_reply_data *reply = EEPROM_DATA_REPDATA(reply_base);

	if (nla_put_u32(skb, ETHTOOL_A_EEPROM_DATA_LENGTH, reply->length) ||
	    nla_put(skb, ETHTOOL_A_EEPROM_DATA, reply->length, reply->data))
		return -EMSGSIZE;

	return 0;
}

static void eeprom_data_cleanup_data(struct ethnl_reply_data *reply_base)
{
	struct eeprom_data_reply_data *reply = EEPROM_DATA_REPDATA(reply_base);

	kfree(reply->data);
}

const struct ethnl_request_ops ethnl_eeprom_data_request_ops = {
	.request_cmd		= ETHTOOL_MSG_EEPROM_DATA_GET,
	.reply_cmd		= ETHTOOL_MSG_EEPROM_DATA_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_EEPROM_DATA_HEADER,
	.req_info_size		= sizeof(struct eeprom_data_req_info),
	.reply_data_size	= sizeof(struct eeprom_data_reply_data),

	.parse_request		= eeprom_data_parse_request,
	.prepare_data		= eeprom_data_prepare_data,
	.reply_size		= eeprom_data_reply_size,
	.fill_reply		= eeprom_data_fill_reply,
	.cleanup_data		= eeprom_data_cleanup_data,
};

const struct nla_policy ethnl_eeprom_data_get_policy[] = {
	[ETHTOOL_A_EEPROM_DATA_HEADER]		= NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_EEPROM_DATA_OFFSET]		= { .type = NLA_U32 },
	[ETHTOOL_A_EEPROM_DATA_LENGTH]		= { .type = NLA_U32 },
	[ETHTOOL_A_EEPROM_DATA_PAGE]		= { .type = NLA_U8 },
	[ETHTOOL_A_EEPROM_DATA_BANK]		= { .type = NLA_U8 },
	[ETHTOOL_A_EEPROM_DATA_I2C_ADDRESS]	= { .type = NLA_U8 },
	[ETHTOOL_A_EEPROM_DATA]			= { .type = NLA_BINARY },
};

