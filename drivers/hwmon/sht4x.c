// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) Linumiz 2021
 *
 * sht4x.c - Linux hwmon driver for SHT4x Temperature and Humidity sensor
 *
 * Author: Navin Sankar Velliangiri <navin@linumiz.com>
 */

#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/ktime.h>

/*
 * Poll intervals (in milliseconds)
 */
#define SHT4X_DEFAULT_MIN_POLL_INTERVAL	2000
#define SHT4X_MIN_POLL_INTERVAL		2000

/*
 * I2C command delays (in microseconds)
 */
#define SHT4X_MEAS_DELAY	1000
#define SHT4X_DELAY_EXTRA	10000

/*
 * Command Bytes
 */
#define SHT4X_CMD_MEASURE_HPM	0b11111101
#define SHT4X_CMD_RESET		0b10010100

#define SHT4X_CMD_LEN		1
#define SHT4X_CRC8_LEN		1
#define SHT4X_WORD_LEN		2
#define SHT4X_RESPONSE_LENGTH	6
#define SHT4X_CRC8_POLYNOMIAL	0x31
#define SHT4X_CRC8_INIT		0xff
#define SHT4X_MIN_TEMPERATURE	-45000
#define SHT4X_MAX_TEMPERATURE	125000
#define SHT4X_MIN_HUMIDITY	0
#define SHT4X_MAX_HUMIDITY	100000

DECLARE_CRC8_TABLE(sht4x_crc8_table);

/**
 * struct sht4x_data - All the data required to operate an SHT4X chip
 * @client: the i2c client associated with the SHT4X
 * @lock: a mutex that is used to prevent parallel access to the i2c client
 * @min_poll_interval: the minimum poll interval
 * @previous_poll_time: the previous time that the SHT4X was polled
 * @temperature: the latest temperature value received from the SHT4X
 * @humidity: the latest humidity value received from the SHT4X
 */
struct sht4x_data {
	struct i2c_client *client;
	/*
	 * Prevent simultaneous access to the i2c client
	 */
	struct mutex lock;
	ktime_t min_poll_interval;
	ktime_t previous_poll_time;
	s32 temperature;
	s32 humidity;
};

/**
 * sht4x_polltime_expired() - check if the minimum poll interval has expired
 * @data: the data containing the time to compare
 * Return: 1 if the minimum poll interval has expired, 0 if not
 */
static bool sht4x_polltime_expired(struct sht4x_data *data)
{
	ktime_t current_time = ktime_get_boottime();
	ktime_t difference = ktime_sub(current_time, data->previous_poll_time);

	return ktime_after(difference, data->min_poll_interval);
}

/**
 * sht4x_read_values() - read and parse the raw data from the SHT4X
 * @sht4x_data: the struct sht4x_data to use for the lock
 * Return: 0 if succesfull, 1 if not
 */
static int sht4x_read_values(struct sht4x_data *data)
{
	int ret;
	u16 t_ticks, rh_ticks;
	struct i2c_client *client = data->client;
	u8 crc, raw_data[SHT4X_RESPONSE_LENGTH],
	cmd[] = {SHT4X_CMD_MEASURE_HPM};

	mutex_lock(&data->lock);
	if (sht4x_polltime_expired(data)) {
		ret = i2c_master_send(client, cmd, SHT4X_CMD_LEN);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			return ret;
		}

		usleep_range(SHT4X_MEAS_DELAY,
			     SHT4X_MEAS_DELAY + SHT4X_DELAY_EXTRA);

		ret = i2c_master_recv(client, raw_data, SHT4X_RESPONSE_LENGTH);
		if (ret != SHT4X_RESPONSE_LENGTH) {
			mutex_unlock(&data->lock);
			if (ret >= 0)
				return -ENODATA;
			else
				return ret;
		}

		t_ticks = raw_data[0] << 8 | raw_data[1];
		rh_ticks = raw_data[3] << 8 | raw_data[4];

		crc = crc8(sht4x_crc8_table, &raw_data[0], SHT4X_WORD_LEN, CRC8_INIT_VALUE);
		if (crc != raw_data[2]) {
			mutex_unlock(&data->lock);
			dev_err(&client->dev, "data integrity check failed\n");
			return -EIO;
		}

		crc = crc8(sht4x_crc8_table, &raw_data[3], SHT4X_WORD_LEN, CRC8_INIT_VALUE);
		if (crc != raw_data[5]) {
			mutex_unlock(&data->lock);
			dev_err(&client->dev, "data integrity check failed\n");
			return -EIO;
		}

		data->temperature = ((21875 * (int32_t)t_ticks) >> 13) - 45000;
		data->humidity = ((15625 * (int32_t)rh_ticks) >> 13) - 6000;
		data->previous_poll_time = ktime_get_boottime();
	}

	mutex_unlock(&data->lock);
	return 0;
}

static ssize_t sht4x_interval_write(struct sht4x_data *data,
				    long val)
{
	data->min_poll_interval = ms_to_ktime(clamp_val(val, 2000, LONG_MAX));
	return 0;
}

/**
 * sht4x_interval_read() - read the minimum poll interval
 *			   in milliseconds
 */
static size_t sht4x_interval_read(struct sht4x_data *data,
				  long *val)
{
	*val = ktime_to_ms(data->min_poll_interval);
	return 0;
}

/**
 * sht4x_temperature1_read() - read the temperature in millidegrees
 */
static int sht4x_temperature1_read(struct sht4x_data *data, long *val)
{
	int ret;

	ret = sht4x_read_values(data);
	if (ret < 0)
		return ret;

	*val = data->temperature;
	return 0;
}

/**
 * sht4x_humidity1_read() - read a relative humidity in millipercent
 */
static int sht4x_humidity1_read(struct sht4x_data *data, long *val)
{
	int ret;

	ret = sht4x_read_values(data);
	if (ret < 0)
		return ret;

	*val = data->humidity;
	return 0;
}

static umode_t sht4x_hwmon_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
	case hwmon_humidity:
		return 0444;
	case hwmon_chip:
		return 0644;
	default:
		return 0;
	}
}

static int sht4x_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct sht4x_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return sht4x_temperature1_read(data, val);
	case hwmon_humidity:
		return sht4x_humidity1_read(data, val);
	case hwmon_chip:
		return sht4x_interval_read(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int sht4x_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long val)
{
	struct sht4x_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_chip:
		return sht4x_interval_write(data, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *sht4x_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(humidity, HWMON_H_INPUT),
	NULL,
};

static const struct hwmon_ops sht4x_hwmon_ops = {
	.is_visible = sht4x_hwmon_visible,
	.read = sht4x_hwmon_read,
	.write = sht4x_hwmon_write,
};

static const struct hwmon_chip_info sht4x_chip_info = {
	.ops = &sht4x_hwmon_ops,
	.info = sht4x_info,
};

static int sht4x_probe(struct i2c_client *client,
		       const struct i2c_device_id *sht4x_id)
{
	struct device *device = &client->dev;
	struct device *hwmon_dev;
	struct sht4x_data *data;
	u8 cmd[] = {SHT4X_CMD_RESET};
	int ret;

	/*
	 * we require full i2c support since the sht4x uses multi-byte read and
	 * writes as well as multi-byte commands which are not supported by
	 * the smbus protocol
	 */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	data = devm_kzalloc(device, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->min_poll_interval = ms_to_ktime(SHT4X_DEFAULT_MIN_POLL_INTERVAL);
	data->client = client;

	mutex_init(&data->lock);

	crc8_populate_msb(sht4x_crc8_table, SHT4X_CRC8_POLYNOMIAL);

	ret = i2c_master_send(client, cmd, SHT4X_CMD_LEN);
	if (ret != SHT4X_CMD_LEN)
		return -EIO;

	hwmon_dev = devm_hwmon_device_register_with_info(device,
							 client->name,
							 data,
							 &sht4x_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id sht4x_id[] = {
	{ "sht4x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sht4x_id);

static struct i2c_driver sht4x_driver = {
	.driver = {
		.name = "sht4x",
	},
	.probe		= sht4x_probe,
	.id_table	= sht4x_id,
};

module_i2c_driver(sht4x_driver);

MODULE_AUTHOR("Navin Sankar Velliangiri <navin@linumiz.com>");
MODULE_DESCRIPTION("Sensirion SHT4x humidity and temperature sensor driver");
MODULE_LICENSE("GPL v2");
