/*
 * Battery power supply driver for X-Powers AXP20X and AXP22X PMICs
 *
 * Copyright 2016 Free Electrons NextThing Co.
 *	Quentin Schulz <quentin.schulz@free-electrons.com>
 *
 * This driver is based on a previous upstreaming attempt by:
 *	Bruno Prémont <bonbons@linux-vserver.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>
#include <linux/mfd/axp20x.h>
#include <linux/reboot.h>

#define AXP20X_PWR_STATUS_BAT_CHARGING	BIT(2)

#define AXP20X_PWR_OP_BATT_PRESENT	BIT(5)
#define AXP20X_PWR_OP_BATT_ACTIVATED	BIT(3)

#define AXP209_FG_PERCENT		GENMASK(6, 0)
#define AXP22X_FG_VALID			BIT(7)

#define AXP20X_CHRG_CTRL1_ENABLE	BIT(7)
#define AXP20X_CHRG_CTRL1_TGT_VOLT	GENMASK(6, 5)
#define AXP20X_CHRG_CTRL1_TGT_4_1V	(0 << 5)
#define AXP20X_CHRG_CTRL1_TGT_4_15V	(1 << 5)
#define AXP20X_CHRG_CTRL1_TGT_4_2V	(2 << 5)
#define AXP20X_CHRG_CTRL1_TGT_4_36V	(3 << 5)

#define AXP22X_CHRG_CTRL1_TGT_4_22V	(1 << 5)
#define AXP22X_CHRG_CTRL1_TGT_4_24V	(3 << 5)

#define AXP813_CHRG_CTRL1_TGT_4_35V	(3 << 5)

#define AXP20X_CHRG_CTRL1_TGT_CURR	GENMASK(3, 0)

#define AXP20X_V_OFF_MASK		GENMASK(2, 0)

#define AXP20X_APS_WARN_MASK		GENMASK(7, 0)

#define AXP20X_TEMP_MASK		GENMASK(7, 0)

#define AXP20X_ADC_TS_RATE_MASK		GENMASK(7, 6)
#define AXP20X_ADC_TS_RATE_25Hz		(0 << 6)
#define AXP20X_ADC_TS_RATE_50Hz		(1 << 6)
#define AXP20X_ADC_TS_RATE_100Hz	(2 << 6)
#define AXP20X_ADC_TS_RATE_200Hz	(3 << 6)

#define AXP20X_ADC_TS_CURRENT_MASK	GENMASK(5, 4)
#define AXP20X_ADC_TS_CURRENT_20uA	(0 << 4)
#define AXP20X_ADC_TS_CURRENT_40uA	(1 << 4)
#define AXP20X_ADC_TS_CURRENT_60uA	(2 << 4)
#define AXP20X_ADC_TS_CURRENT_80uA	(3 << 4)


#define DRVNAME "axp20x-battery-power-supply"

struct axp20x_batt_ps;

struct axp_data {
	int	ccc_scale;
	int	ccc_offset;
	bool	has_fg_valid;
	int	(*get_max_voltage)(struct axp20x_batt_ps *batt, int *val);
	int	(*set_max_voltage)(struct axp20x_batt_ps *batt, int val);
};

struct axp20x_batt_ps {
	struct regmap *regmap;
	struct power_supply *batt;
	struct device *dev;
	struct iio_channel *batt_chrg_i;
	struct iio_channel *batt_dischrg_i;
	struct iio_channel *batt_v;
	/* Maximum constant charge current */
	unsigned int max_ccc;
	const struct axp_data	*data;
};

/*
 * OCV curve has fixed values and percentage can be adjusted, this array represents
 * the fixed values in uV
 */
const int axp20x_ocv_values_uV[AXP20X_OCV_MAX + 1] = {
	3132800,
	3273600,
	3414400,
	3555200,
	3625600,
	3660800,
	3696000,
	3731200,
	3766400,
	3801600,
	3836800,
	3872000,
	3942400,
	4012800,
	4083200,
	4153600,
};

static irqreturn_t axp20x_battery_power_irq(int irq, void *devid)
{
	struct axp20x_batt_ps *axp20x_batt = devid;

	power_supply_changed(axp20x_batt->batt);

	return IRQ_HANDLED;
}

static irqreturn_t axp20x_battery_low_voltage_alert1_irq(int irq, void *devid)
{
	struct axp20x_batt_ps *axp20x_batt = devid;

	dev_warn(axp20x_batt->dev, "Battery voltage low!");

	return IRQ_HANDLED;
}


static irqreturn_t axp20x_battery_low_voltage_alert2_irq(int irq, void *devid)
{
	struct axp20x_batt_ps *axp20x_batt = devid;

	dev_emerg(axp20x_batt->dev, "Battery voltage very low! Iniatializing shutdown.");

	orderly_poweroff(true);

	return IRQ_HANDLED;
}

static irqreturn_t axp20x_battery_temperature_low_irq(int irq, void *devid)
{
	struct axp20x_batt_ps *axp20x_batt = devid;

	dev_crit(axp20x_batt->dev, "Battery temperature to low!");

	return IRQ_HANDLED;
}


static irqreturn_t axp20x_battery_temperature_high_irq(int irq, void *devid)
{
	struct axp20x_batt_ps *axp20x_batt = devid;

	dev_crit(axp20x_batt->dev, "Battery temperature to high!");

	return IRQ_HANDLED;
}


static int axp20x_battery_get_max_voltage(struct axp20x_batt_ps *axp20x_batt,
					  int *val)
{
	int ret, reg;

	ret = regmap_read(axp20x_batt->regmap, AXP20X_CHRG_CTRL1, &reg);
	if (ret)
		return ret;

	switch (reg & AXP20X_CHRG_CTRL1_TGT_VOLT) {
	case AXP20X_CHRG_CTRL1_TGT_4_1V:
		*val = 4100000;
		break;
	case AXP20X_CHRG_CTRL1_TGT_4_15V:
		*val = 4150000;
		break;
	case AXP20X_CHRG_CTRL1_TGT_4_2V:
		*val = 4200000;
		break;
	case AXP20X_CHRG_CTRL1_TGT_4_36V:
		*val = 4360000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int axp22x_battery_get_max_voltage(struct axp20x_batt_ps *axp20x_batt,
					  int *val)
{
	int ret, reg;

	ret = regmap_read(axp20x_batt->regmap, AXP20X_CHRG_CTRL1, &reg);
	if (ret)
		return ret;

	switch (reg & AXP20X_CHRG_CTRL1_TGT_VOLT) {
	case AXP20X_CHRG_CTRL1_TGT_4_1V:
		*val = 4100000;
		break;
	case AXP20X_CHRG_CTRL1_TGT_4_2V:
		*val = 4200000;
		break;
	case AXP22X_CHRG_CTRL1_TGT_4_22V:
		*val = 4220000;
		break;
	case AXP22X_CHRG_CTRL1_TGT_4_24V:
		*val = 4240000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int axp813_battery_get_max_voltage(struct axp20x_batt_ps *axp20x_batt,
					  int *val)
{
	int ret, reg;

	ret = regmap_read(axp20x_batt->regmap, AXP20X_CHRG_CTRL1, &reg);
	if (ret)
		return ret;

	switch (reg & AXP20X_CHRG_CTRL1_TGT_VOLT) {
	case AXP20X_CHRG_CTRL1_TGT_4_1V:
		*val = 4100000;
		break;
	case AXP20X_CHRG_CTRL1_TGT_4_15V:
		*val = 4150000;
		break;
	case AXP20X_CHRG_CTRL1_TGT_4_2V:
		*val = 4200000;
		break;
	case AXP813_CHRG_CTRL1_TGT_4_35V:
		*val = 4350000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int axp20x_get_constant_charge_current(struct axp20x_batt_ps *axp,
					      int *val)
{
	int ret;

	ret = regmap_read(axp->regmap, AXP20X_CHRG_CTRL1, val);
	if (ret)
		return ret;

	*val &= AXP20X_CHRG_CTRL1_TGT_CURR;

	*val = *val * axp->data->ccc_scale + axp->data->ccc_offset;

	return 0;
}

static int axp20x_battery_set_ocv_table(struct axp20x_batt_ps *axp_batt,
					struct power_supply_battery_ocv_table ocv_table[AXP20X_OCV_MAX+1],
					int ocv_table_size)
{
	int ret, i, error = 0;

	if (ocv_table_size != AXP20X_OCV_MAX+1)
		return 1;

	for (i = 0; i < ocv_table_size; i++) {
		ret = regmap_update_bits(axp_batt->regmap, AXP20X_OCV(i),
			GENMASK(7, 0), ocv_table[i].capacity);

		if (ret)
			error = ret;
	}

	return error;
}

static int axp20x_battery_set_voltage_low_alert1(struct axp20x_batt_ps *axp_batt,
					 int voltage_alert)
{
	int ret;
	/* converts the warning voltage level in uV to the neeeded reg value */
	int val1 = (voltage_alert - 2867200) / (1400 * 4);

	if (val1 < 0 || val1 > AXP20X_APS_WARN_MASK)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_APS_WARN_L1,
				  AXP20X_APS_WARN_MASK, val1);

	return ret;
}

static int axp20x_battery_get_voltage_low_alert1(struct axp20x_batt_ps *axp_batt,
						 int *voltage_alert)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_APS_WARN_L1, &reg);
	if (ret)
		return ret;

	/* converts the reg value to warning voltage level in uV */
	*voltage_alert = 2867200 + (1400 * (reg & AXP20X_APS_WARN_MASK) * 4);

	return ret;
}

static int axp20x_battery_set_voltage_low_alert2(struct axp20x_batt_ps *axp_batt,
					 int voltage_alert)
{
	int ret;

	/* converts the warning voltage level in uV to the neeeded reg value */
	int val1 = (voltage_alert - 2867200) / (1400 * 4);

	if (val1 < 0 || val1 > AXP20X_APS_WARN_MASK)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_APS_WARN_L2,
				  AXP20X_APS_WARN_MASK, val1);

	return ret;
}

static int axp20x_battery_get_voltage_low_alert2(struct axp20x_batt_ps *axp_batt,
						 int *voltage_alert)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_APS_WARN_L2, &reg);
	if (ret)
		return ret;

	/* converts the reg value to warning voltage level in uV */
	*voltage_alert = 2867200 + (1400 * (reg & AXP20X_APS_WARN_MASK) * 4);

	return ret;
}

static int axp20x_battery_set_temperature_sense_current(struct axp20x_batt_ps *axp_batt,
							int sense_current)
{
	int ret;
	int reg = -1;

	switch (sense_current) {
	case 20:
		reg = AXP20X_ADC_TS_CURRENT_20uA;
		break;
	case 40:
		reg = AXP20X_ADC_TS_CURRENT_40uA;
		break;
	case 60:
		reg = AXP20X_ADC_TS_CURRENT_60uA;
		break;
	case 80:
		reg = AXP20X_ADC_TS_CURRENT_80uA;
		break;
	default:
		return -EINVAL;
	}

	if (reg < 0)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_ADC_RATE,
				  AXP20X_ADC_TS_CURRENT_MASK, reg);

	return ret;
}

static int axp20x_battery_get_temperature_sense_current(struct axp20x_batt_ps *axp_batt,
							int *sense_current)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_ADC_RATE, &reg);
	if (ret)
		return ret;

	reg = reg & AXP20X_ADC_TS_CURRENT_MASK;

	switch (reg) {
	case AXP20X_ADC_TS_CURRENT_20uA:
		*sense_current = 20;
		break;
	case AXP20X_ADC_TS_CURRENT_40uA:
		*sense_current = 40;
		break;
	case AXP20X_ADC_TS_CURRENT_60uA:
		*sense_current = 60;
		break;
	case AXP20X_ADC_TS_CURRENT_80uA:
		*sense_current = 80;
		break;
	default:
		*sense_current = -1;
		return -EINVAL;
	}

	return ret;
}

static int axp20x_battery_set_temperature_sense_rate(struct axp20x_batt_ps *axp_batt,
						     int sample_rate)
{
	int ret;
	int reg = -1;

	switch (sample_rate) {
	case 25:
		reg = AXP20X_ADC_TS_RATE_25Hz;
		break;
	case 50:
		reg = AXP20X_ADC_TS_RATE_50Hz;
		break;
	case 100:
		reg = AXP20X_ADC_TS_RATE_100Hz;
		break;
	case 200:
		reg = AXP20X_ADC_TS_RATE_200Hz;
		break;
	default:
		return -EINVAL;
	}

	if (reg < 0)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_ADC_RATE,
				  AXP20X_ADC_TS_RATE_MASK, reg);

	return ret;
}

static int axp20x_battery_get_temperature_sense_rate(struct axp20x_batt_ps *axp_batt,
						     int *sample_rate)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_ADC_RATE, &reg);
	if (ret)
		return ret;

	reg = reg & AXP20X_ADC_TS_RATE_MASK;

	switch (reg) {
	case AXP20X_ADC_TS_RATE_25Hz:
		*sample_rate = 25;
		break;
	case AXP20X_ADC_TS_RATE_50Hz:
		*sample_rate = 50;
		break;
	case AXP20X_ADC_TS_RATE_100Hz:
		*sample_rate = 100;
		break;
	case AXP20X_ADC_TS_RATE_200Hz:
		*sample_rate = 200;
		break;
	default:
		*sample_rate = -1;
		return -EINVAL;
	}

	return ret;
}

static int axp20x_battery_set_temperature_discharge_voltage_min(struct axp20x_batt_ps *axp_batt,
								int voltage)
{
	int ret;

	int val1 = voltage / (0x10 * 800);

	if (val1 < 0 || val1 > AXP20X_TEMP_MASK)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_V_LTF_DISCHRG,
				  AXP20X_TEMP_MASK, val1);

	return ret;
}

static int axp20x_battery_get_temperature_discharge_voltage_min(struct axp20x_batt_ps *axp_batt,
								int *voltage)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_V_LTF_DISCHRG, &reg);
	if (ret)
		return ret;

	*voltage = reg * 0x10 * 800;

	return ret;
}

static int axp20x_battery_set_temperature_discharge_voltage_max(struct axp20x_batt_ps *axp_batt,
								int voltage)
{
	int ret;

	int val1 = voltage / (0x10 * 800);

	if (val1 < 0 || val1 > AXP20X_TEMP_MASK)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_V_HTF_DISCHRG,
				  AXP20X_TEMP_MASK, val1);

	return ret;
}

static int axp20x_battery_get_temperature_discharge_voltage_max(struct axp20x_batt_ps *axp_batt,
								int *voltage)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_V_HTF_DISCHRG, &reg);
	if (ret)
		return ret;

	*voltage = reg * 0x10 * 800;

	return ret;
}

static int axp20x_battery_set_temperature_charge_voltage_min(struct axp20x_batt_ps *axp_batt,
							     int voltage)
{
	int ret;

	int val1 = voltage / (0x10 * 800);

	if (val1 < 0 || val1 > AXP20X_TEMP_MASK)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_V_LTF_CHRG,
				  AXP20X_TEMP_MASK, val1);

	return ret;
}

static int axp20x_battery_get_temperature_charge_voltage_min(struct axp20x_batt_ps *axp_batt,
							     int *voltage)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_V_LTF_CHRG, &reg);
	if (ret)
		return ret;

	*voltage = reg * 0x10 * 800;

	return ret;
}

static int axp20x_battery_set_temperature_charge_voltage_max(struct axp20x_batt_ps *axp_batt,
							     int voltage)
{
	int ret;

	int val1 = voltage / (0x10 * 800);

	if (val1 < 0 || val1 > AXP20X_TEMP_MASK)
		return -EINVAL;

	ret = regmap_update_bits(axp_batt->regmap, AXP20X_V_HTF_CHRG,
				  AXP20X_TEMP_MASK, val1);

	return ret;
}

static int axp20x_battery_get_temperature_charge_voltage_max(struct axp20x_batt_ps *axp_batt,
							     int *voltage)
{
	int reg, ret;

	ret = regmap_read(axp_batt->regmap, AXP20X_V_HTF_CHRG, &reg);
	if (ret)
		return ret;

	*voltage = reg * 0x10 * 800;

	return ret;
}

static int axp20x_battery_get_temp_sense_voltage_now(struct axp20x_batt_ps *axp_batt,
						     int *voltage)
{
	int reg, ret, val1;

	ret = regmap_read(axp_batt->regmap, AXP20X_TS_IN_L, &reg);
	if (ret)
		return ret;

	val1 = reg;

	ret = regmap_read(axp_batt->regmap, AXP20X_TS_IN_H, &reg);
	if (ret)
		return ret;

	/* merging high and low value */
	val1 = (reg << 4) | val1;

	/* convert register value to real uV */
	*voltage = val1 * 800;

	return ret;
}

static int axp20x_battery_get_prop(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	struct iio_channel *chan;
	int ret = 0, reg, val1;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		ret = regmap_read(axp20x_batt->regmap, AXP20X_PWR_OP_MODE,
				  &reg);
		if (ret)
			return ret;

		val->intval = !!(reg & AXP20X_PWR_OP_BATT_PRESENT);
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = regmap_read(axp20x_batt->regmap, AXP20X_PWR_INPUT_STATUS,
				  &reg);
		if (ret)
			return ret;

		if (reg & AXP20X_PWR_STATUS_BAT_CHARGING) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			return 0;
		}

		ret = iio_read_channel_processed(axp20x_batt->batt_dischrg_i,
						 &val1);
		if (ret)
			return ret;

		if (val1) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			return 0;
		}

		ret = regmap_read(axp20x_batt->regmap, AXP20X_FG_RES, &val1);
		if (ret)
			return ret;

		/*
		 * Fuel Gauge data takes 7 bits but the stored value seems to be
		 * directly the raw percentage without any scaling to 7 bits.
		 */
		if ((val1 & AXP209_FG_PERCENT) == 100)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		ret = regmap_read(axp20x_batt->regmap, AXP20X_PWR_OP_MODE,
				  &val1);
		if (ret)
			return ret;

		if (val1 & AXP20X_PWR_OP_BATT_ACTIVATED) {
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
			return 0;
		}

		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = axp20x_get_constant_charge_current(axp20x_batt,
							 &val->intval);
		if (ret)
			return ret;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = axp20x_batt->max_ccc;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = regmap_read(axp20x_batt->regmap, AXP20X_PWR_INPUT_STATUS,
				  &reg);
		if (ret)
			return ret;

		if (reg & AXP20X_PWR_STATUS_BAT_CHARGING)
			chan = axp20x_batt->batt_chrg_i;
		else
			chan = axp20x_batt->batt_dischrg_i;

		ret = iio_read_channel_processed(chan, &val->intval);
		if (ret)
			return ret;

		/* IIO framework gives mA but Power Supply framework gives uA */
		val->intval *= 1000;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		/* When no battery is present, return capacity is 100% */
		ret = regmap_read(axp20x_batt->regmap, AXP20X_PWR_OP_MODE,
				  &reg);
		if (ret)
			return ret;

		if (!(reg & AXP20X_PWR_OP_BATT_PRESENT)) {
			val->intval = 100;
			return 0;
		}

		ret = regmap_read(axp20x_batt->regmap, AXP20X_FG_RES, &reg);
		if (ret)
			return ret;

		if (axp20x_batt->data->has_fg_valid && !(reg & AXP22X_FG_VALID))
			return -EINVAL;

		/*
		 * Fuel Gauge data takes 7 bits but the stored value seems to be
		 * directly the raw percentage without any scaling to 7 bits.
		 */
		val->intval = reg & AXP209_FG_PERCENT;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		return axp20x_batt->data->get_max_voltage(axp20x_batt,
							  &val->intval);

	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		ret = regmap_read(axp20x_batt->regmap, AXP20X_V_OFF, &reg);
		if (ret)
			return ret;

		val->intval = 2600000 + 100000 * (reg & AXP20X_V_OFF_MASK);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = iio_read_channel_processed(axp20x_batt->batt_v,
						 &val->intval);
		if (ret)
			return ret;

		/* IIO framework gives mV but Power Supply framework gives uV */
		val->intval *= 1000;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int axp22x_battery_set_max_voltage(struct axp20x_batt_ps *axp20x_batt,
					  int val)
{
	switch (val) {
	case 4100000:
		val = AXP20X_CHRG_CTRL1_TGT_4_1V;
		break;

	case 4200000:
		val = AXP20X_CHRG_CTRL1_TGT_4_2V;
		break;

	default:
		/*
		 * AXP20x max voltage can be set to 4.36V and AXP22X max voltage
		 * can be set to 4.22V and 4.24V, but these voltages are too
		 * high for Lithium based batteries (AXP PMICs are supposed to
		 * be used with these kinds of battery).
		 */
		return -EINVAL;
	}

	return regmap_update_bits(axp20x_batt->regmap, AXP20X_CHRG_CTRL1,
				  AXP20X_CHRG_CTRL1_TGT_VOLT, val);
}

static int axp20x_battery_set_max_voltage(struct axp20x_batt_ps *axp20x_batt,
					  int val)
{
	switch (val) {
	case 4100000:
		val = AXP20X_CHRG_CTRL1_TGT_4_1V;
		break;

	case 4150000:
		val = AXP20X_CHRG_CTRL1_TGT_4_15V;
		break;

	case 4200000:
		val = AXP20X_CHRG_CTRL1_TGT_4_2V;
		break;

	default:
		/*
		 * AXP20x max voltage can be set to 4.36V and AXP22X max voltage
		 * can be set to 4.22V and 4.24V, but these voltages are too
		 * high for Lithium based batteries (AXP PMICs are supposed to
		 * be used with these kinds of battery).
		 */
		return -EINVAL;
	}

	return regmap_update_bits(axp20x_batt->regmap, AXP20X_CHRG_CTRL1,
				  AXP20X_CHRG_CTRL1_TGT_VOLT, val);
}

static int axp20x_set_constant_charge_current(struct axp20x_batt_ps *axp_batt,
					      int charge_current)
{
	if (charge_current > axp_batt->max_ccc)
		return -EINVAL;

	charge_current = (charge_current - axp_batt->data->ccc_offset) /
		axp_batt->data->ccc_scale;

	if (charge_current > AXP20X_CHRG_CTRL1_TGT_CURR || charge_current < 0)
		return -EINVAL;

	return regmap_update_bits(axp_batt->regmap, AXP20X_CHRG_CTRL1,
				  AXP20X_CHRG_CTRL1_TGT_CURR, charge_current);
}

static int axp20x_set_max_constant_charge_current(struct axp20x_batt_ps *axp,
						  int charge_current)
{
	bool lower_max = false;

	charge_current = (charge_current - axp->data->ccc_offset) /
		axp->data->ccc_scale;

	if (charge_current > AXP20X_CHRG_CTRL1_TGT_CURR || charge_current < 0)
		return -EINVAL;

	charge_current = charge_current * axp->data->ccc_scale +
		axp->data->ccc_offset;

	if (charge_current > axp->max_ccc)
		dev_warn(axp->dev,
			 "Setting max constant charge current higher than previously defined. Note that increasing the constant charge current may damage your battery.\n");
	else
		lower_max = true;

	axp->max_ccc = charge_current;

	if (lower_max) {
		int current_cc;

		axp20x_get_constant_charge_current(axp, &current_cc);
		if (current_cc > charge_current)
			axp20x_set_constant_charge_current(axp, charge_current);
	}

	return 0;
}
static int axp20x_set_voltage_min_design(struct axp20x_batt_ps *axp_batt,
					 int min_voltage)
{
	int val1 = (min_voltage - 2600000) / 100000;

	if (val1 < 0 || val1 > AXP20X_V_OFF_MASK)
		return -EINVAL;

	return regmap_update_bits(axp_batt->regmap, AXP20X_V_OFF,
				  AXP20X_V_OFF_MASK, val1);
}

static int axp20x_battery_set_prop(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		return axp20x_set_voltage_min_design(axp20x_batt, val->intval);

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		return axp20x_batt->data->set_max_voltage(axp20x_batt,
							  val->intval);

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return axp20x_set_constant_charge_current(axp20x_batt,
							  val->intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return axp20x_set_max_constant_charge_current(axp20x_batt,
							      val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		switch (val->intval) {
		case POWER_SUPPLY_STATUS_CHARGING:
			return regmap_update_bits(axp20x_batt->regmap,
						  AXP20X_CHRG_CTRL1,
						  AXP20X_CHRG_CTRL1_ENABLE,
						  AXP20X_CHRG_CTRL1_ENABLE);

		case POWER_SUPPLY_STATUS_DISCHARGING:
		case POWER_SUPPLY_STATUS_NOT_CHARGING:
			return regmap_update_bits(axp20x_batt->regmap,
						  AXP20X_CHRG_CTRL1,
						  AXP20X_CHRG_CTRL1_ENABLE, 0);
		}
		fallthrough;
	default:
		return -EINVAL;
	}
}

static enum power_supply_property axp20x_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int axp20x_battery_prop_writeable(struct power_supply *psy,
					 enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_STATUS ||
	       psp == POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN ||
	       psp == POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN ||
	       psp == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT ||
	       psp == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX;
}

/* -- Custom attributes ----------------------------------------------------- */

static ssize_t voltage_low_alert_level1_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status;

	int voltage_alert;

	axp20x_battery_get_voltage_low_alert1(axp20x_batt, &voltage_alert);
	status = sprintf(buf, "%d\n", voltage_alert);

	return status;
}

static ssize_t voltage_low_alert_level1_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);

	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	unsigned long value;
	int status;

	status = kstrtoul(buf, 0, &value);
	if (status)
		return status;

	status = axp20x_battery_set_voltage_low_alert1(axp20x_batt, value);
	if (status)
		return status;

	return count;
}

DEVICE_ATTR_RW(voltage_low_alert_level1);

static ssize_t voltage_low_alert_level2_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status;

	int voltage_alert;

	axp20x_battery_get_voltage_low_alert2(axp20x_batt, &voltage_alert);
	status = sprintf(buf, "%d\n", voltage_alert);

	return status;
}

static ssize_t voltage_low_alert_level2_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	unsigned long value;
	int status;

	status = kstrtoul(buf, 0, &value);
	if (status)
		return status;

	status = axp20x_battery_set_voltage_low_alert2(axp20x_batt, value);
	if (status)
		return status;

	return count;
}

static DEVICE_ATTR_RW(voltage_low_alert_level2);

static ssize_t ocv_curve_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status, ret, reg, i;

	int ocv_curve_size = AXP20X_OCV_MAX+1;
	struct power_supply_battery_ocv_table ocv_curve[AXP20X_OCV_MAX+1];


	status = 0;
	for (i = 0; i < ocv_curve_size; i++) {
		ret = regmap_read(axp20x_batt->regmap, AXP20X_OCV(i), &reg);
		if (ret)
			status = ret;
		ocv_curve[i].capacity = reg;
		ocv_curve[i].ocv = axp20x_ocv_values_uV[i];
	}

	if (status)
		return status;

	status = 0;
	for (i = 0; i < ocv_curve_size; i++) {
		ret = sprintf(buf, "%sOCV_%d=%d\nCAP_%d=%d\n", buf, i,
			      ocv_curve[i].ocv, i, ocv_curve[i].capacity);
		if (ret)
			status = ret;
	}

	return status;
}

static DEVICE_ATTR_RO(ocv_curve);

static ssize_t temperature_sense_current_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status;

	int sense_current;

	axp20x_battery_get_temperature_sense_current(axp20x_batt, &sense_current);
	status = sprintf(buf, "%d\n", sense_current);

	return status;
}

static ssize_t temperature_sense_current_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	unsigned long value;
	int status;

	status = kstrtoul(buf, 0, &value);
	if (status)
		return status;

	status = axp20x_battery_set_temperature_sense_current(axp20x_batt, value);
	if (status)
		return status;

	return count;
}

static DEVICE_ATTR_RW(temperature_sense_current);

static ssize_t temperature_sense_rate_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status;

	int sense_rate;

	axp20x_battery_get_temperature_sense_rate(axp20x_batt, &sense_rate);
	status = sprintf(buf, "%d\n", sense_rate);

	return status;
}

static ssize_t temperature_sense_rate_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	unsigned long value;
	int status;

	status = kstrtoul(buf, 0, &value);
	if (status)
		return status;

	status = axp20x_battery_set_temperature_sense_rate(axp20x_batt, value);
	if (status)
		return status;

	return count;
}

static DEVICE_ATTR_RW(temperature_sense_rate);

static ssize_t temperature_sense_voltage_now_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status;

	int voltage;

	axp20x_battery_get_temp_sense_voltage_now(axp20x_batt, &voltage);
	status = sprintf(buf, "%d\n", voltage);

	return status;
}

static DEVICE_ATTR_RO(temperature_sense_voltage_now);

static ssize_t temperature_discharge_threshold_voltage_range_show(struct device *dev,
								  struct device_attribute *attr,
								  char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status;

	int min_voltage, max_voltage;

	axp20x_battery_get_temperature_discharge_voltage_min(axp20x_batt,
							     &min_voltage);
	axp20x_battery_get_temperature_discharge_voltage_max(axp20x_batt,
							     &max_voltage);

	status = sprintf(buf, "MIN=%d\nMAX=%d\n", min_voltage, max_voltage);

	return status;
}

static DEVICE_ATTR_RO(temperature_discharge_threshold_voltage_range);

static ssize_t temperature_charge_threshold_voltage_range_show(struct device *dev,
							       struct device_attribute *attr,
							       char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct axp20x_batt_ps *axp20x_batt = power_supply_get_drvdata(psy);
	int status;

	int min_voltage, max_voltage;

	axp20x_battery_get_temperature_charge_voltage_min(axp20x_batt,
							  &min_voltage);
	axp20x_battery_get_temperature_charge_voltage_max(axp20x_batt,
							  &max_voltage);

	status = sprintf(buf, "MIN=%d\nMAX=%d\n", min_voltage, max_voltage);

	return status;
}

static DEVICE_ATTR_RO(temperature_charge_threshold_voltage_range);

static struct attribute *axp20x_batt_attrs[] = {
	&dev_attr_voltage_low_alert_level1.attr,
	&dev_attr_voltage_low_alert_level2.attr,
	&dev_attr_ocv_curve.attr,
	&dev_attr_temperature_sense_current.attr,
	&dev_attr_temperature_sense_rate.attr,
	&dev_attr_temperature_sense_voltage_now.attr,
	&dev_attr_temperature_discharge_threshold_voltage_range.attr,
	&dev_attr_temperature_charge_threshold_voltage_range.attr,
	NULL,
};

ATTRIBUTE_GROUPS(axp20x_batt);

/* -- Custom attributes END ------------------------------------------------- */

static const struct power_supply_desc axp20x_batt_ps_desc = {
	.name = "axp20x-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = axp20x_battery_props,
	.num_properties = ARRAY_SIZE(axp20x_battery_props),
	.property_is_writeable = axp20x_battery_prop_writeable,
	.get_property = axp20x_battery_get_prop,
	.set_property = axp20x_battery_set_prop,
};

static const char * const irq_names[] = { "BATT_PLUGIN", "BATT_REMOVAL", "CHARG",
					  "CHARG_DONE", NULL };

static const struct axp_data axp209_data = {
	.ccc_scale = 100000,
	.ccc_offset = 300000,
	.get_max_voltage = axp20x_battery_get_max_voltage,
	.set_max_voltage = axp20x_battery_set_max_voltage,
};

static const struct axp_data axp221_data = {
	.ccc_scale = 150000,
	.ccc_offset = 300000,
	.has_fg_valid = true,
	.get_max_voltage = axp22x_battery_get_max_voltage,
	.set_max_voltage = axp22x_battery_set_max_voltage,
};

static const struct axp_data axp813_data = {
	.ccc_scale = 200000,
	.ccc_offset = 200000,
	.has_fg_valid = true,
	.get_max_voltage = axp813_battery_get_max_voltage,
	.set_max_voltage = axp20x_battery_set_max_voltage,
};

static const struct of_device_id axp20x_battery_ps_id[] = {
	{
		.compatible = "x-powers,axp209-battery-power-supply",
		.data = (void *)&axp209_data,
	}, {
		.compatible = "x-powers,axp221-battery-power-supply",
		.data = (void *)&axp221_data,
	}, {
		.compatible = "x-powers,axp813-battery-power-supply",
		.data = (void *)&axp813_data,
	}, { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, axp20x_battery_ps_id);

static int axp20x_power_probe(struct platform_device *pdev)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct axp20x_batt_ps *axp20x_batt;
	struct power_supply_config psy_cfg = {};
	struct power_supply_battery_info info;
	struct device *dev = &pdev->dev;
	int i, irq, ret = 0;

	if (!of_device_is_available(pdev->dev.of_node))
		return -ENODEV;

	axp20x_batt = devm_kzalloc(&pdev->dev, sizeof(*axp20x_batt),
				   GFP_KERNEL);
	if (!axp20x_batt)
		return -ENOMEM;

	axp20x_batt->dev = &pdev->dev;

	axp20x_batt->batt_v = devm_iio_channel_get(&pdev->dev, "batt_v");
	if (IS_ERR(axp20x_batt->batt_v)) {
		if (PTR_ERR(axp20x_batt->batt_v) == -ENODEV)
			return -EPROBE_DEFER;
		return PTR_ERR(axp20x_batt->batt_v);
	}

	axp20x_batt->batt_chrg_i = devm_iio_channel_get(&pdev->dev,
							"batt_chrg_i");
	if (IS_ERR(axp20x_batt->batt_chrg_i)) {
		if (PTR_ERR(axp20x_batt->batt_chrg_i) == -ENODEV)
			return -EPROBE_DEFER;
		return PTR_ERR(axp20x_batt->batt_chrg_i);
	}

	axp20x_batt->batt_dischrg_i = devm_iio_channel_get(&pdev->dev,
							   "batt_dischrg_i");
	if (IS_ERR(axp20x_batt->batt_dischrg_i)) {
		if (PTR_ERR(axp20x_batt->batt_dischrg_i) == -ENODEV)
			return -EPROBE_DEFER;
		return PTR_ERR(axp20x_batt->batt_dischrg_i);
	}

	axp20x_batt->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	platform_set_drvdata(pdev, axp20x_batt);

	psy_cfg.drv_data = axp20x_batt;
	psy_cfg.of_node = pdev->dev.of_node;
	psy_cfg.attr_grp = axp20x_batt_groups;

	axp20x_batt->data = (struct axp_data *)of_device_get_match_data(dev);

	axp20x_batt->batt = devm_power_supply_register(&pdev->dev,
						       &axp20x_batt_ps_desc,
						       &psy_cfg);
	if (IS_ERR(axp20x_batt->batt)) {
		dev_err(&pdev->dev, "failed to register power supply: %ld\n",
			PTR_ERR(axp20x_batt->batt));
		return PTR_ERR(axp20x_batt->batt);
	}

	if (!power_supply_get_battery_info(axp20x_batt->batt, &info)) {
		struct device_node *battery_np;

		int vmin = info.voltage_min_design_uv;
		int vmax = info.voltage_max_design_uv;
		int ccc = info.constant_charge_current_max_ua;
		struct power_supply_battery_ocv_table ocv_table[AXP20X_OCV_MAX+1];
		int ocv_table_size = 0;
		int lvl1 = 0;
		int lvl2 = 0;
		int temp_sense_current = 0;
		int temp_sense_rate = 0;
		int temp_discharge_min = -1;
		int temp_discharge_max = -1;
		int temp_charge_min = -1;
		int temp_charge_max = -1;

		int i = 0, j = 0;
		bool too_many_ocv_tables = false;
		bool too_many_ocv_values = false;
		bool ocv_values_mismatch = false;

		battery_np = of_parse_phandle(axp20x_batt->batt->of_node,
					      "monitored-battery", 0);

		of_property_read_u32(battery_np, "low-voltage-level1-microvolt",
				     &lvl1);
		of_property_read_u32(battery_np, "low-voltage-level2-microvolt",
				     &lvl2);
		of_property_read_u32(battery_np, "temperature-sense-current-microamp",
				     &temp_sense_current);
		of_property_read_u32(battery_np, "temperature-sense-rate-hertz",
				     &temp_sense_rate);

		of_property_read_u32_index(battery_np, "temperature-discharge-range-microvolt",
					   0, &temp_discharge_min);
		of_property_read_u32_index(battery_np, "temperature-discharge-range-microvolt",
					   1, &temp_discharge_max);

		of_property_read_u32_index(battery_np, "temperature-charge-range-microvolt",
					   0, &temp_charge_min);
		of_property_read_u32_index(battery_np, "temperature-charge-range-microvolt",
					   1, &temp_charge_max);

		if (vmin > 0 && axp20x_set_voltage_min_design(axp20x_batt,
							      vmin))
			dev_err(&pdev->dev,
				"couldn't set voltage_min_design\n");

		if (vmax > 0 && axp20x_battery_set_max_voltage(axp20x_batt,
							       vmax))
			dev_err(&pdev->dev,
				"couldn't set voltage_max_design\n");

		if (lvl1 > 0 && axp20x_battery_set_voltage_low_alert1(axp20x_batt,
								      lvl1))
			dev_err(&pdev->dev,
				"couldn't set voltage_low_alert_level1\n");

		if (lvl2 > 0 && axp20x_battery_set_voltage_low_alert2(axp20x_batt,
								      lvl2))
			dev_err(&pdev->dev,
				"couldn't set voltage_low_alert_level2\n");

		if (temp_sense_current > 0 &&
		    axp20x_battery_set_temperature_sense_current(axp20x_batt,
								 temp_sense_current))
			dev_err(&pdev->dev,
				"couldn't set temperature_sense_current\n");

		if (temp_sense_rate > 0 &&
		    axp20x_battery_set_temperature_sense_rate(axp20x_batt,
							      temp_sense_rate))
			dev_err(&pdev->dev,
				"couldn't set temperature_sense_rate\n");

		if (temp_discharge_min >= 0 &&
		    axp20x_battery_set_temperature_discharge_voltage_min(axp20x_batt,
									 temp_discharge_min))
			dev_err(&pdev->dev,
				"couldn't set temperature_sense_rate\n");

		if (temp_discharge_max >= 0 &&
		    axp20x_battery_set_temperature_discharge_voltage_max(axp20x_batt,
									 temp_discharge_max))
			dev_err(&pdev->dev,
				"couldn't set temperature_sense_rate\n");

		if (temp_charge_min >= 0 &&
		    axp20x_battery_set_temperature_charge_voltage_min(axp20x_batt,
								      temp_charge_min))
			dev_err(&pdev->dev,
				"couldn't set temperature_sense_rate\n");

		if (temp_charge_max >= 0 &&
		    axp20x_battery_set_temperature_charge_voltage_max(axp20x_batt,
								      temp_charge_max))
			dev_err(&pdev->dev,
				"couldn't set temperature_sense_rate\n");

		/* Set max to unverified value to be able to set CCC */
		axp20x_batt->max_ccc = ccc;

		if (ccc <= 0 || axp20x_set_constant_charge_current(axp20x_batt,
								   ccc)) {
			dev_err(&pdev->dev,
				"couldn't set constant charge current from DT: fallback to minimum value\n");
			ccc = 300000;
			axp20x_batt->max_ccc = ccc;
			axp20x_set_constant_charge_current(axp20x_batt, ccc);
		}

		too_many_ocv_tables = false;
		too_many_ocv_values = false;
		ocv_values_mismatch = false;
		for (i = 0; i < POWER_SUPPLY_OCV_TEMP_MAX; i++) {
			if (info.ocv_table_size[i] == -EINVAL ||
			   info.ocv_temp[i] == -EINVAL ||
			   info.ocv_table[i] == NULL)
				continue;

			if (info.ocv_table_size[i] > (AXP20X_OCV_MAX+1)) {
				too_many_ocv_values = true;
				dev_err(&pdev->dev, "Too many values in ocv table, only %d values are supported",
					AXP20X_OCV_MAX + 1);
				break;
			}

			if (i > 0) {
				too_many_ocv_tables = true;
				dev_err(&pdev->dev, "Only one ocv table is supported");
				break;
			}

			for (j = 0; j < info.ocv_table_size[i]; j++) {
				if (info.ocv_table[i][j].ocv != axp20x_ocv_values_uV[j]) {
					ocv_values_mismatch = true;
					break;
				}
			}

			if (ocv_values_mismatch) {
				dev_err(&pdev->dev, "ocv tables missmatches requirements");
				dev_info(&pdev->dev, "ocv table requires following ocv values in that order:");
				for (j = 0; j < AXP20X_OCV_MAX+1; j++) {
					dev_info(&pdev->dev, "%d uV",
						 axp20x_ocv_values_uV[j]);
				}
				break;
			}

			ocv_table_size = info.ocv_table_size[i];
			for (j = 0; j < info.ocv_table_size[i]; j++)
				ocv_table[j] = info.ocv_table[i][j];

		}

		if (!too_many_ocv_tables && !too_many_ocv_values &&
		    !ocv_values_mismatch)
			axp20x_battery_set_ocv_table(axp20x_batt, ocv_table,
						     ocv_table_size);

	}

	/*
	 * Update max CCC to a valid value if battery info is present or set it
	 * to current register value by default.
	 */
	axp20x_get_constant_charge_current(axp20x_batt,
					   &axp20x_batt->max_ccc);

	/* Request irqs after registering, as irqs may trigger immediately */
	for (i = 0; irq_names[i]; i++) {
		irq = platform_get_irq_byname(pdev, irq_names[i]);
		if (irq < 0) {
			dev_warn(&pdev->dev, "No IRQ for %s: %d\n",
				 irq_names[i], irq);
			continue;
		}
		irq = regmap_irq_get_virq(axp20x->regmap_irqc, irq);
		ret = devm_request_any_context_irq(&pdev->dev, irq,
						   axp20x_battery_power_irq, 0,
						   DRVNAME, axp20x_batt);
		if (ret < 0)
			dev_warn(&pdev->dev, "Error requesting %s IRQ: %d\n",
				 irq_names[i], ret);
	}

	irq = platform_get_irq_byname(pdev, "LOW_PWR_LVL1");
	irq = regmap_irq_get_virq(axp20x->regmap_irqc, irq);
	ret = devm_request_any_context_irq(&pdev->dev, irq,
					   axp20x_battery_low_voltage_alert1_irq,
					   0, DRVNAME, axp20x_batt);

	if (ret < 0)
		dev_warn(&pdev->dev, "Error requesting AXP20X_IRQ_LOW_PWR_LVL1 IRQ: %d\n",
			 ret);

	irq = platform_get_irq_byname(pdev, "LOW_PWR_LVL2");
	irq = regmap_irq_get_virq(axp20x->regmap_irqc, irq);
	ret = devm_request_any_context_irq(&pdev->dev, irq,
					   axp20x_battery_low_voltage_alert2_irq,
					   0, DRVNAME, axp20x_batt);

	if (ret < 0)
		dev_warn(&pdev->dev, "Error requesting AXP20X_IRQ_LOW_PWR_LVL2 IRQ: %d\n",
			 ret);

	irq = platform_get_irq_byname(pdev, "BATT_TEMP_LOW");
	irq = regmap_irq_get_virq(axp20x->regmap_irqc, irq);
	ret = devm_request_any_context_irq(&pdev->dev, irq,
					   axp20x_battery_temperature_low_irq,
					   0, DRVNAME, axp20x_batt);

	if (ret < 0)
		dev_warn(&pdev->dev, "Error requesting AXP20X_IRQ_BATT_TEMP_LOW IRQ: %d\n",
			 ret);

	irq = platform_get_irq_byname(pdev, "BATT_TEMP_HIGH");
	irq = regmap_irq_get_virq(axp20x->regmap_irqc, irq);
	ret = devm_request_any_context_irq(&pdev->dev, irq,
					   axp20x_battery_temperature_high_irq,
					   0, DRVNAME, axp20x_batt);

	if (ret < 0)
		dev_warn(&pdev->dev, "Error requesting AXP20X_IRQ_BATT_TEMP_HIGH IRQ: %d\n",
			 ret);

	return 0;
}

static struct platform_driver axp20x_batt_driver = {
	.probe    = axp20x_power_probe,
	.driver   = {
		.name  = DRVNAME,
		.of_match_table = axp20x_battery_ps_id,
	},
};

module_platform_driver(axp20x_batt_driver);

MODULE_DESCRIPTION("Battery power supply driver for AXP20X and AXP22X PMICs");
MODULE_AUTHOR("Quentin Schulz <quentin.schulz@free-electrons.com>");
MODULE_LICENSE("GPL");
