// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Linaro Limited
 */
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

#define MAX_NUM_REGULATORS	8

static struct vreg {
	const char *name;
	unsigned int load_uA;
} vregs[MAX_NUM_REGULATORS] = {
	/* 2.0 V */
	{ "vddpcie2", 15000 },
	{ "vddrfa3", 400000 },

	/* 0.95 V */
	{ "vddaon", 100000 },
	{ "vddpmu", 1250000 },
	{ "vddrfa1", 200000 },

	/* 1.35 V */
	{ "vddrfa2", 400000 },
	{ "vddpcie1", 35000 },

	/* 1.8 V */
	{ "vddio", 20000 },
};

struct qca6390_data {
	struct device *dev;
	struct regulator_bulk_data regulators[MAX_NUM_REGULATORS];
	size_t num_vregs;

	struct regulator_desc desc;
	struct regulator_dev *regulator_dev;
	unsigned int enable_counter;
};

#define domain_to_data(domain) container_of(domain, struct qca6390_data, pd)

static int qca6390_enable(struct regulator_dev *rdev)
{
	struct qca6390_data *data = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_bulk_enable(data->num_vregs, data->regulators);
	if (ret) {
		dev_err(data->dev, "Failed to enable regulators");
		return ret;
	}

	/* Wait for 1ms before toggling enable pins. */
	usleep_range(1000, 2000);

	data->enable_counter++;

	return 0;
}

static int qca6390_disable(struct regulator_dev *rdev)
{
	struct qca6390_data *data = rdev_get_drvdata(rdev);

	regulator_bulk_disable(data->num_vregs, data->regulators);

	data->enable_counter--;

	return 0;
}

static int qca6390_is_enabled(struct regulator_dev *rdev)
{
	struct qca6390_data *data = rdev_get_drvdata(rdev);

	return data->enable_counter > 0;
}

static const struct regulator_ops qca6390_ops = {
	.enable = qca6390_enable,
	.disable = qca6390_disable,
	.is_enabled = qca6390_is_enabled,
};

static int qca6390_probe(struct platform_device *pdev)
{
	struct qca6390_data *data;
	struct device *dev = &pdev->dev;
	struct regulator_config cfg = { };
	int i, ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->num_vregs = ARRAY_SIZE(vregs);

	for (i = 0; i < data->num_vregs; i++)
		data->regulators[i].supply = vregs[i].name;

	ret = devm_regulator_bulk_get(dev, data->num_vregs, data->regulators);
	if (ret < 0)
		return ret;

	for (i = 0; i < data->num_vregs; i++) {
		ret = regulator_set_load(data->regulators[i].consumer, vregs[i].load_uA);
		if (ret)
			return ret;
	}

	data->desc.name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!data->desc.name)
		return -ENOMEM;

	data->desc.type = REGULATOR_VOLTAGE;
	data->desc.owner = THIS_MODULE;
	data->desc.ops = &qca6390_ops;

	cfg.dev = dev;
	cfg.of_node = dev->of_node;
	cfg.driver_data = data;
	cfg.init_data = of_get_regulator_init_data(dev, dev->of_node, &data->desc);

	data->regulator_dev = devm_regulator_register(dev, &data->desc, &cfg);
	if (IS_ERR(data->regulator_dev)) {
		ret = PTR_ERR(data->regulator_dev);
		return ret;
	}

	platform_set_drvdata(pdev, data);

	return 0;
}

static const struct of_device_id qca6390_of_match[] = {
	{ .compatible = "qcom,qca6390" },
};

static struct platform_driver qca6390_driver = {
	.probe = qca6390_probe,
	.driver = {
		.name = "qca6390",
		.of_match_table = qca6390_of_match,
	},
};

module_platform_driver(qca6390_driver);
MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_DESCRIPTION("Power control for Qualcomm QCA6390/1 BT/WiFi chip");
MODULE_LICENSE("GPL v2");
