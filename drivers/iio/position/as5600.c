// SPDX-License-Identifier: GPL-2.0+
/*
 * ams AS5600 -- 12-Bit Programmable Contactless Potentiometer
 *
 * Copyright 2021, Frank Zago
 *
 * datasheet v1.06 (2018-Jun-20):
 *    https://ams.com/documents/20143/36005/AS5600_DS000365_5-00.pdf
 *
 * The rotating magnet is installed from 0.5mm to 3mm parallel to and
 * above the chip.
 *
 * The raw angle value returned by the chip is [0..4095]. The channel
 * 0 (in_angl0_raw) returns the unscaled and unmodified angle, always
 * covering the 360 degrees. The channel 1 returns the chip adjusted
 * angle, covering from 18 to 360 degrees, as modified by its
 * ZPOS/MPOS/MANG values,
 *
 * ZPOS and MPOS can be programmed through their debugfs entries. The
 * MANG register doesn't appear to be programmable without flashing
 * the chip.
 *
 * If the DIR pin is grounded, angles will increase when the magnet is
 * turned clockwise. If DIR is connected to Vcc, it will be the opposite.
 *
 * Permanent programming of the MPOS/ZPOS/MANG/CONF registers is not
 * implemented.
 *
 * The i2c address of the device is 0x36.
 */

#include <linux/iio/iio.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/bitfield.h>

/* Registers and their fields, as defined in the datasheet */
#define REG_ZMCO 0x00
#define REG_ZPOS 0x01
#define   REG_ZPOS_ZPOS GENMASK(11, 0)
#define REG_MPOS 0x03
#define   REG_MPOS_MPOS GENMASK(11, 0)
#define REG_MANG 0x05
#define   REG_MANG_MANG GENMASK(11, 0)
#define REG_CONF 0x07
#define REG_STATUS 0x0b
#define   REG_STATUS_MD BIT(5)
#define REG_RAW_ANGLE 0x0c
#define   REG_RAW_ANGLE_ANGLE GENMASK(11, 0)
#define REG_ANGLE 0x0e
#define   REG_ANGLE_ANGLE GENMASK(11, 0)
#define REG_AGC 0x1a
#define REG_MAGNITUDE 0x1b
#define REG_BURN 0xff

enum {
	X_REG_ZMCO_ZMCO,
	X_REG_ZPOS_ZPOS,
	X_REG_MPOS_MPOS,
	X_REG_MANG_MANG,
	X_REG_CONF_PM,
	X_REG_CONF_HYST,
	X_REG_CONF_OUTS,
	X_REG_CONF_PWMF,
	X_REG_CONF_SF,
	X_REG_CONF_FTH,
	X_REG_CONF_WD,
	X_REG_STATUS_MH,
	X_REG_STATUS_ML,
	X_REG_STATUS_MD,
	X_REG_AGC_AGC,
	X_REG_MAGNITUDE_MAGNITUDE,

	X_REG_NUM_ENTRIES,	/* last */
};

static const struct {
	u8 reg;
	u16 mask;
	u16 max_value;		/* maximum writable value */
} reg_access[] = {
	[X_REG_ZMCO_ZMCO] = { REG_ZMCO, GENMASK(1, 0) },
	[X_REG_ZPOS_ZPOS] = { REG_ZPOS, REG_ZPOS_ZPOS, 4095 },
	[X_REG_MPOS_MPOS] = { REG_MPOS, REG_MPOS_MPOS, 4095 },
	[X_REG_MANG_MANG] = { REG_MANG, REG_MANG_MANG, 4095 },
	[X_REG_CONF_PM] = { REG_CONF, GENMASK(1, 0), 3 },
	[X_REG_CONF_HYST] = { REG_CONF, GENMASK(3, 2), 3 },
	[X_REG_CONF_OUTS] = { REG_CONF, GENMASK(5, 4), 3 },
	[X_REG_CONF_PWMF] = { REG_CONF, GENMASK(7, 6), 3 },
	[X_REG_CONF_SF] = { REG_CONF, GENMASK(9, 8), 3 },
	[X_REG_CONF_FTH] = { REG_CONF, GENMASK(12, 10), 7},
	[X_REG_CONF_WD] = { REG_CONF, BIT(13), 1 },
	[X_REG_STATUS_MH] = { REG_STATUS, BIT(3) },
	[X_REG_STATUS_ML] = { REG_STATUS, BIT(4) },
	[X_REG_STATUS_MD] = { REG_STATUS, REG_STATUS_MD },
	[X_REG_AGC_AGC] = { REG_AGC, GENMASK(7, 0) },
	[X_REG_MAGNITUDE_MAGNITUDE] = { REG_MAGNITUDE, GENMASK(11, 0) },
};

/* runtime versions of the FIELD_GET/FIELD_PREP macros */
#define field_get(_mask, _reg) (((_reg) & (_mask)) >> (ffs(_mask) - 1))
#define field_prep(_mask, _val) (((_val) << (ffs(_mask) - 1)) & (_mask))

struct as5600_priv {
	struct iio_dev *iio_dev;
	struct i2c_client *client;
	struct mutex lock;
	u16 zpos;
	u16 mpos;
};

static int as5600_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct as5600_priv *priv = iio_priv(indio_dev);
	u16 bitmask;
	s32 angle;
	u16 reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->channel == 0) {
			reg = REG_RAW_ANGLE;
			bitmask = REG_RAW_ANGLE_ANGLE;
		} else {
			reg = REG_ANGLE;
			bitmask = REG_ANGLE_ANGLE;
		}
		angle = i2c_smbus_read_word_swapped(priv->client, reg);

		if (angle < 0)
			return angle;
		*val = field_get(bitmask, angle);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		/* Always 4096 steps, but angle range varies between
		 * 18 and 360 degrees.
		 */
		if (chan->channel == 0) {
			/* Whole angle range - 2*pi / 4096 */
			*val = 3141592;
			*val2 = 2048000000;
		} else {
			s32 range;

			/* Partial angle - (range/4096) * (2*pi / 4096) */
			mutex_lock(&priv->lock);
			range = priv->mpos - priv->zpos;
			mutex_unlock(&priv->lock);
			if (range <= 0)
				range += 4096;

			*val = range * 314159;
			*val /= 4096;
			*val2 = 204800000;
		}

		return IIO_VAL_FRACTIONAL;

	default:
		return -EINVAL;
	}
}

static ssize_t as5600_reg_access_read(struct as5600_priv *priv,
				      unsigned int reg_access_idx,
				      unsigned int *readval)
{
	unsigned int reg = reg_access[reg_access_idx].reg;
	unsigned int mask = reg_access[reg_access_idx].mask;
	int ret;

	switch (reg) {
	case REG_ZMCO:
	case REG_STATUS:
	case REG_AGC:
		ret = i2c_smbus_read_byte_data(priv->client, reg);
		if (ret < 0)
			return ret;

		*readval = field_get(mask, ret);
		return 0;

	case REG_ZPOS:
	case REG_MPOS:
	case REG_CONF:
	case REG_MAGNITUDE:
		ret = i2c_smbus_read_word_swapped(priv->client, reg);
		if (ret < 0)
			return ret;

		*readval = field_get(mask, ret);
		return 0;
	}

	return -EINVAL;
}

static ssize_t as5600_reg_access_write(struct as5600_priv *priv,
				       unsigned int reg_access_idx,
				       unsigned int writeval)
{
	unsigned int reg = reg_access[reg_access_idx].reg;
	unsigned int mask = reg_access[reg_access_idx].mask;
	u16 out;
	int ret;

	switch (reg) {
	case REG_ZPOS:
	case REG_MPOS:
	case REG_CONF:
		if (writeval > reg_access[reg_access_idx].max_value)
			return -EINVAL;

		/* Read then write, as per spec */
		ret = i2c_smbus_read_word_swapped(priv->client, reg);
		if (ret < 0)
			return ret;

		out = ret & ~mask;
		out |= field_prep(mask, writeval);

		ret = i2c_smbus_write_word_swapped(priv->client, reg, out);
		if (ret < 0)
			return ret;

		if (reg == REG_ZPOS)
			priv->zpos = writeval;
		else if (reg == REG_MPOS)
			priv->mpos = writeval;

		break;

	default:
		/* Not a writable register */
		return -EINVAL;

	}

	return 0;
}

static int as5600_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	struct as5600_priv *priv = iio_priv(indio_dev);
	int ret;

	if (reg >= X_REG_NUM_ENTRIES)
		return -EINVAL;

	if (readval) {
		ret = as5600_reg_access_read(priv, reg, readval);
	} else {

		mutex_lock(&priv->lock);
		ret = as5600_reg_access_write(priv, reg, writeval);
		mutex_unlock(&priv->lock);
	}

	return ret;
}

static const struct iio_chan_spec as5600_channels[] = {
	{
		.type = IIO_ANGL,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = 0,
	},
	{
		.type = IIO_ANGL,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.indexed = 1,
		.channel = 1,
	},
};

static const struct iio_info as5600_info = {
	.read_raw = &as5600_read_raw,
	.debugfs_reg_access = &as5600_reg_access,
};

static int as5600_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct as5600_priv *priv;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	priv->client = client;
	mutex_init(&priv->lock);

	indio_dev->info = &as5600_info;
	indio_dev->name = "as5600";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = as5600_channels;
	indio_dev->num_channels = ARRAY_SIZE(as5600_channels);

	ret = i2c_smbus_read_byte_data(client, REG_STATUS);
	if (ret < 0)
		return ret;

	/* No magnet present could be a problem. */
	if ((ret & REG_STATUS_MD) == 0)
		dev_warn(&client->dev, "Magnet not detected\n");

	ret = i2c_smbus_read_byte_data(client, REG_ZPOS);
	if (ret < 0)
		return ret;
	priv->zpos = FIELD_GET(REG_ZPOS_ZPOS, ret);

	ret = i2c_smbus_read_byte_data(client, REG_MPOS);
	if (ret < 0)
		return ret;
	priv->mpos = FIELD_GET(REG_MPOS_MPOS, ret);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id as5600_i2c_id[] = {
	{"as5600", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, as5600_i2c_id);

static struct i2c_driver as5600_driver = {
	.driver = {
		.name = "as5600_i2c",
	},
	.probe = as5600_probe,
	.id_table   = as5600_i2c_id,
};

module_i2c_driver(as5600_driver);

MODULE_AUTHOR("Frank Zago <frank@zago.net>");
MODULE_DESCRIPTION("ams AS5600 Contactless Potentiometer");
MODULE_LICENSE("GPL");
