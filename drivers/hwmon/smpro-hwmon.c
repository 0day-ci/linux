// SPDX-License-Identifier: GPL-2.0+
/*
 * Ampere Computing SoC's SMPro Hardware Monitoring Driver
 *
 * Copyright (c) 2021, Ampere Computing LLC
 */
#include <linux/bitfield.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

/* Identification Registers */
#define MANUFACTURER_ID_REG		0x02
#define AMPERE_MANUFACTURER_ID		0xCD3A

/* Logical Power Sensor Registers */
#define SOC_TEMP_REG			0x10
#define SOC_VRD_TEMP_REG		0x11
#define DIMM_VRD_TEMP_REG		0x12
#define CORE_VRD_TEMP_REG		0x13
#define CH0_DIMM_TEMP_REG		0x14
#define CH1_DIMM_TEMP_REG		0x15
#define CH2_DIMM_TEMP_REG		0x16
#define CH3_DIMM_TEMP_REG		0x17
#define CH4_DIMM_TEMP_REG		0x18
#define CH5_DIMM_TEMP_REG		0x19
#define CH6_DIMM_TEMP_REG		0x1A
#define CH7_DIMM_TEMP_REG		0x1B
#define RCA_VRD_TEMP_REG		0x1C

#define CORE_VRD_PWR_REG		0x20
#define SOC_PWR_REG			0x21
#define DIMM_VRD1_PWR_REG		0x22
#define DIMM_VRD2_PWR_REG		0x23
#define CORE_VRD_PWR_MW_REG		0x26
#define SOC_PWR_MW_REG			0x27
#define DIMM_VRD1_PWR_MW_REG		0x28
#define DIMM_VRD2_PWR_MW_REG		0x29
#define RCA_VRD_PWR_REG			0x2A
#define RCA_VRD_PWR_MW_REG		0x2B

#define MEM_HOT_THRESHOLD_REG		0x32
#define SOC_VR_HOT_THRESHOLD_REG	0x33
#define CORE_VRD_VOLT_REG		0x34
#define SOC_VRD_VOLT_REG		0x35
#define DIMM_VRD1_VOLT_REG		0x36
#define DIMM_VRD2_VOLT_REG		0x37
#define RCA_VRD_VOLT_REG		0x38

#define CORE_VRD_CURR_REG		0x39
#define SOC_VRD_CURR_REG		0x3A
#define DIMM_VRD1_CURR_REG		0x3B
#define DIMM_VRD2_CURR_REG		0x3C
#define RCA_VRD_CURR_REG		0x3D

struct smpro_hwmon {
	struct regmap *regmap;
};

struct smpro_sensor {
	const u8 reg;
	const u8 reg_ext;
	const char *label;
};

static const struct smpro_sensor temperature[] = {
	{
		.reg = SOC_TEMP_REG,
		.label = "temp1 SoC"
	},
	{
		.reg = SOC_VRD_TEMP_REG,
		.label = "temp2 SoC VRD"
	},
	{
		.reg = DIMM_VRD_TEMP_REG,
		.label = "temp3 DIMM VRD"
	},
	{
		.reg = CORE_VRD_TEMP_REG,
		.label = "temp4 CORE VRD"
	},
	{
		.reg = CH0_DIMM_TEMP_REG,
		.label = "temp5 CH0 DIMM"
	},
	{
		.reg = CH1_DIMM_TEMP_REG,
		.label = "temp6 CH1 DIMM"
	},
	{
		.reg = CH2_DIMM_TEMP_REG,
		.label = "temp7 CH2 DIMM"
	},
	{
		.reg = CH3_DIMM_TEMP_REG,
		.label = "temp8 CH3 DIMM"
	},
	{
		.reg = CH4_DIMM_TEMP_REG,
		.label = "temp9 CH4 DIMM"
	},
	{
		.reg = CH5_DIMM_TEMP_REG,
		.label = "temp10 CH5 DIMM"
	},
	{
		.reg = CH6_DIMM_TEMP_REG,
		.label = "temp11 CH6 DIMM"
	},
	{
		.reg = CH7_DIMM_TEMP_REG,
		.label = "temp12 CH7 DIMM"
	},
	{
		.reg = RCA_VRD_TEMP_REG,
		.label = "temp13 RCA VRD"
	},
};

static const struct smpro_sensor voltage[] = {
	{
		.reg = CORE_VRD_VOLT_REG,
		.label = "vout0 CORE VRD"
	},
	{
		.reg = SOC_VRD_VOLT_REG,
		.label = "vout1 SoC VRD"
	},
	{
		.reg = DIMM_VRD1_VOLT_REG,
		.label = "vout2 DIMM VRD1"
	},
	{
		.reg = DIMM_VRD2_VOLT_REG,
		.label = "vout3 DIMM VRD2"
	},
	{
		.reg = RCA_VRD_VOLT_REG,
		.label = "vout4 RCA VRD"
	},
};

static const struct smpro_sensor curr_sensor[] = {
	{
		.reg = CORE_VRD_CURR_REG,
		.label = "iout1 CORE VRD"
	},
	{
		.reg = SOC_VRD_CURR_REG,
		.label = "iout2 SoC VRD"
	},
	{
		.reg = DIMM_VRD1_CURR_REG,
		.label = "iout3 DIMM VRD1"
	},
	{
		.reg = DIMM_VRD2_CURR_REG,
		.label = "iout4 DIMM VRD2"
	},
	{
		.reg = RCA_VRD_CURR_REG,
		.label = "iout5 RCA VRD"
	},
};

static const struct smpro_sensor power[] = {
	{
		.reg = CORE_VRD_PWR_REG,
		.reg_ext = CORE_VRD_PWR_MW_REG,
		.label = "power1 CORE VRD"
	},
	{
		.reg = SOC_PWR_REG,
		.reg_ext = SOC_PWR_MW_REG,
		.label = "power2 SoC"
	},
	{
		.reg = DIMM_VRD1_PWR_REG,
		.reg_ext = DIMM_VRD1_PWR_MW_REG,
		.label = "power3 DIMM VRD1"
	},
	{
		.reg = DIMM_VRD2_PWR_REG,
		.reg_ext = DIMM_VRD2_PWR_MW_REG,
		.label = "power4 DIMM VRD2"
	},
	{
		.reg = RCA_VRD_PWR_REG,
		.reg_ext = RCA_VRD_PWR_MW_REG,
		.label = "power5 RCA VRD"
	},
};

static int smpro_read_temp(struct device *dev, u32 attr, int channel, long *val)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_temp_input:
		ret = regmap_read(hwmon->regmap,
				  temperature[channel].reg, &value);
		if (ret)
			return ret;
		*val = (value & 0x1ff) * 1000;
		break;
	case hwmon_temp_crit:
		if (temperature[channel].reg == SOC_VRD_TEMP_REG) {
			ret = regmap_read(hwmon->regmap, SOC_VR_HOT_THRESHOLD_REG, &value);
			if (ret)
				return ret;
			*val = (value & 0x1ff) * 1000;
		} else {
			/* Report same MEM HOT threshold across DIMM channels */
			ret = regmap_read(hwmon->regmap, MEM_HOT_THRESHOLD_REG, &value);
			if (ret)
				return ret;
			*val = (value & 0x1ff) * 1000;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int smpro_read_in(struct device *dev, u32 attr, int channel, long *val)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_in_input:
		ret = regmap_read(hwmon->regmap, voltage[channel].reg, &value);
		if (ret < 0)
			return ret;
		/* Scale reported by the hardware is 1mV */
		*val = value & 0x7fff;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_read_curr(struct device *dev, u32 attr, int channel, long *val)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	switch (attr) {
	case hwmon_curr_input:
		ret = regmap_read(hwmon->regmap, curr_sensor[channel].reg, &value);
		if (ret < 0)
			return ret;
		/* Scale reported by the hardware is 1mA */
		*val = value & 0x7fff;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_read_power(struct device *dev, u32 attr, int channel, long *val_pwr)
{
	struct smpro_hwmon *hwmon = dev_get_drvdata(dev);
	unsigned int val = 0, val_mw = 0;
	int ret;

	switch (attr) {
	case hwmon_power_input:
		ret = regmap_read(hwmon->regmap, power[channel].reg, &val);
		if (ret)
			return ret;

		ret = regmap_read(hwmon->regmap, power[channel].reg_ext, &val_mw);
		if (ret)
			return ret;

		*val_pwr = val * 1000000 + val_mw * 1000;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		return smpro_read_temp(dev, attr, channel, val);
	case hwmon_in:
		return smpro_read_in(dev, attr, channel, val);
	case hwmon_power:
		return smpro_read_power(dev, attr, channel, val);
	case hwmon_curr:
		return smpro_read_curr(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int smpro_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	return -EOPNOTSUPP;
}

static int smpro_read_string(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
			*str = temperature[channel].label;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;

	case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
			*str = voltage[channel].label;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;

	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_label:
			*str = curr_sensor[channel].label;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;

	case hwmon_power:
		switch (attr) {
		case hwmon_power_label:
			*str = power[channel].label;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static umode_t smpro_is_visible(const void *data, enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	const struct smpro_hwmon *hwmon = data;
	unsigned int value;
	int ret;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_label:
		case hwmon_temp_crit:
			ret = regmap_read(hwmon->regmap, temperature[channel].reg, &value);
			if (ret || value == 0xFFFF)
				return 0;
		break;
		}
	default:
		break;
	}

	return 0444;
}

static const struct hwmon_channel_info *smpro_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_ops smpro_hwmon_ops = {
	.is_visible = smpro_is_visible,
	.read = smpro_read,
	.write = smpro_write,
	.read_string = smpro_read_string,
};

static const struct hwmon_chip_info smpro_chip_info = {
	.ops = &smpro_hwmon_ops,
	.info = smpro_info,
};

static bool is_valid_id(struct regmap *regmap)
{
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, MANUFACTURER_ID_REG, &val);

	return  (ret || (val != AMPERE_MANUFACTURER_ID)) ? false : true;
}

static int smpro_hwmon_probe(struct platform_device *pdev)
{
	struct smpro_hwmon *hwmon;
	struct device *hwmon_dev;

	hwmon = devm_kzalloc(&pdev->dev, sizeof(struct smpro_hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!hwmon->regmap)
		return -ENODEV;

	/* Check for valid ID */
	if (!is_valid_id(hwmon->regmap))
		return -EPROBE_DEFER;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, "smpro_hwmon",
							 hwmon, &smpro_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id smpro_hwmon_of_match[] = {
	{ .compatible = "ampere,ac01-hwmon" },
	{}
};
MODULE_DEVICE_TABLE(of, smpro_hwmon_of_match);

static struct platform_driver smpro_hwmon_driver = {
	.probe		= smpro_hwmon_probe,
	.driver = {
		.name	= "smpro-hwmon",
		.of_match_table = smpro_hwmon_of_match,
	},
};

module_platform_driver(smpro_hwmon_driver);

MODULE_AUTHOR("Thu Nguyen <thu@os.amperecomputing.com>");
MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("Ampere Altra SMPro hwmon driver");
MODULE_LICENSE("GPL v2");
