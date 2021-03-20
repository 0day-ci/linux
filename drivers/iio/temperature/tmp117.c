// SPDX-License-Identifier: GPL-2.0-only
/*
 * tmp117.c - Digital temperature sensor with integrated NV memory
 *
 * Copyright (c) 2021 Puranjay Mohan <puranjay12@gmail.com>
 *
 * Driver for the Texas Instruments TMP117 Temperature Sensor
 *
 * (7-bit I2C slave address (0x48 - 0x4B), changeable via ADD pins)
 *
 * Note: This driver assumes that the sensor has been calibrated beforehand.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/irq.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

#define TMP117_REG_TEMP			0x0
#define TMP117_REG_CFGR			0x1
#define TMP117_REG_HIGH_LIM		0x2
#define TMP117_REG_LOW_LIM		0x3
#define TMP117_REG_EEPROM_UL		0x4
#define TMP117_REG_EEPROM1		0x5
#define TMP117_REG_EEPROM2		0x6
#define TMP117_REG_TEMP_OFFSET		0x7
#define TMP117_REG_EEPROM3		0x8
#define TMP117_REG_DEVICE_ID		0xF

#define TMP117_SCALE			7812500       /* in uCelsius*/
#define TMP117_RESOLUTION		78125
#define TMP117_DEVICE_ID		0x0117

struct tmp117_data {
	struct i2c_client *client;
	struct mutex lock;
};

static int tmp117_read_reg(struct tmp117_data *data, u8 reg)
{
	return i2c_smbus_read_word_swapped(data->client, reg);
}

static int tmp117_write_reg(struct tmp117_data *data, u8 reg, int val)
{
	return i2c_smbus_write_word_swapped(data->client, reg, val);
}

static int tmp117_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *channel, int *val,
		int *val2, long mask)
{
	struct tmp117_data *data = iio_priv(indio_dev);
	u16 tmp, off;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		tmp = tmp117_read_reg(data, TMP117_REG_TEMP);
		*val = tmp;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_CALIBBIAS:
		off = tmp117_read_reg(data, TMP117_REG_TEMP_OFFSET);
		*val = ((int16_t)off * (int32_t)TMP117_RESOLUTION) / 10000000;
		*val2 = ((int16_t)off * (int32_t)TMP117_RESOLUTION) % 10000000;
		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = TMP117_SCALE;
		return IIO_VAL_INT_PLUS_NANO;

	default:
		return -EINVAL;
	}
}

static int tmp117_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *channel, int val,
		int val2, long mask)
{
	struct tmp117_data *data = iio_priv(indio_dev);
	u16 off;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		off = ((val * 10000000) + (val2 * 10))
						/ (int32_t)TMP117_RESOLUTION;
		return tmp117_write_reg(data, TMP117_REG_TEMP_OFFSET, off);

	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec tmp117_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_CALIBBIAS) | BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info tmp117_info = {
	.read_raw = tmp117_read_raw,
	.write_raw = tmp117_write_raw,
};

static bool tmp117_identify(struct i2c_client *client)
{
	int dev_id;

	dev_id = i2c_smbus_read_word_swapped(client, TMP117_REG_DEVICE_ID);
	if (dev_id < 0)
		return false;

	return (dev_id == TMP117_DEVICE_ID);
}

static int tmp117_probe(struct i2c_client *client,
			const struct i2c_device_id *tmp117_id)
{
	struct tmp117_data *data;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	if (!tmp117_identify(client)) {
		dev_err(&client->dev, "TMP117 not found\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	indio_dev->name = "tmp117";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &tmp117_info;

	indio_dev->channels = tmp117_channels;
	indio_dev->num_channels = ARRAY_SIZE(tmp117_channels);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id tmp117_of_match[] = {
	{ .compatible = "ti,tmp117", },
	{ },
};
MODULE_DEVICE_TABLE(of, tmp117_of_match);

static const struct i2c_device_id tmp117_id[] = {
	{ "tmp117", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmp117_id);

static struct i2c_driver tmp117_driver = {
	.driver = {
		.name	= "tmp117",
		.of_match_table = of_match_ptr(tmp117_of_match),
	},
	.probe		= tmp117_probe,
	.id_table	= tmp117_id,
};
module_i2c_driver(tmp117_driver);

MODULE_AUTHOR("Puranjay Mohan <puranjay12@gmail.com>");
MODULE_DESCRIPTION("TI TMP117 Temperature sensor driver");
MODULE_LICENSE("GPL");
