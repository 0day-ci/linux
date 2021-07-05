// SPDX-License-Identifier: GPL-2.0-only
/* IEI WT61P803 PUZZLE MCU LED Driver
 *
 * Copyright (C) 2020 Sartura Ltd.
 * Author: Luka Kovacic <luka.kovacic@sartura.hr>
 */

#include <linux/leds.h>
#include <linux/mfd/iei-wt61p803-puzzle.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

enum iei_wt61p803_puzzle_led_state {
	IEI_LED_OFF = 0x30,
	IEI_LED_ON = 0x31,
	IEI_LED_BLINK_5HZ = 0x32,
	IEI_LED_BLINK_1HZ = 0x33,
};

/**
 * struct iei_wt61p803_puzzle_led - MCU LED Driver
 * @cdev:		LED classdev
 * @mcu:		MCU struct pointer
 * @response_buffer	Global MCU response buffer
 * @lock:		General mutex lock to protect simultaneous R/W access to led_power_state
 * @led_power_state:	State of the front panel power LED
 */
struct iei_wt61p803_puzzle_led {
	struct led_classdev cdev;
	struct iei_wt61p803_puzzle *mcu;
	unsigned char response_buffer[IEI_WT61P803_PUZZLE_BUF_SIZE];
	struct mutex lock /* mutex to protect led_power_state */;
	int led_power_state;
};

static inline struct iei_wt61p803_puzzle_led *cdev_to_iei_wt61p803_puzzle_led
	(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct iei_wt61p803_puzzle_led, cdev);
}

static int iei_wt61p803_puzzle_led_brightness_set_blocking(struct led_classdev *cdev,
							   enum led_brightness brightness)
{
	struct iei_wt61p803_puzzle_led *priv = cdev_to_iei_wt61p803_puzzle_led(cdev);
	unsigned char *resp_buf = priv->response_buffer;
	unsigned char led_power_cmd[5] = {};
	size_t reply_size;
	int ret;

	led_power_cmd[0] = IEI_WT61P803_PUZZLE_CMD_HEADER_START;
	led_power_cmd[1] = IEI_WT61P803_PUZZLE_CMD_LED;
	led_power_cmd[2] = IEI_WT61P803_PUZZLE_CMD_LED_POWER;
	led_power_cmd[3] = brightness == LED_OFF ? IEI_LED_OFF : IEI_LED_ON;

	ret = iei_wt61p803_puzzle_write_command(priv->mcu, led_power_cmd,
						sizeof(led_power_cmd),
						resp_buf,
						&reply_size);
	if (ret)
		return ret;

	if (reply_size != 3)
		return -EIO;

	if (!(resp_buf[0] == IEI_WT61P803_PUZZLE_CMD_HEADER_START &&
	      resp_buf[1] == IEI_WT61P803_PUZZLE_CMD_RESPONSE_OK &&
	      resp_buf[2] == IEI_WT61P803_PUZZLE_CHECKSUM_RESPONSE_OK))
		return -EIO;

	mutex_lock(&priv->lock);
	priv->led_power_state = brightness;
	mutex_unlock(&priv->lock);

	return 0;
}

static enum led_brightness iei_wt61p803_puzzle_led_brightness_get(struct led_classdev *cdev)
{
	struct iei_wt61p803_puzzle_led *priv = cdev_to_iei_wt61p803_puzzle_led(cdev);
	int led_state;

	mutex_lock(&priv->lock);
	led_state = priv->led_power_state;
	mutex_unlock(&priv->lock);

	return led_state;
}

static int iei_wt61p803_puzzle_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iei_wt61p803_puzzle *mcu = dev_get_drvdata(dev->parent);
	struct iei_wt61p803_puzzle_led *priv;
	struct led_init_data init_data = {};
	struct fwnode_handle *child;
	int ret;
	u32 reg;

	if (device_get_child_node_count(dev) != 1)
		return -EINVAL;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mcu = mcu;
	priv->led_power_state = 1;
	mutex_init(&priv->lock);
	dev_set_drvdata(dev, priv);

	child = device_get_next_child_node(dev, NULL);
	ret = fwnode_property_read_u32(child, "reg", &reg);
	if (ret || reg > 1) {
		dev_err(dev, "Could not register 'reg' (%u)\n", reg);
		ret = -EINVAL;
		goto err_child_node;
	}

	priv->cdev.brightness_set_blocking = iei_wt61p803_puzzle_led_brightness_set_blocking;
	priv->cdev.brightness_get = iei_wt61p803_puzzle_led_brightness_get;
	priv->cdev.max_brightness = 1;
	init_data.fwnode = child;

	ret = devm_led_classdev_register_ext(dev, &priv->cdev, &init_data);
	if (ret) {
		dev_err(dev, "Could not register LED\n");
		goto err_child_node;
	}
err_child_node:
	fwnode_handle_put(child);
	return ret;
}

static const struct of_device_id iei_wt61p803_puzzle_led_of_match[] = {
	{ .compatible = "iei,wt61p803-puzzle-leds" },
	{ }
};
MODULE_DEVICE_TABLE(of, iei_wt61p803_puzzle_led_of_match);

static struct platform_driver iei_wt61p803_puzzle_led_driver = {
	.driver = {
		.name = "iei-wt61p803-puzzle-led",
		.of_match_table = iei_wt61p803_puzzle_led_of_match,
	},
	.probe = iei_wt61p803_puzzle_led_probe,
};
module_platform_driver(iei_wt61p803_puzzle_led_driver);

MODULE_DESCRIPTION("IEI WT61P803 PUZZLE front panel LED driver");
MODULE_AUTHOR("Luka Kovacic <luka.kovacic@sartura.hr>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-iei-wt61p803-puzzle");
