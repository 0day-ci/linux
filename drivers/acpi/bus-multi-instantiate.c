// SPDX-License-Identifier: GPL-2.0+
/*
 * Bus multi-instantiate driver, pseudo driver to instantiate multiple
 * i2c-clients from a single fwnode.
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
#include <linux/types.h>

#define IRQ_RESOURCE_TYPE	GENMASK(1, 0)
#define IRQ_RESOURCE_NONE	0
#define IRQ_RESOURCE_GPIO	1
#define IRQ_RESOURCE_APIC	2

struct bmi_instance {
	const char *type;
	unsigned int flags;
	int irq_idx;
};

struct bmi {
	int i2c_num;
	struct i2c_client **i2c_devs;
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
	const struct bmi_instance *inst_array;
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	struct bmi *bmi;
	int ret;

	inst_array = device_get_match_data(dev);
	if (!inst_array) {
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

	ret = bmi_i2c_probe(pdev, adev, bmi, inst_array);
	if (ret > 0)
		return 0;
	if (ret == 0)
		ret = -ENODEV;

	return ret;
}

static int bmi_remove(struct platform_device *pdev)
{
	struct bmi *bmi = platform_get_drvdata(pdev);

	bmi_devs_unregister(bmi);

	return 0;
}

static const struct bmi_instance bsg1160_data[]  = {
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	{ "bmg160" },
	{}
};

static const struct bmi_instance bsg2150_data[]  = {
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	/* The resources describe a 3th client, but it is not really there. */
	{ "bsg2150_dummy_dev" },
	{}
};

static const struct bmi_instance int3515_data[]  = {
	{ "tps6598x", IRQ_RESOURCE_APIC, 0 },
	{ "tps6598x", IRQ_RESOURCE_APIC, 1 },
	{ "tps6598x", IRQ_RESOURCE_APIC, 2 },
	{ "tps6598x", IRQ_RESOURCE_APIC, 3 },
	{}
};

/*
 * Note new device-ids must also be added to bus_multi_instantiate_ids in
 * drivers/acpi/scan.c: acpi_device_enumeration_by_parent().
 */
static const struct acpi_device_id bmi_acpi_ids[] = {
	{ "BSG1160", (unsigned long)bsg1160_data },
	{ "BSG2150", (unsigned long)bsg2150_data },
	{ "INT3515", (unsigned long)int3515_data },
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
