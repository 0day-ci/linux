// SPDX-License-Identifier: GPL-2.0-only

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define RT6160_MODE_AUTO	0
#define RT6160_MODE_FPWM	1

#define RT6160_REG_CNTL		0x01
#define RT6160_REG_STATUS	0x02
#define RT6160_REG_DEVID	0x03
#define RT6160_REG_VSELL	0x04
#define RT6160_REG_VSELH	0x05

#define RT6160_FPWM_MASK	BIT(3)
#define RT6160_RAMPRATE_MASK	GENMASK(1, 0)
#define RT6160_VID_MASK		GENMASK(7, 4)
#define RT6160_VSEL_MASK	GENMASK(6, 0)
#define RT6160_HDSTAT_MASK	BIT(4)
#define RT6160_UVSTAT_MASK	BIT(3)
#define RT6160_OCSTAT_MASK	BIT(2)
#define RT6160_TSDSTAT_MASK	BIT(1)
#define RT6160_PGSTAT_MASK	BIT(0)

#define RT6160_RAMPRATE_1VMS	0
#define RT6160_RAMPRATE_2P5VMS	1
#define RT6160_RAMPRATE_5VMS	2
#define RT6160_RAMPRATE_10VMS	3
#define RT6160_VENDOR_ID	0xA0
#define RT6160_VOUT_MINUV	2025000
#define RT6160_VOUT_MAXUV	5200000
#define RT6160_VOUT_STPUV	25000
#define RT6160_N_VOUTS		((RT6160_VOUT_MAXUV - RT6160_VOUT_MINUV) / RT6160_VOUT_STPUV + 1)

struct rt6160_priv {
	struct regulator_desc desc;
	bool vsel_active_low;
};

static int rt6160_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mode_val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		mode_val = RT6160_FPWM_MASK;
		break;
	case REGULATOR_MODE_NORMAL:
		mode_val = 0;
		break;
	default:
		dev_err(&rdev->dev, "mode not supported\n");
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT6160_REG_CNTL, RT6160_FPWM_MASK, mode_val);
}

static unsigned int rt6160_get_mode(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, RT6160_REG_CNTL, &val);
	if (ret)
		return ret;

	if (val & RT6160_FPWM_MASK)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

static int rt6160_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct rt6160_priv *priv = rdev_get_drvdata(rdev);
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int reg = RT6160_REG_VSELH;
	int vsel;

	vsel = regulator_map_voltage_linear(rdev, uV, uV);
	if (vsel < 0)
		return vsel;

	if (priv->vsel_active_low)
		reg = RT6160_REG_VSELL;

	return regmap_update_bits(regmap, reg, RT6160_VSEL_MASK, vsel);
}

static int rt6160_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int ramp_value = RT6160_RAMPRATE_1VMS;

	switch (ramp_delay) {
	case 1 ... 1000:
		ramp_value = RT6160_RAMPRATE_1VMS;
		break;
	case 1001 ... 2500:
		ramp_value = RT6160_RAMPRATE_2P5VMS;
		break;
	case 2501 ... 5000:
		ramp_value = RT6160_RAMPRATE_5VMS;
		break;
	case 5001 ... 10000:
		ramp_value = RT6160_RAMPRATE_10VMS;
		break;
	default:
		dev_warn(&rdev->dev, "ramp_delay %d not supported, setting 1000\n", ramp_delay);
	}

	return regmap_update_bits(regmap, RT6160_REG_CNTL, RT6160_RAMPRATE_MASK, ramp_value);
}

static int rt6160_get_error_flags(struct regulator_dev *rdev, unsigned int *flags)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int val, events = 0;
	int ret;

	ret = regmap_read(regmap, RT6160_REG_STATUS, &val);
	if (ret)
		return ret;

	if (val & (RT6160_HDSTAT_MASK | RT6160_TSDSTAT_MASK))
		events |= REGULATOR_ERROR_OVER_TEMP;

	if (val & RT6160_UVSTAT_MASK)
		events |= REGULATOR_ERROR_UNDER_VOLTAGE;

	if (val & RT6160_OCSTAT_MASK)
		events |= REGULATOR_ERROR_OVER_CURRENT;

	if (val & RT6160_PGSTAT_MASK)
		events |= REGULATOR_ERROR_FAIL;

	*flags = events;
	return 0;
}

static const struct regulator_ops rt6160_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,

	.set_mode = rt6160_set_mode,
	.get_mode = rt6160_get_mode,
	.set_suspend_voltage = rt6160_set_suspend_voltage,
	.set_ramp_delay = rt6160_set_ramp_delay,
	.get_error_flags = rt6160_get_error_flags,
};

static unsigned int rt6160_of_map_mode(unsigned int mode)
{
	if (mode == RT6160_MODE_FPWM)
		return REGULATOR_MODE_FAST;
	else if (mode == RT6160_MODE_AUTO)
		return REGULATOR_MODE_NORMAL;

	return REGULATOR_MODE_INVALID;
}

static bool rt6160_is_accessible_reg(struct device *dev, unsigned int reg)
{
	if (reg >= RT6160_REG_CNTL && reg <= RT6160_REG_VSELH)
		return true;
	return false;
}

static const struct regmap_config rt6160_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT6160_REG_VSELH,

	.writeable_reg = rt6160_is_accessible_reg,
	.readable_reg = rt6160_is_accessible_reg,
};

static int rt6160_probe(struct i2c_client *i2c)
{
	struct rt6160_priv *priv;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct regulator_config regulator_cfg = {};
	struct regulator_dev *rdev;
	unsigned int devid;
	int ret;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->vsel_active_low = device_property_present(&i2c->dev, "richtek,vsel-active-low");

	enable_gpio = devm_gpiod_get_optional(&i2c->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable_gpio)) {
		dev_err(&i2c->dev, "Failed to get 'enable' gpio\n");
		return PTR_ERR(enable_gpio);
	}

	regmap = devm_regmap_init_i2c(i2c, &rt6160_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&i2c->dev, "Failed to init regmap\n");
		return PTR_ERR(regmap);
	}

	ret = regmap_read(regmap, RT6160_REG_DEVID, &devid);
	if (ret)
		return ret;

	if ((devid & RT6160_VID_MASK) != RT6160_VENDOR_ID) {
		dev_err(&i2c->dev, "VID not correct [0x%02x]\n", devid);
		return -ENODEV;
	}

	priv->desc.name = "rt6160-buckboost";
	priv->desc.type = REGULATOR_VOLTAGE;
	priv->desc.owner = THIS_MODULE;
	priv->desc.min_uV = RT6160_VOUT_MINUV;
	priv->desc.uV_step = RT6160_VOUT_STPUV;
	priv->desc.vsel_reg = RT6160_REG_VSELH;
	priv->desc.vsel_mask = RT6160_VSEL_MASK;
	priv->desc.n_voltages = RT6160_N_VOUTS;
	priv->desc.of_map_mode = rt6160_of_map_mode;
	priv->desc.ops = &rt6160_regulator_ops;
	if (priv->vsel_active_low)
		priv->desc.vsel_reg = RT6160_REG_VSELL;

	regulator_cfg.dev = &i2c->dev;
	regulator_cfg.of_node = i2c->dev.of_node;
	regulator_cfg.regmap = regmap;
	regulator_cfg.driver_data = priv;
	regulator_cfg.init_data = of_get_regulator_init_data(&i2c->dev, i2c->dev.of_node,
							     &priv->desc);

	rdev = devm_regulator_register(&i2c->dev, &priv->desc, &regulator_cfg);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register regulator\n");
		return PTR_ERR(rdev);
	}

	return 0;
}

static const struct of_device_id __maybe_unused rt6160_of_match_table[] = {
	{ .compatible = "richtek,rt6160", },
	{}
};
MODULE_DEVICE_TABLE(of, rt6160_of_match_table);

static struct i2c_driver rt6160_driver = {
	.driver = {
		.name = "rt6160",
		.of_match_table = rt6160_of_match_table,
	},
	.probe_new = rt6160_probe,
};
module_i2c_driver(rt6160_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_LICENSE("GPL v2");
