// SPDX-License-Identifier: GPL-2.0-only
/*
 * Delta TN48M CPLD parent driver
 *
 * Copyright 2020 Sartura Ltd
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 */

#include <linux/debugfs.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tn48m.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

static const struct mfd_cell tn48m_cell[] = {
	{
		.name = "delta,tn48m-gpio",
	}
};

static const struct regmap_config tn48m_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x40,
};

static int hardware_version_show(struct seq_file *s, void *data)
{
	struct tn48m_data *priv = s->private;
	unsigned int regval;
	char *buf;

	regmap_read(priv->regmap, HARDWARE_VERSION_ID, &regval);

	switch (FIELD_GET(HARDWARE_VERSION_MASK, regval)) {
	case HARDWARE_VERSION_EVT1:
		buf = "EVT1";
		break;
	case HARDWARE_VERSION_EVT2:
		buf = "EVT2";
		break;
	case HARDWARE_VERSION_DVT:
		buf = "DVT";
		break;
	case HARDWARE_VERSION_PVT:
		buf = "PVT";
		break;
	default:
		buf = "Unknown";
		break;
	}

	seq_printf(s, "%s\n", buf);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(hardware_version);

static int board_id_show(struct seq_file *s, void *data)
{
	struct tn48m_data *priv = s->private;
	unsigned int regval;
	char *buf;

	regmap_read(priv->regmap, BOARD_ID, &regval);

	switch (regval) {
	case BOARD_ID_TN48M:
		buf = "TN48M";
		break;
	case BOARD_ID_TN48M_P:
		buf = "TN48-P";
		break;
	default:
		buf = "Unknown";
		break;
	}

	seq_printf(s, "%s\n", buf);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(board_id);

static int code_version_show(struct seq_file *s, void *data)
{
	struct tn48m_data *priv = s->private;
	unsigned int regval;

	regmap_read(priv->regmap, CPLD_CODE_VERSION, &regval);

	seq_printf(s, "%d\n", regval);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(code_version);

static void tn48m_init_debugfs(struct tn48m_data *data)
{
	data->debugfs_dir = debugfs_create_dir(data->client->name, NULL);

	debugfs_create_file("hardware_version",
			    0400,
			    data->debugfs_dir,
			    data,
			    &hardware_version_fops);

	debugfs_create_file("board_id",
			    0400,
			    data->debugfs_dir,
			    data,
			    &board_id_fops);

	debugfs_create_file("code_version",
			    0400,
			    data->debugfs_dir,
			    data,
			    &code_version_fops);
}

static int tn48m_probe(struct i2c_client *client)
{
	struct tn48m_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->dev = &client->dev;
	i2c_set_clientdata(client, data);

	data->regmap = devm_regmap_init_i2c(client, &tn48m_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(data->dev, "Failed to allocate regmap\n");
		return PTR_ERR(data->regmap);
	}

	ret = devm_mfd_add_devices(data->dev, PLATFORM_DEVID_AUTO, tn48m_cell,
				   ARRAY_SIZE(tn48m_cell), NULL, 0, NULL);
	if (ret)
		dev_err(data->dev, "Failed to register sub-devices %d\n", ret);

	tn48m_init_debugfs(data);

	return ret;
}

static int tn48m_remove(struct i2c_client *client)
{
	struct tn48m_data *data = i2c_get_clientdata(client);

	debugfs_remove_recursive(data->debugfs_dir);

	return 0;
}

static const struct of_device_id tn48m_of_match[] = {
	{ .compatible = "delta,tn48m-cpld"},
	{ }
};
MODULE_DEVICE_TABLE(of, tn48m_of_match);

static struct i2c_driver tn48m_driver = {
	.driver = {
		.name = "tn48m-cpld",
		.of_match_table = tn48m_of_match,
	},
	.probe_new	= tn48m_probe,
	.remove		= tn48m_remove,
};
module_i2c_driver(tn48m_driver);

MODULE_AUTHOR("Robert Marko <robert.marko@sartura.hr>");
MODULE_DESCRIPTION("Delta TN48M CPLD parent driver");
MODULE_LICENSE("GPL");
