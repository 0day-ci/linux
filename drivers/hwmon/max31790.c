// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max31790.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring.
 *
 * (C) 2015 by Il Han <corone.il.han@gmail.com>
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* MAX31790 registers */
#define MAX31790_REG_GLOBAL_CONFIG	0x00
#define MAX31790_REG_FAN_CONFIG(ch)	(0x02 + (ch))
#define MAX31790_REG_FAN_DYNAMICS(ch)	(0x08 + (ch))
#define MAX31790_REG_FAN_FAULT_STATUS2	0x10
#define MAX31790_REG_FAN_FAULT_STATUS1	0x11
#define MAX31790_REG_TACH_COUNT(ch)	(0x18 + (ch) * 2)
#define MAX31790_REG_PWM_DUTY_CYCLE(ch)	(0x30 + (ch) * 2)
#define MAX31790_REG_PWMOUT(ch)		(0x40 + (ch) * 2)
#define MAX31790_REG_TARGET_COUNT(ch)	(0x50 + (ch) * 2)

/* Fan Config register bits */
#define MAX31790_FAN_CFG_RPM_MODE	0x80
#define MAX31790_FAN_CFG_TACH_INPUT_EN	0x08
#define MAX31790_FAN_CFG_TACH_INPUT	0x01

/* Fan Dynamics register bits */
#define MAX31790_FAN_DYN_SR_SHIFT	5
#define MAX31790_FAN_DYN_SR_MASK	0xE0
#define SR_FROM_REG(reg)		(((reg) & MAX31790_FAN_DYN_SR_MASK) \
					 >> MAX31790_FAN_DYN_SR_SHIFT)

#define FAN_RPM_MIN			120
#define FAN_RPM_MAX			7864320
#define MAX_PWM				0XFF80

#define RPM_FROM_REG(reg, sr)		(((reg) >> 4) ? \
					 ((60 * (sr) * 8192) / ((reg) >> 4)) : \
					 FAN_RPM_MAX)
#define RPM_TO_REG(rpm, sr)		((60 * (sr) * 8192) / ((rpm) * 2))

#define NR_CHANNEL			6

#define MAX31790_REG_USER_BYTE_67	0x67

#define BULK_TO_U16(msb, lsb)		(((msb) << 8) + (lsb))
#define U16_MSB(num)			(((num) & 0xFF00) >> 8)
#define U16_LSB(num)			((num) & 0x00FF)

static const struct regmap_range max31790_ro_range = {
	.range_min = MAX31790_REG_TACH_COUNT(0),
	.range_max = MAX31790_REG_PWMOUT(0) - 1,
};

static const struct regmap_access_table max31790_wr_table = {
	.no_ranges = &max31790_ro_range,
	.n_no_ranges = 1,
};

static const struct regmap_range max31790_volatile_ranges[] = {
	regmap_reg_range(MAX31790_REG_TACH_COUNT(0), MAX31790_REG_TACH_COUNT(12)),
	regmap_reg_range(MAX31790_REG_FAN_FAULT_STATUS2, MAX31790_REG_FAN_FAULT_STATUS1),
};

static const struct regmap_access_table max31790_volatile_table = {
	.no_ranges = max31790_volatile_ranges,
	.n_no_ranges = 2,
	.n_yes_ranges = 0
};

static const struct regmap_config max31790_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,
	.max_register = MAX31790_REG_USER_BYTE_67,
	.wr_table = &max31790_wr_table,
	.volatile_table = &max31790_volatile_table
};

/*
 * Client data (each client gets its own)
 */
struct max31790_data {
	struct regmap *regmap;

	struct mutex update_lock; /* for full_speed */
	bool full_speed[NR_CHANNEL];
};

static const u8 tach_period[8] = { 1, 2, 4, 8, 16, 32, 32, 32 };

static u8 get_tach_period(u8 fan_dynamics)
{
	return tach_period[SR_FROM_REG(fan_dynamics)];
}

static u8 bits_for_tach_period(int rpm)
{
	u8 bits;

	if (rpm < 500)
		bits = 0x0;
	else if (rpm < 1000)
		bits = 0x1;
	else if (rpm < 2000)
		bits = 0x2;
	else if (rpm < 4000)
		bits = 0x3;
	else if (rpm < 8000)
		bits = 0x4;
	else
		bits = 0x5;

	return bits;
}

static int read_reg_byte(struct regmap *regmap, u8 reg)
{
	int rv;
	int val;

	rv = regmap_read(regmap, reg, &val);

	if (rv < 0)
		return rv;

	return val;
}

static int read_reg_word(struct regmap *regmap, u8 reg)
{
	int rv;
	u8 val_bulk[2];

	rv = regmap_bulk_read(regmap, reg, val_bulk, 2);
	if (rv < 0)
		return rv;

	return BULK_TO_U16(val_bulk[0], val_bulk[1]);
}

static int write_reg_word(struct regmap *regmap, u8 reg, u16 val)
{
	u8 bulk_val[2];

	bulk_val[0] = U16_MSB(val);
	bulk_val[1] = U16_LSB(val);

	return regmap_bulk_write(regmap, reg, bulk_val, 2);
}

static int bits_for_speed_range(long speed_range)
{
	switch (speed_range) {
	case 1:
		return 0x0;
	case 2:
		return 0x1;
	case 4:
		return 0x2;
	case 8:
		return 0x3;
	case 16:
		return 0x4;
	case 32:
		return 0x5;
	default:
		return -1;
	}
}

static int max31790_read_fan(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct max31790_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int rpm, dynamics, tach, fault, cfg;

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (attr) {
	case hwmon_fan_input:
		cfg = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel));
		if (cfg < 0)
			return cfg;

		if (!(cfg & MAX31790_FAN_CFG_TACH_INPUT_EN))
			return -ENODATA;

		dynamics = read_reg_byte(regmap, MAX31790_REG_FAN_DYNAMICS(channel));
		if (dynamics < 0)
			return dynamics;

		tach = read_reg_word(regmap, MAX31790_REG_TACH_COUNT(channel));
		if (tach < 0)
			return tach;

		rpm = RPM_FROM_REG(tach, get_tach_period(dynamics));
		*val = rpm;
		return 0;
	case hwmon_fan_target:
		dynamics = read_reg_byte(regmap, MAX31790_REG_FAN_DYNAMICS(channel));
		if (dynamics < 0)
			return dynamics;

		tach = read_reg_word(regmap, MAX31790_REG_TARGET_COUNT(channel));
		if (tach < 0)
			return tach;

		rpm = RPM_FROM_REG(tach, get_tach_period(dynamics));
		*val = rpm;
		return 0;
	case hwmon_fan_fault:
		cfg = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel));
		if (cfg < 0)
			return cfg;

		if (!(cfg & MAX31790_FAN_CFG_TACH_INPUT_EN)) {
			*val = 0;
			return 0;
		}

		if (channel > 6)
			fault = read_reg_byte(regmap, MAX31790_REG_FAN_FAULT_STATUS2);
		else
			fault = read_reg_byte(regmap, MAX31790_REG_FAN_FAULT_STATUS1);

		if (fault < 0)
			return fault;

		if (channel > 6)
			*val = !!(fault & (1 << (channel - 6)));
		else
			*val = !!(fault & (1 << channel));
		return 0;
	case hwmon_fan_enable:
		cfg = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel));
		if (cfg < 0)
			return cfg;

		*val = !!(cfg & MAX31790_FAN_CFG_TACH_INPUT_EN);
		return 0;
	case hwmon_fan_div:
		cfg = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel));
		if (cfg < 0)
			return cfg;

		*val = get_tach_period(cfg);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int max31790_write_fan(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct max31790_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int target_count;
	int err = 0;
	u8 bits;
	int sr;
	int fan_dynamics, cfg;

	switch (attr) {
	case hwmon_fan_target:
		val = clamp_val(val, FAN_RPM_MIN, FAN_RPM_MAX);
		bits = bits_for_tach_period(val);
		fan_dynamics = read_reg_byte(regmap, MAX31790_REG_FAN_DYNAMICS(channel));

		if (fan_dynamics < 0)
			return fan_dynamics;

		fan_dynamics =
			((fan_dynamics &
			  ~MAX31790_FAN_DYN_SR_MASK) |
			 (bits << MAX31790_FAN_DYN_SR_SHIFT));
		err = regmap_write(regmap,
				   MAX31790_REG_FAN_DYNAMICS(channel),
				   fan_dynamics);
		if (err < 0)
			break;

		sr = get_tach_period(fan_dynamics);
		target_count = RPM_TO_REG(val, sr);
		target_count = clamp_val(target_count, 0x1, 0x7FF);

		target_count = target_count << 5;

		err = write_reg_word(regmap,
				     MAX31790_REG_TARGET_COUNT(channel),
				     target_count);
		break;
	case hwmon_fan_enable:
		cfg = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel));
		if (val == 0)
			cfg &= ~MAX31790_FAN_CFG_TACH_INPUT_EN;
		else
			cfg |= MAX31790_FAN_CFG_TACH_INPUT_EN;
		err = regmap_write(regmap, MAX31790_REG_FAN_CONFIG(channel), cfg);
		break;
	case hwmon_fan_div:
		cfg = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel));
		if (cfg < 0)
			return cfg;

		if (cfg & MAX31790_FAN_CFG_RPM_MODE) {
			err = -EINVAL;
			break;
		}
		sr = bits_for_speed_range(val);
		if (sr < 0) {
			err = -EINVAL;
			break;
		}

		fan_dynamics = read_reg_byte(regmap, MAX31790_REG_FAN_DYNAMICS(channel));

		if (fan_dynamics < 0)
			return fan_dynamics;

		fan_dynamics = ((fan_dynamics &
				 ~MAX31790_FAN_DYN_SR_MASK) |
				(sr << MAX31790_FAN_DYN_SR_SHIFT));
		err = regmap_write(regmap,
				   MAX31790_REG_FAN_DYNAMICS(channel),
				   fan_dynamics);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static umode_t max31790_fan_is_visible(const void *_data, u32 attr, int channel)
{
	const struct max31790_data *data = _data;
	struct regmap *regmap = data->regmap;
	u8 fan_config = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel % NR_CHANNEL));

	if (fan_config < 0)
		return 0;

	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_fault:
		if (channel < NR_CHANNEL ||
		    (fan_config & MAX31790_FAN_CFG_TACH_INPUT))
			return 0444;
		return 0;
	case hwmon_fan_target:
		if (channel < NR_CHANNEL &&
		    !(fan_config & MAX31790_FAN_CFG_TACH_INPUT))
			return 0644;
		return 0;
	case hwmon_fan_enable:
	case hwmon_fan_div:
		if (channel < NR_CHANNEL ||
		    (fan_config & MAX31790_FAN_CFG_TACH_INPUT))
			return 0644;
		return 0;
	default:
		return 0;
	}
}

static int max31790_read_pwm(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct max31790_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int read;

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (attr) {
	case hwmon_pwm_input:
		read = read_reg_word(regmap, MAX31790_REG_PWMOUT(channel));
		if (read < 0)
			return read;

		*val = read >> 8;
		return 0;
	case hwmon_pwm_enable:
		read = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel));
		if (read < 0)
			return read;

		mutex_lock(&data->update_lock);
		if (data->full_speed[channel])
			*val = 0;
		else if (read & MAX31790_FAN_CFG_RPM_MODE)
			*val = 2;
		else
			*val = 1;
		mutex_unlock(&data->update_lock);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int max31790_write_pwm(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct max31790_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u8 fan_config;
	int err = 0;

	switch (attr) {
	case hwmon_pwm_input:
		mutex_lock(&data->update_lock);
		if (data->full_speed[channel] || val < 0 || val > 255) {
			err = -EINVAL;
			break;
		}
		mutex_unlock(&data->update_lock);

		err = write_reg_word(regmap, MAX31790_REG_PWMOUT(channel), val << 8);
		break;
	case hwmon_pwm_enable:
		fan_config = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel % NR_CHANNEL));

		if (fan_config < 0)
			return fan_config;

		if (val == 0 || val == 1) {
			fan_config &= ~MAX31790_FAN_CFG_RPM_MODE;
		} else if (val == 2) {
			fan_config |= MAX31790_FAN_CFG_RPM_MODE;
		} else {
			err = -EINVAL;
			break;
		}

		/*
		 * The chip sets PWM to zero when using its "monitor only" mode
		 * and 0 means full speed.
		 */
		mutex_lock(&data->update_lock);
		if (val == 0) {
			data->full_speed[channel] = true;
			err = write_reg_word(regmap, MAX31790_REG_PWMOUT(channel), MAX_PWM);
		} else {
			data->full_speed[channel] = false;
		}
		mutex_unlock(&data->update_lock);

		/*
		 * RPM mode implies enabled TACH input, so enable it in RPM
		 * mode.
		 */
		if (val == 2)
			fan_config |= MAX31790_FAN_CFG_TACH_INPUT_EN;

		err = regmap_write(regmap,
				   MAX31790_REG_FAN_CONFIG(channel),
				   fan_config);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static umode_t max31790_pwm_is_visible(const void *_data, u32 attr, int channel)
{
	const struct max31790_data *data = _data;
	struct regmap *regmap = data->regmap;
	u8 fan_config = read_reg_byte(regmap, MAX31790_REG_FAN_CONFIG(channel % NR_CHANNEL));

	if (fan_config < 0)
		return 0;

	switch (attr) {
	case hwmon_pwm_input:
	case hwmon_pwm_enable:
		if (!(fan_config & MAX31790_FAN_CFG_TACH_INPUT))
			return 0644;
		return 0;
	default:
		return 0;
	}
}

static int max31790_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return max31790_read_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return max31790_read_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int max31790_write(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_fan:
		return max31790_write_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return max31790_write_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max31790_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return max31790_fan_is_visible(data, attr, channel);
	case hwmon_pwm:
		return max31790_pwm_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static const struct hwmon_channel_info *max31790_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_DIV | HWMON_F_ENABLE | HWMON_F_INPUT | HWMON_F_FAULT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL
};

static const struct hwmon_ops max31790_hwmon_ops = {
	.is_visible = max31790_is_visible,
	.read = max31790_read,
	.write = max31790_write,
};

static const struct hwmon_chip_info max31790_chip_info = {
	.ops = &max31790_hwmon_ops,
	.info = max31790_info,
};

static int max31790_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct max31790_data *data;
	struct device *hwmon_dev;
	int i;

	if (!i2c_check_functionality(adapter,
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct max31790_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->update_lock);
	for (i = 0; i < NR_CHANNEL; i++)
		data->full_speed[i] = false;

	data->regmap = devm_regmap_init_i2c(client, &max31790_regmap_config);

	if (IS_ERR(data->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &max31790_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max31790_id[] = {
	{ "max31790", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max31790_id);

static struct i2c_driver max31790_driver = {
	.class		= I2C_CLASS_HWMON,
	.probe_new	= max31790_probe,
	.driver = {
		.name	= "max31790",
	},
	.id_table	= max31790_id,
};

module_i2c_driver(max31790_driver);

MODULE_AUTHOR("Il Han <corone.il.han@gmail.com>");
MODULE_DESCRIPTION("MAX31790 sensor driver");
MODULE_LICENSE("GPL");
