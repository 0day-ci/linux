// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, The Linux Foundation. All rights reserved. */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define STARTUP_DELAY_USEC		20
#define VSET_STEP_MV			8
#define VSET_STEP_UV			(VSET_STEP_MV * 1000)

#define LDO_ENABLE_REG(base)		((base) + 0x46)
#define ENABLE_BIT			BIT(7)

#define LDO_STATUS1_REG(base)		((base) + 0x08)
#define VREG_READY_BIT			BIT(7)

#define LDO_VSET_LB_REG(base)		((base) + 0x40)

#define LDO_STEPPER_CTL_REG(base)	((base) + 0x3b)
#define DEFAULT_VOLTAGE_STEPPER_RATE	38400
#define STEP_RATE_MASK			GENMASK(1, 0)

#define PM8008_NUM_LDOS			7

struct regulator_data {
	const char			*name;
	const char			*supply_name;
	u16				base;
	int				min_uv;
	int				max_uv;
	int				min_dropout_uv;
	const struct linear_range	*voltage_range;
};

struct pm8008_regulator {
	struct device		*dev;
	struct regmap		*regmap;
	struct regulator_desc	rdesc;
	u16			base;
	int			step_rate;
};

static const struct linear_range nldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(528000, 0, 122, 8000),
};

static const struct linear_range pldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(1504000, 0, 237, 8000),
};

static const struct regulator_data reg_data[] = {
	/* name  parent       base   min_uv  max_uv  headroom_uv voltage_range */
	{ "LDO1", "vdd_l1_l2", 0x4000,  528000, 1504000, 225000, nldo_ranges, },
	{ "LDO2", "vdd_l1_l2", 0x4100,  528000, 1504000, 225000, nldo_ranges, },
	{ "LDO3", "vdd_l3_l4", 0x4200, 1504000, 3400000, 300000, pldo_ranges, },
	{ "LDO4", "vdd_l3_l4", 0x4300, 1504000, 3400000, 300000, pldo_ranges, },
	{ "LDO5", "vdd_l5",    0x4400, 1504000, 3400000, 200000, pldo_ranges, },
	{ "LDO6", "vdd_l6",    0x4500, 1504000, 3400000, 200000, pldo_ranges, },
	{ "LDO7", "vdd_l7",    0x4600, 1504000, 3400000, 200000, pldo_ranges, },
};

static int pm8008_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct pm8008_regulator *pm8008_reg = rdev_get_drvdata(rdev);
	__le16 mV;
	int rc;

	rc = regmap_bulk_read(pm8008_reg->regmap,
			LDO_VSET_LB_REG(pm8008_reg->base), (void *)&mV, 2);
	if (rc < 0) {
		dev_err(&rdev->dev, "failed to read regulator voltage rc=%d\n", rc);
		return rc;
	}

	return le16_to_cpu(mV) * 1000;
}

static inline int pm8008_write_voltage(struct pm8008_regulator *pm8008_reg,
							int mV)
{
	int rc;
	u16 vset_raw;

	vset_raw = cpu_to_le16(mV);

	rc = regmap_bulk_write(pm8008_reg->regmap,
			LDO_VSET_LB_REG(pm8008_reg->base),
			(const void *)&vset_raw, sizeof(vset_raw));
	if (rc < 0) {
		dev_err(pm8008_reg->dev, "failed to write voltage rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int pm8008_regulator_set_voltage_time(struct regulator_dev *rdev,
				int old_uV, int new_uv)
{
	struct pm8008_regulator *pm8008_reg = rdev_get_drvdata(rdev);

	return DIV_ROUND_UP(abs(new_uv - old_uV), pm8008_reg->step_rate);
}

static int pm8008_regulator_set_voltage(struct regulator_dev *rdev,
					unsigned int selector)
{
	struct pm8008_regulator *pm8008_reg = rdev_get_drvdata(rdev);
	int rc, mV;

	/* voltage control register is set with voltage in millivolts */
	mV = DIV_ROUND_UP(regulator_list_voltage_linear_range(rdev, selector),
						1000);
	if (mV < 0)
		return mV;

	rc = pm8008_write_voltage(pm8008_reg, mV);
	if (rc < 0)
		return rc;

	dev_dbg(&rdev->dev, "voltage set to %d\n", mV * 1000);
	return 0;
}

static const struct regulator_ops pm8008_regulator_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_voltage_sel	= pm8008_regulator_set_voltage,
	.get_voltage		= pm8008_regulator_get_voltage,
	.list_voltage		= regulator_list_voltage_linear,
	.set_voltage_time	= pm8008_regulator_set_voltage_time,
};

static int pm8008_regulator_of_parse(struct device_node *node,
			const struct regulator_desc *desc,
			struct regulator_config *config)
{
	struct pm8008_regulator *pm8008_reg = config->driver_data;
	int rc;
	unsigned int reg;

	/* get slew rate */
	rc = regmap_bulk_read(pm8008_reg->regmap,
			LDO_STEPPER_CTL_REG(pm8008_reg->base), &reg, 1);
	if (rc < 0) {
		dev_err(pm8008_reg->dev,
			"%s: failed to read step rate configuration rc=%d\n",
			pm8008_reg->rdesc.name, rc);
		return rc;
	}
	reg &= STEP_RATE_MASK;
	pm8008_reg->step_rate = DEFAULT_VOLTAGE_STEPPER_RATE >> reg;

	return 0;
}

static int pm8008_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int id = pdev->id % PM8008_NUM_LDOS;
	struct regulator_dev    *rdev;
	struct pm8008_regulator *pm8008_reg;
	struct regmap *regmap;
	struct regulator_config reg_config = {};
	int rc;

	dev_dbg(dev, "DEBUG: Probing LDO%d\n", id + 1);

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		dev_err(dev, "parent regmap is missing\n");
		return -EINVAL;
	}

	pm8008_reg = devm_kzalloc(dev, sizeof(*pm8008_reg), GFP_KERNEL);
	if (!pm8008_reg)
		return -ENOMEM;

	pm8008_reg->regmap = regmap;
	pm8008_reg->dev = dev;
	pm8008_reg->base = reg_data[id].base;

	pm8008_reg->rdesc.type = REGULATOR_VOLTAGE;
	pm8008_reg->rdesc.regulators_node = of_match_ptr("regulators");
	pm8008_reg->rdesc.ops = &pm8008_regulator_ops;
	pm8008_reg->rdesc.name = reg_data[id].name;
	pm8008_reg->rdesc.supply_name = reg_data[id].supply_name;
	pm8008_reg->rdesc.of_match = reg_data[id].name;
	pm8008_reg->rdesc.of_parse_cb = pm8008_regulator_of_parse;
	pm8008_reg->rdesc.uV_step = VSET_STEP_UV;
	pm8008_reg->rdesc.min_uV = reg_data[id].min_uv;
	pm8008_reg->rdesc.n_voltages
		= ((reg_data[id].max_uv - reg_data[id].min_uv)
			/ pm8008_reg->rdesc.uV_step) + 1;
	pm8008_reg->rdesc.linear_ranges = reg_data[id].voltage_range;
	pm8008_reg->rdesc.n_linear_ranges = 1;
	pm8008_reg->rdesc.enable_reg = LDO_ENABLE_REG(pm8008_reg->base);
	pm8008_reg->rdesc.enable_mask = ENABLE_BIT;
	pm8008_reg->rdesc.min_dropout_uV = reg_data[id].min_dropout_uv;

	reg_config.dev = dev->parent;
	reg_config.driver_data = pm8008_reg;

	rdev = devm_regulator_register(dev, &pm8008_reg->rdesc, &reg_config);
	if (IS_ERR(rdev)) {
		rc = PTR_ERR(rdev);
		dev_err(dev, "%s: failed to register regulator rc=%d\n",
				reg_data[id].name, rc);
		return rc;
	}

	return 0;
}

static struct platform_driver pm8008_regulator_driver = {
	.driver	= {
		.name		= "qcom,pm8008-regulators",
	},
	.probe	= pm8008_regulator_probe,
};

module_platform_driver(pm8008_regulator_driver);

MODULE_DESCRIPTION("Qualcomm PM8008 PMIC Regulator Driver");
MODULE_LICENSE("GPL");
