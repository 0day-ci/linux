// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>

static int rpmsg_syslog_cb(struct rpmsg_device *rpdev, void *data, int len,
			   void *priv, u32 src)
{
	const char *buffer = data;

	switch (buffer[0]) {
	case 'e':
		dev_err(&rpdev->dev, "%s", buffer + 1);
		break;
	case 'w':
		dev_warn(&rpdev->dev, "%s", buffer + 1);
		break;
	case 'i':
		dev_info(&rpdev->dev, "%s", buffer + 1);
		break;
	default:
		dev_info(&rpdev->dev, "%s", buffer);
		break;
	}

	return 0;
}

static int rpmsg_syslog_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_endpoint *syslog_ept;
	struct rpmsg_channel_info syslog_chinfo = {
		.src = 42,
		.dst = 42,
		.name = "syslog",
	};

	/*
	 * Create the syslog service endpoint associated to the RPMsg
	 * device. The endpoint will be automatically destroyed when the RPMsg
	 * device will be deleted.
	 */
	syslog_ept = rpmsg_create_ept(rpdev, rpmsg_syslog_cb, NULL, syslog_chinfo);
	if (!syslog_ept) {
		dev_err(&rpdev->dev, "failed to create the syslog ept\n");
		return -ENOMEM;
	}
	rpdev->ept = syslog_ept;

	return 0;
}

static struct rpmsg_device_id rpmsg_driver_syslog_id_table[] = {
	{ .name = "syslog" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_syslog_id_table);

static struct rpmsg_driver rpmsg_syslog_client = {
	.drv.name       = KBUILD_MODNAME,
	.id_table       = rpmsg_driver_syslog_id_table,
	.probe          = rpmsg_syslog_probe,
};
module_rpmsg_driver(rpmsg_syslog_client);
