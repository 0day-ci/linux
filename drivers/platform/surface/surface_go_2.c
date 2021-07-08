// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/property.h>

static const struct software_node tps68470_node = {
	"INT3472",
};

static const struct property_entry ana_properties[] = {
	PROPERTY_ENTRY_STRING("regulator-name", "ANA"),
	PROPERTY_ENTRY_U32("regulator-min-microvolt", 2815200),
	PROPERTY_ENTRY_U32("regulator-max-microvolt", 2815200),
	{ }
};

static const struct property_entry vsio_properties[] = {
	PROPERTY_ENTRY_STRING("regulator-name", "VSIO"),
	PROPERTY_ENTRY_U32("regulator-min-microvolt", 1800600),
	PROPERTY_ENTRY_U32("regulator-max-microvolt", 1800600),
	{ }
};

static const struct property_entry core_properties[] = {
	PROPERTY_ENTRY_STRING("regulator-name", "CORE"),
	PROPERTY_ENTRY_U32("regulator-min-microvolt", 1200000),
	PROPERTY_ENTRY_U32("regulator-max-microvolt", 1200000),
	{ }
};

static const struct software_node regulator_nodes[] = {
	{"ANA", &tps68470_node, ana_properties},
	{"VSIO", &tps68470_node, vsio_properties},
	{"CORE", &tps68470_node, core_properties},
};

static const struct property_entry sensor_properties[] = {
	PROPERTY_ENTRY_REF("avdd-supply", &regulator_nodes[0]),
	PROPERTY_ENTRY_REF("dovdd-supply", &regulator_nodes[1]),
	PROPERTY_ENTRY_REF("dvdd-supply", &regulator_nodes[2]),
	{ }
};

static struct software_node sensor_node = {
	.name		= "INT347A",
	.properties	= sensor_properties,
};

static struct gpiod_lookup_table surface_go_2_gpios = {
	.dev_id = "i2c-INT347A:00",
	.table = {
		GPIO_LOOKUP("tps68470-gpio", 9, "reset", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("tps68470-gpio", 7, "powerdown", GPIO_ACTIVE_LOW)
	}
};

static int __init surface_go_2_init(void)
{
	struct fwnode_handle *fwnode, *sensor_fwnode;
	struct acpi_device *adev, *sensor;
	int ret;

	adev = acpi_dev_get_first_match_dev("INT3472", "0", -1);
	if (!adev) {
		pr_err("%s(): Failed to find INT3472 ACPI device\n", __func__);
		return -EINVAL;
	}

	ret = software_node_register(&tps68470_node);
	if (ret) {
		dev_err(&adev->dev, "Failed to add tps68470 software node\n");
		goto err_put_adev;
	}

	fwnode = software_node_fwnode(&tps68470_node);
	if (!fwnode) {
		dev_err(&adev->dev, "Failed to find tps68470 fwnode\n");
		ret = -ENODEV;
		goto err_put_adev;
	}

	adev->fwnode.secondary = fwnode;

	ret = software_node_register_nodes(regulator_nodes);
	if (ret) {
		dev_err(&adev->dev,
			"failed to register software nodes for regulator\n");
		goto err_unregister_node;
	}

	gpiod_add_lookup_table(&surface_go_2_gpios);

	sensor = acpi_dev_get_first_match_dev("INT347A", "0", -1);
	if (!sensor) {
		pr_err("%s(): Failed to find sensor\n", __func__);
		ret = -ENODEV;
		goto err_remove_gpio_lookup;
	}

	ret = software_node_register(&sensor_node);
	if (ret) {
		dev_err(&sensor->dev, "Failed to add sensor node\n");
		goto err_put_sensor;
	}

	sensor_fwnode = software_node_fwnode(&sensor_node);
	if (!sensor_fwnode) {
		dev_err(&sensor->dev, "Failed to find sensor fwnode\n");
		ret = -ENODEV;
		goto err_unregister_sensor_node;
	}

	sensor->fwnode.secondary = sensor_fwnode;

	return ret;

err_unregister_sensor_node:
	software_node_unregister(&sensor_node);
err_put_sensor:
	acpi_dev_put(sensor);
err_remove_gpio_lookup:
	gpiod_remove_lookup_table(&surface_go_2_gpios);
err_unregister_node:
	adev->fwnode.secondary = -ENODEV;
	software_node_unregister(&tps68470_node);
err_put_adev:
	acpi_dev_put(adev);

	return ret;
}
device_initcall(surface_go_2_init);
