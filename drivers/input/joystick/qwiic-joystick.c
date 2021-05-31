// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2021 Oleh Kravchenko <oleg@kaa.org.ua>

/*
 * SparkFun Qwiic Joystick
 * Product page:https://www.sparkfun.com/products/15168
 * Firmware and hardware sources:https://github.com/sparkfun/Qwiic_Joystick
 */

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/module.h>

#define QWIIC_JSK_REG_VERS	0
#define QWIIC_JSK_REG_DATA	3

#define QWIIC_JSK_MAX_AXIS	GENMASK(10, 0)
#define QWIIC_JSK_FUZZ		2
#define QWIIC_JSK_FLAT		2

struct qwiic_jsk {
	char			phys[32];
	struct input_dev	*dev;
	struct i2c_client	*i2c;
};

struct qwiic_ver {
	u8 addr;
	u8 major;
	u8 minor;
} __packed;

struct qwiic_data {
	u8 hx;
	u8 lx;
	u8 hy;
	u8 ly;
	u8 thumb;
} __packed;

static void qwiic_poll(struct input_dev *input)
{
	struct qwiic_jsk	*priv;
	struct qwiic_data	data;
	int			ret;
	int			x, y, btn;

	priv = input_get_drvdata(input);

	ret = i2c_smbus_read_i2c_block_data(priv->i2c, QWIIC_JSK_REG_DATA,
					    sizeof(data), (u8 *)&data);
	if (ret == sizeof(data)) {
		x = (data.hx << 8 | data.lx) >> 6;
		y = (data.hy << 8 | data.ly) >> 6;
		btn = !!!data.thumb;

		input_report_abs(input, ABS_X, x);
		input_report_abs(input, ABS_Y, y);
		input_report_key(input, BTN_THUMBL, btn);

		input_sync(input);
	}
}

static int qwiic_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct qwiic_jsk	*priv;
	struct qwiic_ver	vers;
	int			ret;

	ret = i2c_smbus_read_i2c_block_data(i2c, QWIIC_JSK_REG_VERS,
					    sizeof(vers), (u8 *)&vers);
	if (ret != sizeof(vers)) {
		ret = -EIO;
		goto err;
	}

	if (i2c->addr != vers.addr) {
		dev_err(&i2c->dev, "address doesn't match!\n");
		ret = -ENODEV;
		goto err;
	}

	dev_info(&i2c->dev, "SparkFun Qwiic Joystick, FW: %d.%d\n",
		 vers.major, vers.minor);

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err;
	}

	priv->i2c = i2c;
	snprintf(priv->phys, sizeof(priv->phys), "i2c/%s", dev_name(&i2c->dev));
	i2c_set_clientdata(i2c, priv);

	priv->dev = devm_input_allocate_device(&i2c->dev);
	if (!priv->dev) {
		dev_err(&i2c->dev, "failed to allocate input device\n");
		ret = -ENOMEM;
		goto err;
	}

	priv->dev->dev.parent = &i2c->dev;
	priv->dev->id.bustype = BUS_I2C;
	priv->dev->name = "SparkFun Qwiic Joystick";
	priv->dev->phys = priv->phys;
	input_set_drvdata(priv->dev, priv);

	input_set_abs_params(priv->dev, ABS_X, 0, QWIIC_JSK_MAX_AXIS,
			     QWIIC_JSK_FUZZ, QWIIC_JSK_FLAT);
	input_set_abs_params(priv->dev, ABS_Y, 0, QWIIC_JSK_MAX_AXIS,
			     QWIIC_JSK_FUZZ, QWIIC_JSK_FLAT);
	input_set_capability(priv->dev, EV_KEY, BTN_THUMBL);

	ret = input_setup_polling(priv->dev, qwiic_poll);
	if (ret) {
		dev_err(&i2c->dev, "failed to set up polling: %d\n", ret);
		goto err;
	}
	input_set_poll_interval(priv->dev, 16);
	input_set_min_poll_interval(priv->dev, 8);
	input_set_max_poll_interval(priv->dev, 32);

	ret = input_register_device(priv->dev);
	if (ret)
		dev_err(&i2c->dev, "failed to register joystick: %d\n", ret);

err:
	return ret;
}

static int qwiic_remove(struct i2c_client *i2c)
{
	struct qwiic_jsk *priv;

	priv = i2c_get_clientdata(i2c);
	input_unregister_device(priv->dev);

	return 0;
}

static const struct of_device_id of_qwiic_match[] = {
	{ .compatible = "sparkfun,qwiic-joystick", },
	{},
};
MODULE_DEVICE_TABLE(of, of_qwiic_match);

static const struct i2c_device_id qwiic_id_table[] = {
	{ KBUILD_MODNAME, 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, qwiic_id_table);

static struct i2c_driver qwiic_driver = {
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= of_match_ptr(of_qwiic_match),
	},
	.id_table	= qwiic_id_table,
	.probe		= qwiic_probe,
	.remove		= qwiic_remove,
};
module_i2c_driver(qwiic_driver);

MODULE_AUTHOR("Oleh Kravchenko <oleg@kaa.org.ua>");
MODULE_DESCRIPTION("SparkFun Qwiic Joystick driver");
MODULE_LICENSE("GPL v2");
