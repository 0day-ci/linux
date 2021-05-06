// SPDX-License-Identifier: GPL-2.0-only
/*
 * Advantech AHC1EC0 Embedded Controller
 *
 * Copyright 2021 Advantech IIoT Group
 */

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/platform_data/ahc1ec0.h>

/* This order cannot be changed, it is use in enum and may in BIOS ACPI table. */
enum {
	ADVEC_ACPI_ID_BRIGHTNESS = 0,
	ADVEC_ACPI_ID_EEPROM,
	ADVEC_ACPI_ID_GPIO,
	ADVEC_ACPI_ID_HWMON,
	ADVEC_ACPI_ID_LED,
	ADVEC_ACPI_ID_WDT,
	ADVEC_ACPI_ID_MAX,
};

static const struct mfd_cell adv_ec_sub_cells[] = {
	{ .name = "adv-ec-brightness", },
	{ .name = "adv-ec-eeprom", },
	{ .name = "adv-ec-gpio", },
	{ .name = "ahc1ec0-hwmon", },
	{ .name = "adv-ec-led", },
	{ .name = "ahc1ec0-wdt", },
};

static int adv_ec_init_ec_data(struct adv_ec_ddata *ddata)
{
	int ret;

	mutex_init(&ddata->lock);

	/* Get product name */
	ddata->bios_product_name =
		devm_kzalloc(ddata->dev, AMI_ADVANTECH_BOARD_ID_LENGTH, GFP_KERNEL);
	if (!ddata->bios_product_name)
		return -ENOMEM;

	ret = adv_ec_get_productname(ddata, ddata->bios_product_name);
	if (ret)
		return ret;

	/* Get pin table */
	ddata->dym_tbl = devm_kzalloc(ddata->dev,
				      EC_MAX_TBL_NUM * sizeof(struct ec_dynamic_table),
				      GFP_KERNEL);
	if (!ddata->dym_tbl)
		return -ENOMEM;

	return adv_get_dynamic_tab(ddata);
}

static int adv_ec_parse_prop(struct adv_ec_ddata *ddata)
{
	int ret;
	u32 value;
	bool has_watchdog = true;

	/* check whether this EC has the following subdevices, hwmon and watchdog. */
	if (device_property_read_u32(ddata->dev, "advantech,hwmon-profile", &value) >= 0) {
		ret = mfd_add_hotplug_devices(ddata->dev,
					      &adv_ec_sub_cells[ADVEC_ACPI_ID_HWMON], 1);
		if (ret) {
			dev_err(ddata->dev, "Failed to add %s subdevice: %d\n",
				adv_ec_sub_cells[ADVEC_ACPI_ID_HWMON].name, ret);
		} else {
			dev_info(ddata->dev, "Success to add %s subdevice\n",
				 adv_ec_sub_cells[ADVEC_ACPI_ID_HWMON].name);
		}
	} else {
		dev_err(ddata->dev, "No 'advantech,hwmon-profile' property: %d\n",
			ret);
	}

	/* default is true for watchdog even if it is not existed */
	if (device_property_present(ddata->dev, "advantech,has-watchdog"))
		has_watchdog = device_property_read_bool(ddata->dev, "advantech,has-watchdog");
	if (has_watchdog) {
		ret = mfd_add_hotplug_devices(ddata->dev, &adv_ec_sub_cells[ADVEC_ACPI_ID_WDT], 1);
		if (ret) {
			dev_err(ddata->dev, "Failed to add %s subdevice: %d\n",
				adv_ec_sub_cells[ADVEC_ACPI_ID_WDT].name, ret);
		} else {
			dev_info(ddata->dev, "Success to add %s subdevice\n",
				 adv_ec_sub_cells[ADVEC_ACPI_ID_WDT].name);
		}
	}

	return 0;
}

static int adv_ec_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct adv_ec_ddata *ddata;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	dev_set_drvdata(dev, ddata);
	ddata->dev = dev;

	ret = adv_ec_init_ec_data(ddata);
	if (ret)
		goto err_prop;

	ret = adv_ec_parse_prop(ddata);
	if (ret)
		goto err_prop;

	dev_info(ddata->dev, "Advantech AHC1EC0 probe done");

	return 0;

err_prop:
	mutex_destroy(&ddata->lock);

	dev_dbg(dev, "Failed to init data and probe\n");
	return ret;
}

static int adv_ec_remove(struct platform_device *pdev)
{
	struct adv_ec_ddata *ddata;

	ddata = dev_get_drvdata(&pdev->dev);

	mutex_destroy(&ddata->lock);
	return 0;
}

static const struct of_device_id adv_ec_of_match[] __maybe_unused = {
	{ .compatible = "advantech,ahc1ec0" },
	{ },
};
MODULE_DEVICE_TABLE(of, adv_ec_of_match);

static const struct acpi_device_id adv_ec_acpi_match[] __maybe_unused = {
	{ "AHC1EC0", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, adv_ec_acpi_match);

static struct platform_driver adv_ec_driver = {
	.driver = {
		.name = "ahc1ec0",
		.of_match_table = of_match_ptr(adv_ec_of_match),
		.acpi_match_table = ACPI_PTR(adv_ec_acpi_match),
	},
	.probe = adv_ec_probe,
	.remove = adv_ec_remove,
};
module_platform_driver(adv_ec_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ahc1ec0");
MODULE_DESCRIPTION("Advantech AHC1EC0 Embedded Controller");
MODULE_AUTHOR("Campion Kang <campion.kang@advantech.com.tw>");
MODULE_AUTHOR("Jianfeng Dai <jianfeng.dai@advantech.com.cn>");
MODULE_VERSION("1.0");
