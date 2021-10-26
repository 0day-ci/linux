// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI USB Power Delivery Device
 *
 * Copyright (C) 2021, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include "ucsi.h"

static struct ucsi_connector *pd_dev_to_connector(const struct pd_dev *dev);

static int ucsi_pd_get_objects(const struct pd_dev *dev, struct pd_message *msg)
{
	struct ucsi_connector *con = pd_dev_to_connector(dev);
	int partner = dev == &con->pd_partner_dev;
	int ret = -ENOTTY;

	mutex_lock(&con->lock);

	if (le16_to_cpu(msg->header) & PD_HEADER_EXT_HDR)
		goto err;

	switch (pd_header_type_le(msg->header)) {
	case PD_DATA_SOURCE_CAP:
		ret = ucsi_read_pdos(con, partner, 1, msg->payload);
		if (ret < 0)
			goto err;

		msg->header = PD_HEADER_LE(PD_DATA_SOURCE_CAP, 0, 0, 0, 0, ret);
		break;
	case PD_DATA_REQUEST:
		msg->header = PD_HEADER_LE(PD_DATA_REQUEST, 0, 0, 0, 0, 1);
		msg->payload[0] = con->status.request_data_obj;
		break;
	case PD_DATA_SINK_CAP:
		ret = ucsi_read_pdos(con, partner, 0, msg->payload);
		if (ret < 0)
			goto err;

		msg->header = PD_HEADER_LE(PD_DATA_SINK_CAP, 0, 0, 0, 0, ret);
		break;
	default:
		goto err;
	}

	ret = 0;
err:
	mutex_unlock(&con->lock);

	return ret;
}

/*
 * This function is here just as an example for now.
 */
static int ucsi_pd_submit(const struct pd_dev *dev, struct pd_message *msg)
{
	struct ucsi_connector *con = pd_dev_to_connector(dev);
	int ret;

	mutex_lock(&con->lock);

	switch (pd_header_type_le(msg->header)) {
	case PD_CTRL_GET_SOURCE_CAP:
		ret = ucsi_read_pdos(con, 1, 1, msg->payload);
		if (ret < 0)
			goto err;

		msg->header = PD_HEADER_LE(PD_DATA_SOURCE_CAP, 0, 0, 0, 0, ret);
		break;
	case PD_CTRL_GET_SINK_CAP:
		ret = ucsi_read_pdos(con, 1, 0, msg->payload);
		if (ret < 0)
			goto err;

		msg->header = PD_HEADER_LE(PD_DATA_SINK_CAP, 0, 0, 0, 0, ret);
		break;
	default:
		ret = -ENOTTY;
		goto err;
	}

	ret = 0;
err:
	mutex_unlock(&con->lock);

	return ret;
}

static const struct pd_ops ucsi_pd_partner_ops = {
	.get_message = ucsi_pd_get_objects,
	.submit = ucsi_pd_submit,
};

static struct pd_info ucsi_pd_partner_info = {
	.specification_revision = 2,
	.ctrl_msgs_supported = BIT(PD_CTRL_GET_SOURCE_CAP) |
			       BIT(PD_CTRL_GET_SINK_CAP),
};

static const struct pd_ops ucsi_pd_port_ops = {
	.get_message = ucsi_pd_get_objects,
};

static struct pd_info ucsi_pd_port_info = {
	.specification_revision = 2,
};

static struct ucsi_connector *pd_dev_to_connector(const struct pd_dev *dev)
{
	if (dev->info == &ucsi_pd_port_info)
		return container_of(dev, struct ucsi_connector, pd_port_dev);
	if (dev->info == &ucsi_pd_partner_info)
		return container_of(dev, struct ucsi_connector, pd_partner_dev);
	return NULL;
}

void ucsi_init_pd_dev(struct ucsi_connector *con)
{
	con->pd_port_dev.info = &ucsi_pd_port_info;
	con->pd_port_dev.ops = &ucsi_pd_port_ops;
	con->pd_partner_dev.info = &ucsi_pd_partner_info;
	con->pd_partner_dev.ops = &ucsi_pd_partner_ops;
}
