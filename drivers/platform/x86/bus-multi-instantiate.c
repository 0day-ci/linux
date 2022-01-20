// SPDX-License-Identifier: GPL-2.0+
/*
 * Bus multi-instantiate driver, pseudo driver to instantiate multiple
 * i2c-clients or spi-devices from a single fwnode.
 *
 * Copyright 2018 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#define IRQ_RESOURCE_TYPE	GENMASK(1, 0)
#define IRQ_RESOURCE_NONE	0
#define IRQ_RESOURCE_GPIO	1
#define IRQ_RESOURCE_APIC	2

enum bmi_bus_type {
	BMI_I2C,
	BMI_SPI,
	BMI_AUTO_DETECT,
};

struct bmi_instance {
	const char *type;
	unsigned int flags;
	int irq_idx;
};

struct bmi_node {
	enum bmi_bus_type bus_type;
	struct bmi_instance instances[];
};

struct bmi {
	int i2c_num;
	int spi_num;
	struct i2c_client **i2c_devs;
	struct spi_device **spi_devs;
};

static int bmi_get_irq(struct platform_device *pdev, struct acpi_device *adev,
		       const struct bmi_instance *inst)
{
	int ret;

	switch (inst->flags & IRQ_RESOURCE_TYPE) {
	case IRQ_RESOURCE_GPIO:
		ret = acpi_dev_gpio_irq_get(adev, inst->irq_idx);
		break;
	case IRQ_RESOURCE_APIC:
		ret = platform_get_irq(pdev, inst->irq_idx);
		break;
	default:
		ret = 0;
		break;
	}

	if (ret < 0)
		dev_err_probe(&pdev->dev, ret, "Error requesting irq at index %d: %d\n",
			      inst->irq_idx, ret);

	return ret;
}

static void bmi_devs_unregister(struct bmi *bmi)
{
	while (bmi->i2c_num > 0)
		i2c_unregister_device(bmi->i2c_devs[--bmi->i2c_num]);

	while (bmi->spi_num > 0)
		spi_unregister_device(bmi->spi_devs[--bmi->spi_num]);
}

/**
 * bmi_spi_probe - Instantiate multiple SPI devices from inst array
 * @pdev:	Platform device
 * @adev:	ACPI device
 * @bmi:	Internal struct for Bus multi instantiate driver
 * @inst:	Array of instances to probe
 *
 * Returns the number of SPI devices instantiate, Zero if none is found or a negative error code.
 */
static int bmi_spi_probe(struct platform_device *pdev, struct acpi_device *adev, struct bmi *bmi,
			 const struct bmi_instance *inst_array)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctlr;
	struct spi_device *spi_dev;
	char name[50];
	int i, ret, count;

	ret = acpi_spi_count_resources(adev);
	if (ret <= 0)
		return ret;
	count = ret;

	bmi->spi_devs = devm_kcalloc(dev, count, sizeof(*bmi->spi_devs), GFP_KERNEL);
	if (!bmi->spi_devs)
		return -ENOMEM;

	for (i = 0; i < count && inst_array[i].type; i++) {

		spi_dev = acpi_spi_device_alloc(NULL, adev, i, inst_array[i].irq_idx);
		if (IS_ERR(spi_dev)) {
			ret = PTR_ERR(spi_dev);
			goto error;
		}

		ctlr = spi_dev->controller;

		strscpy(spi_dev->modalias, inst_array[i].type, sizeof(spi_dev->modalias));

		if (spi_dev->irq < 0) {
			ret = bmi_get_irq(pdev, adev, &inst_array[i]);
			if (ret < 0) {
				spi_dev_put(spi_dev);
				goto error;
			}
			spi_dev->irq = ret;
		}

		snprintf(name, sizeof(name), "%s-%s-%s.%d", dev_name(&ctlr->dev), dev_name(dev),
			 inst_array[i].type, i);
		spi_dev->dev.init_name = name;

		ret = spi_add_device(spi_dev);
		if (ret) {
			dev_err(&ctlr->dev, "failed to add SPI device %s from ACPI: %d\n",
				dev_name(&adev->dev), ret);
			spi_dev_put(spi_dev);
			goto error;
		}

		dev_dbg(dev, "SPI device %s using chip select %u", name, spi_dev->chip_select);

		bmi->spi_devs[i] = spi_dev;
		bmi->spi_num++;
	}

	if (bmi->spi_num < count) {
		dev_err(dev, "Error finding driver, idx %d\n", i);
		ret = -ENODEV;
		goto error;
	}

	dev_info(dev, "Instantiate %d SPI devices.\n", bmi->spi_num);

	return bmi->spi_num;
error:
	bmi_devs_unregister(bmi);
	dev_err_probe(dev, ret, "SPI error %d\n", ret);

	return ret;

}

/**
 * bmi_i2c_probe - Instantiate multiple I2C devices from inst array
 * @pdev:	Platform device
 * @adev:	ACPI device
 * @bmi:	Internal struct for Bus multi instantiate driver
 * @inst:	Array of instances to probe
 *
 * Returns the number of I2C devices instantiate, Zero if none is found or a negative error code.
 */
static int bmi_i2c_probe(struct platform_device *pdev, struct acpi_device *adev, struct bmi *bmi,
			 const struct bmi_instance *inst_array)
{
	struct i2c_board_info board_info = {};
	struct device *dev = &pdev->dev;
	char name[32];
	int i, ret = 0, count;

	ret = i2c_acpi_client_count(adev);
	if (ret <= 0)
		return ret;
	count = ret;

	bmi->i2c_devs = devm_kcalloc(dev, count, sizeof(*bmi->i2c_devs), GFP_KERNEL);
	if (!bmi->i2c_devs)
		return -ENOMEM;

	for (i = 0; i < count && inst_array[i].type; i++) {
		memset(&board_info, 0, sizeof(board_info));
		strscpy(board_info.type, inst_array[i].type, I2C_NAME_SIZE);
		snprintf(name, sizeof(name), "%s-%s.%d", dev_name(dev), inst_array[i].type, i);
		board_info.dev_name = name;

		ret = bmi_get_irq(pdev, adev, &inst_array[i]);
		if (ret < 0)
			goto error;
		board_info.irq = ret;

		bmi->i2c_devs[i] = i2c_acpi_new_device(dev, i, &board_info);
		if (IS_ERR(bmi->i2c_devs[i])) {
			ret = dev_err_probe(dev, PTR_ERR(bmi->i2c_devs[i]),
					    "Error creating i2c-client, idx %d\n", i);
			goto error;
		}
		bmi->i2c_num++;
	}
	if (bmi->i2c_num < count) {
		dev_err(dev, "Error finding driver, idx %d\n", i);
		ret = -ENODEV;
		goto error;
	}

	dev_info(dev, "Instantiate %d I2C devices.\n", bmi->i2c_num);

	return bmi->i2c_num;
error:
	dev_err_probe(dev, ret, "I2C error %d\n", ret);
	bmi_devs_unregister(bmi);

	return ret;
}

static int bmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct bmi_node *node;
	struct acpi_device *adev;
	struct bmi *bmi;
	int i2c_ret = 0, spi_ret = 0;

	node = device_get_match_data(dev);
	if (!node) {
		dev_err(dev, "Error ACPI match data is missing\n");
		return -ENODEV;
	}

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	bmi = devm_kzalloc(dev, sizeof(*bmi), GFP_KERNEL);
	if (!bmi)
		return -ENOMEM;

	platform_set_drvdata(pdev, bmi);

	/* Each time this driver probes only one type of bus will be chosen.
	 * And I2C has preference, which means that if find a I2cSerialBus it assumes
	 * that all following devices will also be I2C.
	 * In case there are zero I2C devices, it assumes that all following devices are SPI.
	 */
	if (node->bus_type != BMI_SPI) {
		i2c_ret = bmi_i2c_probe(pdev, adev, bmi, node->instances);
		if (i2c_ret > 0)
			return 0;
		else if (i2c_ret == -EPROBE_DEFER)
			return i2c_ret;
		if (node->bus_type == BMI_I2C) {
			if (i2c_ret == 0)
				return -ENODEV;
			else
				return i2c_ret;
		}
	}
	/* BMI_SPI or BMI_AUTO_DETECT */
	spi_ret = bmi_spi_probe(pdev, adev, bmi, node->instances);
	if (spi_ret > 0)
		return 0;
	else if (spi_ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (node->bus_type == BMI_SPI) {
		if (spi_ret == 0)
			return -ENODEV;
		else
			return spi_ret;
	}

	/* The only way to get here is BMI_AUTO_DETECT and i2c_ret <= 0 and spi_ret <= 0 */
	if (i2c_ret == 0 && spi_ret == 0)
		return -ENODEV;
	else if (i2c_ret == 0 && spi_ret)
		return spi_ret;

	return i2c_ret;
}

static int bmi_remove(struct platform_device *pdev)
{
	struct bmi *bmi = platform_get_drvdata(pdev);

	bmi_devs_unregister(bmi);

	return 0;
}

static const struct bmi_node bsg1160_data = {
	.instances = {
		{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
		{ "bmc150_magn" },
		{ "bmg160" },
		{}
	},
	.bus_type = BMI_I2C,
};

static const struct bmi_node bsg2150_data = {
	.instances = {
		{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
		{ "bmc150_magn" },
		/* The resources describe a 3th client, but it is not really there. */
		{ "bsg2150_dummy_dev" },
		{}
	},
	.bus_type = BMI_I2C,
};

static const struct bmi_node int3515_data = {
	.instances = {
		{ "tps6598x", IRQ_RESOURCE_APIC, 0 },
		{ "tps6598x", IRQ_RESOURCE_APIC, 1 },
		{ "tps6598x", IRQ_RESOURCE_APIC, 2 },
		{ "tps6598x", IRQ_RESOURCE_APIC, 3 },
		{}
	},
	.bus_type = BMI_I2C,
};

static const struct bmi_node cs35l41_hda = {
	.instances = {
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{ "cs35l41-hda", IRQ_RESOURCE_GPIO, 0 },
		{}
	},
	.bus_type = BMI_AUTO_DETECT,
};

/*
 * Note new device-ids must also be added to bus_multi_instantiate_ids in
 * drivers/acpi/scan.c: acpi_device_enumeration_by_parent().
 */
static const struct acpi_device_id bmi_acpi_ids[] = {
	{ "BSG1160", (unsigned long)&bsg1160_data },
	{ "BSG2150", (unsigned long)&bsg2150_data },
	{ "INT3515", (unsigned long)&int3515_data },
	{ "CSC3551", (unsigned long)&cs35l41_hda },
	/* Non-conforming _HID for Cirrus Logic already released */
	{ "CLSA0100", (unsigned long)&cs35l41_hda },
	{ }
};
MODULE_DEVICE_TABLE(acpi, bmi_acpi_ids);

static struct platform_driver bmi_driver = {
	.driver	= {
		.name = "Bus multi instantiate pseudo device driver",
		.acpi_match_table = bmi_acpi_ids,
	},
	.probe = bmi_probe,
	.remove = bmi_remove,
};
module_platform_driver(bmi_driver);

MODULE_DESCRIPTION("Bus multi instantiate pseudo device driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
