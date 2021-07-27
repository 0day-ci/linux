// SPDX-License-Identifier: GPL-2.0+
/*
 * sgp40.c - Support for Sensirion SGP40 Gas Sensors
 *
 * Copyright (C) 2021 Andreas Klinger <ak@it-klinger.de>
 *
 * I2C slave address: 0x59
 *
 * Datasheets:
 * https://www.sensirion.com/file/datasheet_sgp40
 *
 * There are two functionalities supported:
 * 1) read raw logarithmic resistance value from sensor
 *    --> useful to pass it to the algorithm of the sensor vendor for
 *    measuring deteriorations and improvements of air quality.
 * 2) calculate an estimated absolute voc index (0 - 500 index points) for
 *    measuring the air quality.
 *    For this purpose the mean value of the resistance can be set up using
 *    a device attribute
 *
 * Compensation of relative humidity and temperature can be used by device
 * attributes.
 */

#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/crc8.h>

#define SGP40_CRC8_POLYNOMIAL			0x31
#define SGP40_CRC8_INIT				0xff

static u8 sgp40_measure_raw_tg[] = {0x26, 0x0F};

DECLARE_CRC8_TABLE(sgp40_crc8_table);

struct sgp40_data {
	struct device		*dev;
	struct i2c_client	*client;
	int			rel_humidity;
	int			temperature;
	int			raw_mean;
	struct mutex		lock;
};

static const struct iio_chan_spec sgp40_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
	{
		.type = IIO_RESISTANCE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
};

/*
 * calculate e^x where n is the exponent multiplied with 100
 *
 * use taylor approximation which is accurate enough for the purpose of
 * coming out with just 500 index points.
 */
int sqp40_exp100(int n)
{
	int x, xn, y, z;
	int s = 1;

	if (n < 0) {
		s = -1;
		n *= -1;
	}

	x = n;

	y = 100 + x;
	xn = x * x;
	y += xn / 2 / 100;
	xn = x * x * x;
	y += xn / 6 / 10000;
	xn = x * x * x * x;
	y += xn / 24 / 1000000;

	if (s == -1)
		z = 10000 / y;
	else
		z = y;

	return z;
}

static int sgp40_calc_voc(struct sgp40_data *data, u16 raw, int *voc)
{
	int x;
	int ex = 0;

	/* we calculate in 100's */
	x = ((int)raw - data->raw_mean) * 65 / 100;

	/* voc = 500 / (1 + e^x) */
	if (x < -800)
		*voc = 500;
	else if (x > 800)
		*voc = 0;
	else {
		ex = sqp40_exp100(x);
		*voc = 50000 / (100 + ex);
	}

	dev_dbg(data->dev, "raw: %d raw_mean: %d x: %d ex: %d voc: %d\n",
						raw, data->raw_mean, x, ex, *voc);

	return 0;
}

static int sgp40_measure_raw(struct sgp40_data *data, u16 *raw)
{
	int ret;
	struct i2c_client *client = data->client;
	u16 buf_be16;
	u8 buf[3];
	u8 tg[8];
	u32 ticks;
	u8 crc;

	memcpy(tg, sgp40_measure_raw_tg, 2);

	ticks = (data->rel_humidity / 10) * 65535 / 10000;
	buf_be16 = cpu_to_be16((u16)ticks);
	memcpy(&tg[2], &buf_be16, 2);
	tg[4] = crc8(sgp40_crc8_table, &tg[2], 2, SGP40_CRC8_INIT);

	ticks = ((data->temperature + 45000) / 10) * 65535 / 17500;
	buf_be16 = cpu_to_be16((u16)ticks);
	memcpy(&tg[5], &buf_be16, 2);
	tg[7] = crc8(sgp40_crc8_table, &tg[5], 2, SGP40_CRC8_INIT);

	ret = i2c_master_send(client, (const char *)tg, sizeof(tg));
	if (ret != sizeof(tg)) {
		dev_warn(data->dev, "i2c_master_send ret: %d sizeof: %d\n", ret, sizeof(tg));
		return -EIO;
	}
	msleep(30);

	ret = i2c_master_recv(client, buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf)) {
		dev_warn(data->dev, "i2c_master_recv ret: %d sizeof: %d\n", ret, sizeof(buf));
		return -EIO;
	}

	crc = crc8(sgp40_crc8_table, buf, 2, SGP40_CRC8_INIT);
	if (crc != buf[2]) {
		dev_err(data->dev, "CRC error while measure-raw\n");
		return -EIO;
	}

	memcpy(&buf_be16, buf, sizeof(buf_be16));
	*raw = be16_to_cpu(buf_be16);

	return 0;
}

static int sgp40_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan, int *val,
			int *val2, long mask)
{
	struct sgp40_data *data = iio_priv(indio_dev);
	int ret;
	u16 raw;
	int voc;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		ret = sgp40_measure_raw(data, &raw);
		if (ret) {
			mutex_unlock(&data->lock);
			return ret;
		}
		*val = raw;
		ret = IIO_VAL_INT;
		mutex_unlock(&data->lock);
		break;
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&data->lock);
		ret = sgp40_measure_raw(data, &raw);
		if (ret) {
			mutex_unlock(&data->lock);
			return ret;
		}
		ret = sgp40_calc_voc(data, raw, &voc);
		if (ret) {
			mutex_unlock(&data->lock);
			return ret;
		}
		*val = voc;
		ret = IIO_VAL_INT;
		mutex_unlock(&data->lock);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static ssize_t rel_humidity_comp_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sgp40_data *data = iio_priv(indio_dev);
	int ret;
	u32 val;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 100000)
		return -EINVAL;

	mutex_lock(&data->lock);
	data->rel_humidity = val;
	mutex_unlock(&data->lock);

	return len;
}

static ssize_t rel_humidity_comp_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sgp40_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", data->rel_humidity);
	mutex_unlock(&data->lock);

	return ret;
}

static ssize_t temperature_comp_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sgp40_data *data = iio_priv(indio_dev);
	int val;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if ((val < -45000) || (val > 130000))
		return -EINVAL;

	mutex_lock(&data->lock);
	data->temperature = val;
	mutex_unlock(&data->lock);

	return len;
}

static ssize_t temperature_comp_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sgp40_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", data->temperature);
	mutex_unlock(&data->lock);

	return ret;
}

static ssize_t raw_mean_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sgp40_data *data = iio_priv(indio_dev);
	u32 val;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if ((val < 20000) || (val > 52768))
		return -EINVAL;

	mutex_lock(&data->lock);
	data->raw_mean = val;
	mutex_unlock(&data->lock);

	return len;
}

static ssize_t raw_mean_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sgp40_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", data->raw_mean);
	mutex_unlock(&data->lock);

	return ret;
}

static IIO_DEVICE_ATTR_RW(rel_humidity_comp, 0);
static IIO_DEVICE_ATTR_RW(temperature_comp, 0);
static IIO_DEVICE_ATTR_RW(raw_mean, 0);

static struct attribute *sgp40_attrs[] = {
	&iio_dev_attr_rel_humidity_comp.dev_attr.attr,
	&iio_dev_attr_temperature_comp.dev_attr.attr,
	&iio_dev_attr_raw_mean.dev_attr.attr,
	NULL
};

static const struct attribute_group sgp40_attr_group = {
	.attrs = sgp40_attrs,
};

static const struct iio_info sgp40_info = {
	.attrs		= &sgp40_attr_group,
	.read_raw	= sgp40_read_raw,
};

static int sgp40_probe(struct i2c_client *client,
		     const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct sgp40_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->dev = dev;

	crc8_populate_msb(sgp40_crc8_table, SGP40_CRC8_POLYNOMIAL);

	mutex_init(&data->lock);

	/* set default values */
	data->rel_humidity = 50000;	/* 50 % */
	data->temperature = 25000;	/* 25 °C */
	data->raw_mean = 30000;		/* resistance raw value for voc index of 250 */

	indio_dev->info = &sgp40_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = sgp40_channels;
	indio_dev->num_channels = ARRAY_SIZE(sgp40_channels);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret) {
		dev_err(dev, "failed to register iio device\n");
		return ret;
	}

	return 0;
}

static const struct i2c_device_id sgp40_id[] = {
	{ "sgp40" },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sgp40_id);

static const struct of_device_id sgp40_dt_ids[] = {
	{ .compatible = "sensirion,sgp40" },
	{ }
};

MODULE_DEVICE_TABLE(of, sgp40_dt_ids);

static struct i2c_driver sgp40_driver = {
	.driver = {
		.name = "sgp40",
		.of_match_table = sgp40_dt_ids,
	},
	.probe = sgp40_probe,
	.id_table = sgp40_id,
};
module_i2c_driver(sgp40_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("Sensirion SGP40 gas sensors");
MODULE_LICENSE("GPL v2");
