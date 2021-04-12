// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Vaisala Oyj. All rights reserved.
 */
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#define SCA3300_ALIAS "sca3300"

#define SCA3300_REG_STATUS 0x6
#define SCA3300_REG_MODE 0xd
#define SCA3300_REG_WHOAMI 0x10
#define SCA3300_VALUE_SW_RESET 0x20
#define SCA3300_CRC8_POLYNOMIAL 0x1d
#define SCA3300_X_READ 0
#define SCA3300_X_WRITE BIT(7)
#define SCA3300_DEVICE_ID 0x51
#define SCA3300_RS_ERROR 0x3

enum sca3300_scan_indexes {
	SCA3300_ACC_X = 0,
	SCA3300_ACC_Y,
	SCA3300_ACC_Z,
	SCA3300_TEMP,
	SCA3300_TIMESTAMP,
};

#define SCA3300_ACCEL_CHANNEL(index, reg, axis) {			\
		.type = IIO_ACCEL,					\
		.address = reg,						\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				      BIT(IIO_CHAN_INFO_PROCESSED),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.scan_index = index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.shift = 0,					\
			.endianness = IIO_CPU,				\
		},							\
	}

static const struct iio_chan_spec sca3300_channels[] = {
	SCA3300_ACCEL_CHANNEL(SCA3300_ACC_X, 0x1, X),
	SCA3300_ACCEL_CHANNEL(SCA3300_ACC_Y, 0x2, Y),
	SCA3300_ACCEL_CHANNEL(SCA3300_ACC_Z, 0x3, Z),
	{
		.type = IIO_TEMP,
		.address = 0x5,
		.scan_index = SCA3300_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.shift = 0,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const int sca3300_accel_scale[] = {2700, 1350, 5400, 5400};

static const unsigned long sca3300_scan_masks[] = {
	BIT(SCA3300_ACC_X) | BIT(SCA3300_ACC_Y) | BIT(SCA3300_ACC_Z) |
	BIT(SCA3300_TEMP),
	0};

/**
 * SCA3300 device data
 *
 * @spi SPI device structure
 * @opmode Device operation mode
 * @lock Data buffer lock
 * @txbuf Transmit buffer
 * @rxbuf Receive buffer
 * @scan Triggered buffer. Four channel 16-bit data + 64-bit timestamp
 */
struct sca3300_data {
	struct spi_device *spi;
	u32 opmode;
	struct mutex lock;
	u8 txbuf[4];
	u8 rxbuf[4];
	struct {
		s16 channels[4];
		s64 ts __aligned(sizeof(s64));
	} scan;
};

DECLARE_CRC8_TABLE(sca3300_crc_table);

static int sca3300_transfer(struct sca3300_data *sca_data, int *val)
{
	struct spi_delay delay = {.value = 10, .unit = SPI_DELAY_UNIT_USECS};
	int32_t ret;
	int rs;
	u8 crc;
	struct spi_transfer xfers[2] = {
		{
			.tx_buf = sca_data->txbuf,
			.rx_buf = NULL,
			.len = ARRAY_SIZE(sca_data->txbuf),
			.delay = delay,
			.cs_change = 1,
		},
		{
			.tx_buf = NULL,
			.rx_buf = sca_data->rxbuf,
			.len = ARRAY_SIZE(sca_data->rxbuf),
			.delay = delay,
			.cs_change = 0,
		}
	};

	/* inverted crc value as described in device data sheet */
	crc = ~crc8(sca3300_crc_table, &sca_data->txbuf[0], 3, CRC8_INIT_VALUE);
	sca_data->txbuf[3] = crc;

	ret = spi_sync_transfer(sca_data->spi, xfers, 2);
	if (ret < 0) {
		dev_err(&sca_data->spi->dev,
			"transfer error, error: %d\n", ret);
		return -EIO;
	}

	crc = ~crc8(sca3300_crc_table, &sca_data->rxbuf[0], 3, CRC8_INIT_VALUE);
	if (sca_data->rxbuf[3] != crc) {
		dev_err(&sca_data->spi->dev, "CRC checksum mismatch");
		return -EIO;
	}

	/* get return status */
	rs = sca_data->rxbuf[0] & 0x03;
	if (rs == SCA3300_RS_ERROR)
		return rs;

	*val = (s16)(sca_data->rxbuf[2] | (sca_data->rxbuf[1] << 8));

	return 0;
}

static int sca3300_read_reg(struct sca3300_data *sca_data, u8 reg, int *val)
{
	int ret;

	mutex_lock(&sca_data->lock);
	sca_data->txbuf[0] = SCA3300_X_READ | (reg << 2);
	ret = sca3300_transfer(sca_data, val);
	if (ret > 0) {
		sca_data->txbuf[0] = SCA3300_X_READ | (SCA3300_REG_STATUS << 2);
		ret = sca3300_transfer(sca_data, val);
		/* status 0 = startup, 0x2 = mode change */
		if (ret > 0 && *val != 0 && *val != 0x2) {
			dev_err_ratelimited(&sca_data->spi->dev,
					    "device status: %x\n",
					    (u16)*val);
			mutex_unlock(&sca_data->lock);
			return -EIO;
		}
		if (ret > 0)
			ret = 0;
	}
	mutex_unlock(&sca_data->lock);

	return ret;
}

static int sca3300_write_reg(struct sca3300_data *sca_data, u8 reg, int val)
{
	int reg_val = 0;
	int ret;

	mutex_lock(&sca_data->lock);
	sca_data->txbuf[0] = SCA3300_X_WRITE | (reg << 2);
	sca_data->txbuf[1] = val >> 8;
	sca_data->txbuf[2] = val & 0xFF;
	ret = sca3300_transfer(sca_data, &reg_val);
	if (ret > 0) {
		sca_data->txbuf[0] = SCA3300_X_READ | (SCA3300_REG_STATUS << 2);
		ret = sca3300_transfer(sca_data, &reg_val);
		/* status 0 = startup, 0x2 = mode change */
		if (ret > 0 && reg_val != 0 && reg_val != 0x2) {
			dev_err_ratelimited(&sca_data->spi->dev,
					    "device status: %x\n",
					    (u16)reg_val);
			mutex_unlock(&sca_data->lock);
			return -EIO;
		}
		if (ret > 0)
			ret = 0;
	}
	mutex_unlock(&sca_data->lock);

	return ret;
}

static int sca3300_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct sca3300_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val < 0 || val > 3)
			return -EINVAL;
		return sca3300_write_reg(data, SCA3300_REG_MODE, val);
	default:
		return -EINVAL;
	}
}

static int sca3300_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct sca3300_data *data = iio_priv(indio_dev);
	int ret;
	int reg_val;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = sca3300_read_reg(data, chan->address, val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = sca3300_read_reg(data, SCA3300_REG_MODE, &reg_val);
		if (ret < 0)
			return ret;
		*val = sca3300_accel_scale[reg_val];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		ret = sca3300_read_reg(data, SCA3300_REG_MODE, &reg_val);
		if (ret < 0)
			return ret;
		*val2 = sca3300_accel_scale[reg_val];
		ret = sca3300_read_reg(data, chan->address, val);
		if (ret < 0)
			return ret;
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static irqreturn_t sca3300_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct sca3300_data *data = iio_priv(indio_dev);
	s64 time_ns = iio_get_time_ns(indio_dev);
	int bit, ret, val, i = 0;

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = sca3300_read_reg(data, sca3300_channels[bit].address,
				       &val);
		if (ret < 0)
			goto out;
		if (ARRAY_SIZE(data->scan.channels) > i)
			((s16 *)data->scan.channels)[i++] = val;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan, time_ns);
out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int sca3300_init(struct sca3300_data *sca_data,
			struct iio_dev *indio_dev)
{
	int ret;
	int value = 0;

	if (sca_data->opmode < 1 || sca_data->opmode > 4)
		return -EINVAL;

	ret = sca3300_write_reg(sca_data, SCA3300_REG_MODE,
				SCA3300_VALUE_SW_RESET);
	if (ret != 0)
		return ret;
	usleep_range(2e3, 10e3);

	ret = sca3300_write_reg(sca_data, SCA3300_REG_MODE,
				sca_data->opmode - 1);
	if (ret != 0)
		return ret;
	msleep(100);
	ret = sca3300_read_reg(sca_data, SCA3300_REG_WHOAMI, &value);
	if (ret != 0)
		return ret;

	if (value != SCA3300_DEVICE_ID) {
		dev_err(&sca_data->spi->dev, "device id not expected value\n");
		return -EIO;
	}
	return 0;
}

static int sca3300_debugfs_reg_access(struct iio_dev *indio_dev,
				      unsigned int reg, unsigned int writeval,
				      unsigned int *readval)
{
	struct sca3300_data *data = iio_priv(indio_dev);
	int value;
	int ret;

	if (reg > 0x1f)
		return -EINVAL;

	if (!readval)
		return sca3300_write_reg(data, reg, writeval);

	ret = sca3300_read_reg(data, reg, &value);
	if (ret < 0)
		return ret;

	*readval = (unsigned int)value;

	return 0;
}

static const struct iio_info sca3300_info = {
	.read_raw = sca3300_read_raw,
	.write_raw = sca3300_write_raw,
	.debugfs_reg_access = &sca3300_debugfs_reg_access,
};

static int sca3300_probe(struct spi_device *spi)
{
	struct sca3300_data *sca_data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*sca_data));
	if (!indio_dev) {
		dev_err(&spi->dev,
			"failed to allocate memory for iio device\n");
		return -ENOMEM;
	}

	sca_data = iio_priv(indio_dev);
	mutex_init(&sca_data->lock);
	sca_data->spi = spi;
	spi_set_drvdata(spi, indio_dev);

	crc8_populate_msb(sca3300_crc_table, SCA3300_CRC8_POLYNOMIAL);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &sca3300_info;
	indio_dev->name = SCA3300_ALIAS;
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_TRIGGERED;
	indio_dev->channels = sca3300_channels;
	indio_dev->num_channels = ARRAY_SIZE(sca3300_channels);
	indio_dev->available_scan_masks = sca3300_scan_masks;

	if (spi->dev.of_node) {
		ret = of_property_read_u32(spi->dev.of_node, "murata,opmode",
					   &sca_data->opmode);
		if (ret < 0)
			return ret;
	}

	ret = sca3300_init(sca_data, indio_dev);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to init device, error: %d\n", ret);
		return ret;
	}

	ret = iio_triggered_buffer_setup(indio_dev, iio_pollfunc_store_time,
					 sca3300_trigger_handler, NULL);
	if (ret < 0) {
		dev_err(&spi->dev,
			"iio triggered buffer setup failed, error: %d\n", ret);
		return ret;
	}

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret < 0) {
		dev_err(&spi->dev, "iio device register failed, error: %d\n",
			ret);
		iio_triggered_buffer_cleanup(indio_dev);
		return ret;
	}

	return 0;
}

static int sca3300_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_triggered_buffer_cleanup(indio_dev);
	return 0;
}

static const struct of_device_id sca3300_dt_ids[] = {
	{ .compatible = "murata,sca3300"},
	{},
};
MODULE_DEVICE_TABLE(of, sca3300_dt_ids);

static struct spi_driver sca3300_driver = {
	.driver = {
		.name		= SCA3300_ALIAS,
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(sca3300_dt_ids),
	},

	.probe	= sca3300_probe,
	.remove	= sca3300_remove,
};

module_spi_driver(sca3300_driver);

MODULE_AUTHOR("Tomas Melin <tomas.melin@vaisala.com>");
MODULE_DESCRIPTION("Murata SCA3300 SPI Accelerometer");
MODULE_LICENSE("GPL v2");
