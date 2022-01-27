// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the ChromeOS anti-snooping sensor (HPS), attached via I2C.
 *
 * The driver exposes HPS as a character device, although currently no read or
 * write operations are supported. Instead, the driver only controls the power
 * state of the sensor, keeping it on only while userspace holds an open file
 * descriptor to the HPS device.
 *
 * Copyright 2022 Google LLC.
 */

#include <linux/acpi.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#define HPS_ACPI_ID		"GOOG0020"
#define HPS_MAX_DEVICES		1

struct hps_drvdata {
	struct i2c_client *client;

	struct cdev cdev;
	struct class *cdev_class;

	struct gpio_desc *enable_gpio;
};

static int hps_dev_major;

static void hps_power_on(struct hps_drvdata *hps)
{
	if (!IS_ERR_OR_NULL(hps->enable_gpio))
		gpiod_set_value_cansleep(hps->enable_gpio, 1);
}

static void hps_power_off(struct hps_drvdata *hps)
{
	if (!IS_ERR_OR_NULL(hps->enable_gpio))
		gpiod_set_value_cansleep(hps->enable_gpio, 0);
}

static void hps_unload(void *drv_data)
{
	struct hps_drvdata *hps = drv_data;

	hps_power_on(hps);
}

static int hps_open(struct inode *inode, struct file *file)
{
	struct hps_drvdata *hps = container_of(inode->i_cdev, struct hps_drvdata, cdev);
	struct device *dev = &hps->client->dev;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto pm_get_fail;
	return 0;

pm_get_fail:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int hps_release(struct inode *inode, struct file *file)
{
	struct hps_drvdata *hps = container_of(inode->i_cdev, struct hps_drvdata, cdev);
	struct device *dev = &hps->client->dev;
	int ret;

	ret = pm_runtime_put(dev);
	if (ret < 0)
		goto pm_put_fail;
	return 0;

pm_put_fail:
	pm_runtime_disable(dev);
	return ret;
}

const struct file_operations hps_fops = {
	.owner = THIS_MODULE,
	.open = hps_open,
	.release = hps_release,
};

static int hps_i2c_probe(struct i2c_client *client)
{
	struct hps_drvdata *hps;
	int ret = 0;
	dev_t hps_dev;

	hps = devm_kzalloc(&client->dev, sizeof(*hps), GFP_KERNEL);
	if (!hps)
		return -ENOMEM;

	i2c_set_clientdata(client, hps);
	hps->client = client;
	hps->enable_gpio = devm_gpiod_get(&client->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(hps->enable_gpio)) {
		ret = PTR_ERR(hps->enable_gpio);
		dev_err(&client->dev, "failed to get enable gpio: %d\n", ret);
		return ret;
	}

	ret = devm_add_action(&client->dev, &hps_unload, hps);
	if (ret) {
		dev_err(&client->dev,
			"failed to install unload action: %d\n", ret);
		return ret;
	}

	ret = alloc_chrdev_region(&hps_dev, 0, HPS_MAX_DEVICES, "hps");
	if (ret) {
		dev_err(&client->dev,
			"failed to register char dev: %d\n", ret);
		return ret;
	}
	hps_dev_major = MAJOR(hps_dev);
	cdev_init(&hps->cdev, &hps_fops);
	ret = cdev_add(&hps->cdev, hps_dev, 1);
	if (ret) {
		dev_err(&client->dev, "cdev_add() failed: %d\n", ret);
		goto cdev_add_failed;
	}

	hps->cdev_class = class_create(THIS_MODULE, "hps");
	if (IS_ERR(hps->cdev_class)) {
		dev_err(&client->dev, "class_create() failed: %d\n", ret);
		goto class_create_failed;
	}
	device_create(hps->cdev_class, NULL, hps_dev, NULL, "hps");

	hps_power_off(hps);
	pm_runtime_enable(&client->dev);
	return ret;

class_create_failed:
	ret = PTR_ERR(hps->cdev_class);
	hps->cdev_class = NULL;

cdev_add_failed:
	unregister_chrdev_region(MKDEV(hps_dev_major, 0), HPS_MAX_DEVICES);
	hps_dev_major = 0;
	return ret;
}

static int hps_i2c_remove(struct i2c_client *client)
{
	struct hps_drvdata *hps = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);
	if (hps_dev_major) {
		dev_t hps_dev = MKDEV(hps_dev_major, 0);

		device_destroy(hps->cdev_class, hps_dev);
		class_destroy(hps->cdev_class);
		hps->cdev_class = NULL;

		cdev_del(&hps->cdev);
		unregister_chrdev_region(hps_dev, HPS_MAX_DEVICES);
		hps_dev_major = 0;
	}
	return 0;
}

static int hps_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hps_drvdata *hps = i2c_get_clientdata(client);

	hps_power_off(hps);
	return 0;
}

static int hps_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hps_drvdata *hps = i2c_get_clientdata(client);

	hps_power_on(hps);
	return 0;
}
static UNIVERSAL_DEV_PM_OPS(hps_pm_ops, hps_suspend, hps_resume, NULL);

static const struct i2c_device_id hps_i2c_id[] = {
	{ "hps", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hps_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id hps_acpi_id[] = {
	{ HPS_ACPI_ID, 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, hps_acpi_id);
#endif /* CONFIG_ACPI */

static struct i2c_driver hps_i2c_driver = {
	.probe_new = hps_i2c_probe,
	.remove = hps_i2c_remove,
	.id_table = hps_i2c_id,
	.driver = {
		.name = "hps",
		.pm = &hps_pm_ops,
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(hps_acpi_id),
#endif
	},
};
module_i2c_driver(hps_i2c_driver);

MODULE_ALIAS("acpi:" HPS_ACPI_ID);
MODULE_AUTHOR("Sami Kyöstilä <skyostil@chromium.org>");
MODULE_DESCRIPTION("Driver for ChromeOS HPS");
MODULE_LICENSE("GPL");
