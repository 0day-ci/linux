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

#define IRQ_RESOURCE_TYPE		GENMASK(1, 0)
#define IRQ_RESOURCE_NONE		0
#define IRQ_RESOURCE_GPIO		1
#define IRQ_RESOURCE_APIC		2
#define MAX_RESOURCE_SOURCE_CHAR	30

struct inst_data {
	const char *type;
	unsigned int flags;
	int irq_idx;
};

struct multi_inst_data {
	int i2c_num;
	int spi_num;
	struct spi_device **spi_devs;
	struct i2c_client **i2c_devs;
};

struct spi_acpi_data {
	char resource_source[MAX_RESOURCE_SOURCE_CHAR];
	struct acpi_resource_spi_serialbus sb;
};

struct spi_serialbus_acpi_data {
	int count;
	struct spi_acpi_data acpi_data[];
};

static int spi_count(struct acpi_resource *ares, void *data)
{
	struct acpi_resource_spi_serialbus *sb;
	int *count = data;

	if (ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return 1;

	sb = &ares->data.spi_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_SPI)
		return 1;

	*count = *count + 1;
	return 1;
}

static int spi_count_resources(struct acpi_device *adev)
{
	LIST_HEAD(r);
	int count = 0;
	int ret;

	ret = acpi_dev_get_resources(adev, &r, spi_count, &count);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&r);
	return count;
}

static int spi_save_res(struct acpi_resource *ares, void *data)
{
	struct acpi_resource_spi_serialbus *sb;
	struct spi_serialbus_acpi_data *resources = data;

	if (ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return 1;

	sb = &ares->data.spi_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_SPI)
		return 1;

	memcpy(&resources->acpi_data[resources->count].sb, sb, sizeof(*sb));
	strscpy(resources->acpi_data[resources->count].resource_source,
		sb->resource_source.string_ptr,
		sizeof(resources->acpi_data[resources->count].resource_source));
	resources->count++;

	return 1;
}

static struct spi_serialbus_acpi_data *spi_get_resources(struct device *dev,
							 struct acpi_device *adev, int count)
{
	struct spi_serialbus_acpi_data *resources;
	LIST_HEAD(r);
	int ret;

	resources = kmalloc(struct_size(resources, acpi_data, count), GFP_KERNEL);
	if (!resources)
		return NULL;

	ret = acpi_dev_get_resources(adev, &r, spi_save_res, resources);
	if (ret < 0)
		goto error;

	acpi_dev_free_resource_list(&r);

	return resources;

error:
	kfree(resources);
	return NULL;
}

static struct spi_controller *find_spi_controller(char *path)
{
	struct spi_controller *ctlr;
	acpi_handle parent_handle;
	acpi_status status;
	int i;

	status = acpi_get_handle(NULL, path, &parent_handle);
	if (ACPI_FAILURE(status))
		return NULL;

	/* There will be not more than 10 spi controller for a device */
	for (i = 0 ; i < 10 ; i++) {
		ctlr = spi_busnum_to_master(i);
		if (ctlr && ACPI_HANDLE(ctlr->dev.parent) == parent_handle)
			return ctlr;
	}

	return NULL;
}

static int spi_multi_inst_probe(struct platform_device *pdev, struct acpi_device *adev,
				const struct inst_data *inst_data, int count)
{
	struct spi_serialbus_acpi_data *acpi_data;
	struct device *dev = &pdev->dev;
	struct multi_inst_data *multi;
	struct spi_controller *ctlr;
	struct spi_device *spi_dev;
	char name[50];
	int i, ret;

	multi = devm_kzalloc(dev, sizeof(*multi), GFP_KERNEL);
	if (!multi)
		return -ENOMEM;

	multi->spi_devs = devm_kcalloc(dev, count, sizeof(*multi->spi_devs), GFP_KERNEL);
	if (!multi->spi_devs)
		return -ENOMEM;

	acpi_data = spi_get_resources(dev, adev, count);
	if (!acpi_data)
		return -ENOMEM;

	for (i = 0; i < count && inst_data[i].type; i++) {
		ctlr = find_spi_controller(acpi_data->acpi_data[i].resource_source);
		if (!ctlr) {
			ret = -EPROBE_DEFER;
			goto probe_error;
		}

		spi_dev = spi_alloc_device(ctlr);
		if (!spi_dev) {
			dev_err(&ctlr->dev, "failed to allocate SPI device for %s\n",
				dev_name(&adev->dev));
			ret = -ENOMEM;
			goto probe_error;
		}

		strscpy(spi_dev->modalias, inst_data[i].type, sizeof(spi_dev->modalias));

		if (ctlr->fw_translate_cs) {
			int cs = ctlr->fw_translate_cs(ctlr,
					acpi_data->acpi_data[i].sb.device_selection);
			if (cs < 0) {
				ret = cs;
				goto probe_error;
			}
			spi_dev->chip_select = cs;
		} else {
			spi_dev->chip_select = acpi_data->acpi_data[i].sb.device_selection;
		}

		spi_dev->max_speed_hz = acpi_data->acpi_data[i].sb.connection_speed;
		spi_dev->bits_per_word = acpi_data->acpi_data[i].sb.data_bit_length;

		if (acpi_data->acpi_data[i].sb.clock_phase == ACPI_SPI_SECOND_PHASE)
			spi_dev->mode |= SPI_CPHA;
		if (acpi_data->acpi_data[i].sb.clock_polarity == ACPI_SPI_START_HIGH)
			spi_dev->mode |= SPI_CPOL;
		if (acpi_data->acpi_data[i].sb.device_polarity == ACPI_SPI_ACTIVE_HIGH)
			spi_dev->mode |= SPI_CS_HIGH;

		switch (inst_data[i].flags & IRQ_RESOURCE_TYPE) {
		case IRQ_RESOURCE_GPIO:
			ret = acpi_dev_gpio_irq_get(adev, inst_data[i].irq_idx);
			if (ret < 0) {
				if (ret != -EPROBE_DEFER)
					dev_err(dev, "Error requesting irq at index %d: %d\n",
						inst_data[i].irq_idx, ret);
				goto probe_error;
			}
			spi_dev->irq = ret;
			break;
		case IRQ_RESOURCE_APIC:
			ret = platform_get_irq(pdev, inst_data[i].irq_idx);
			if (ret < 0) {
				dev_dbg(dev, "Error requesting irq at index %d: %d\n",
					inst_data[i].irq_idx, ret);
				goto probe_error;
			}
			spi_dev->irq = ret;
			break;
		default:
			spi_dev->irq = 0;
			break;
		}

		snprintf(name, sizeof(name), "%s.%u-%s-%s.%d", dev_name(&ctlr->dev),
			 spi_dev->chip_select, dev_name(dev), inst_data[i].type, i);
		spi_dev->dev.init_name = name;

		if (spi_add_device(spi_dev)) {
			dev_err(&ctlr->dev, "failed to add SPI device %s from ACPI\n",
				dev_name(&adev->dev));
			spi_dev_put(spi_dev);
			goto probe_error;
		}

		multi->spi_devs[i] = spi_dev;
		multi->spi_num++;
	}

	if (multi->spi_num < count) {
		dev_err(dev, "Error finding driver, idx %d\n", i);
		ret = -ENODEV;
		goto probe_error;
	}

	dev_info(dev, "Instantiate %d devices.\n", multi->spi_num);
	platform_set_drvdata(pdev, multi);
	kfree(acpi_data);

	return 0;

probe_error:
	while (--i >= 0)
		spi_unregister_device(multi->spi_devs[i]);

	kfree(acpi_data);
	return ret;
}

static int i2c_multi_inst_probe(struct platform_device *pdev, struct acpi_device *adev,
				const struct inst_data *inst_data, int count)
{
	struct i2c_board_info board_info = {};
	struct device *dev = &pdev->dev;
	struct multi_inst_data *multi;
	char name[32];
	int i, ret;

	multi = devm_kzalloc(dev, sizeof(*multi), GFP_KERNEL);
	if (!multi)
		return -ENOMEM;

	multi->i2c_devs = devm_kcalloc(dev, count, sizeof(*multi->i2c_devs), GFP_KERNEL);
	if (!multi->i2c_devs)
		return -ENOMEM;

	for (i = 0; i < count && inst_data[i].type; i++) {
		memset(&board_info, 0, sizeof(board_info));
		strscpy(board_info.type, inst_data[i].type, I2C_NAME_SIZE);
		snprintf(name, sizeof(name), "%s-%s.%d", dev_name(dev), inst_data[i].type, i);
		board_info.dev_name = name;
		switch (inst_data[i].flags & IRQ_RESOURCE_TYPE) {
		case IRQ_RESOURCE_GPIO:
			ret = acpi_dev_gpio_irq_get(adev, inst_data[i].irq_idx);
			if (ret < 0) {
				if (ret != -EPROBE_DEFER)
					dev_err(dev, "Error requesting irq at index %d: %d\n",
						inst_data[i].irq_idx, ret);
				goto error;
			}
			board_info.irq = ret;
			break;
		case IRQ_RESOURCE_APIC:
			ret = platform_get_irq(pdev, inst_data[i].irq_idx);
			if (ret < 0) {
				dev_dbg(dev, "Error requesting irq at index %d: %d\n",
					inst_data[i].irq_idx, ret);
				goto error;
			}
			board_info.irq = ret;
			break;
		default:
			board_info.irq = 0;
			break;
		}
		multi->i2c_devs[i] = i2c_acpi_new_device(dev, i, &board_info);
		if (IS_ERR(multi->i2c_devs[i])) {
			ret = dev_err_probe(dev, PTR_ERR(multi->i2c_devs[i]),
					    "Error creating i2c-client, idx %d\n", i);
			goto error;
		}
		multi->i2c_num++;
	}
	if (multi->i2c_num < count) {
		dev_err(dev, "Error finding driver, idx %d\n", i);
		ret = -ENODEV;
		goto error;
	}

	dev_info(dev, "Instantiate %d devices.\n", multi->i2c_num);
	platform_set_drvdata(pdev, multi);

	return 0;

error:
	while (--i >= 0)
		i2c_unregister_device(multi->i2c_devs[i]);

	return ret;
}

static int bus_multi_inst_probe(struct platform_device *pdev)
{
	const struct inst_data *inst_data;
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	int count;

	inst_data = device_get_match_data(dev);
	if (!inst_data) {
		dev_err(dev, "Error ACPI match data is missing\n");
		return -ENODEV;
	}

	adev = ACPI_COMPANION(dev);

	/* Count number of i2c clients to instantiate */
	count = i2c_acpi_client_count(adev);
	if (count > 0)
		return i2c_multi_inst_probe(pdev, adev, inst_data, count);
	else if (count < 0)
		dev_warn(dev, "I2C multi instantiate error %d\n", count);

	/* Count number of spi devices to instantiate */
	count = spi_count_resources(adev);
	if (count > 0)
		return spi_multi_inst_probe(pdev, adev, inst_data, count);
	else if (count < 0)
		dev_warn(dev, "SPI multi instantiate error %d\n", count);

	return -ENODEV;
}

static int bus_multi_inst_remove(struct platform_device *pdev)
{
	struct multi_inst_data *multi = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < multi->i2c_num; i++)
		i2c_unregister_device(multi->i2c_devs[i]);

	for (i = 0; i < multi->spi_num; i++)
		spi_unregister_device(multi->spi_devs[i]);

	return 0;
}

static const struct inst_data bsg1160_data[]  = {
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	{ "bmg160" },
	{}
};

static const struct inst_data bsg2150_data[]  = {
	{ "bmc150_accel", IRQ_RESOURCE_GPIO, 0 },
	{ "bmc150_magn" },
	/* The resources describe a 3th client, but it is not really there. */
	{ "bsg2150_dummy_dev" },
	{}
};

static const struct inst_data int3515_data[]  = {
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
static const struct acpi_device_id bus_multi_inst_acpi_ids[] = {
	{ "BSG1160", (unsigned long)bsg1160_data },
	{ "BSG2150", (unsigned long)bsg2150_data },
	{ "INT3515", (unsigned long)int3515_data },
	{ }
};
MODULE_DEVICE_TABLE(acpi, bus_multi_inst_acpi_ids);

static struct platform_driver bus_multi_inst_driver = {
	.driver	= {
		.name = "Bus multi instantiate pseudo device driver",
		.acpi_match_table = bus_multi_inst_acpi_ids,
	},
	.probe = bus_multi_inst_probe,
	.remove = bus_multi_inst_remove,
};
module_platform_driver(bus_multi_inst_driver);

MODULE_DESCRIPTION("Bus multi instantiate pseudo device driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
