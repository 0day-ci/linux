// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SCD4X carbon dioxide sensor i2c driver
 *
 * Copyright (C) 2021 Protonic Holland
 * Author: Roan van Dijk <roan@protonic.nl>
 *
 * I2C slave address: 0x62
 *
 * Datasheets:
 * https://www.sensirion.com/file/datasheet_scd4x
 */

#include <asm/unaligned.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/types.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define SCD4X_CRC8_POLYNOMIAL 0x31
#define SCD4X_TIMEOUT_ERR 1000
#define SCD4X_READ_BUF_SIZE 9
#define SCD4X_COMMAND_BUF_SIZE 2
#define SCD4X_WRITE_BUF_SIZE 5
#define SCD4X_FRC_MIN_PPM 400
#define SCD4X_FRC_MAX_PPM 2000

/*Commands SCD4X*/
enum scd4x_cmd {
	CMD_START_MEAS          = 0x21b1,
	CMD_READ_MEAS           = 0xec05,
	CMD_STOP_MEAS           = 0x3f86,
	CMD_SET_TEMP_OFFSET     = 0x241d,
	CMD_GET_TEMP_OFFSET     = 0x2318,
	CMD_FRC                 = 0x362f,
	CMD_SET_ASC             = 0x2416,
	CMD_GET_ASC             = 0x2313,
	CMD_GET_DATA_READY      = 0xe4b8,
};

enum scd4x_channel_idx {
	SCD4X_CO2,
	SCD4X_TEMP,
	SCD4X_HR,
};

struct scd4x_state {
	struct i2c_client *client;
	struct mutex lock;
	struct device *dev;
	struct regulator *vdd;

	int irq;
	uint16_t meas[3];
};

DECLARE_CRC8_TABLE(scd4x_crc8_table);

static int scd4x_i2c_xfer(struct scd4x_state *state, char *txbuf, int txsize,
				char *rxbuf, int rxsize)
{
	struct i2c_client *client = to_i2c_client(state->dev);
	int ret;

	ret = i2c_master_send(client, txbuf, txsize);

	if (ret < 0)
		return ret;
	if (ret != txsize)
		return -EIO;

	if (rxsize == 0)
		return 0;

	ret = i2c_master_recv(client, rxbuf, rxsize);
	if (ret < 0)
		return ret;
	if (ret != rxsize)
		return -EIO;

	return 0;
}

static int scd4x_send_command(struct scd4x_state *state, enum scd4x_cmd cmd)
{
	char buf[SCD4X_COMMAND_BUF_SIZE];
	int ret;

	/*
	 * Measurement needs to be stopped before sending commands.
	 * Except stop and start command.
	 */
	if ((cmd != CMD_STOP_MEAS) & (cmd != CMD_START_MEAS)) {

		ret = scd4x_send_command(state, CMD_STOP_MEAS);
		if (ret)
			return ret;

		/* execution time for stopping measurement */
		msleep_interruptible(500);
	}

	put_unaligned_be16(cmd, buf);
	ret = scd4x_i2c_xfer(state, buf, 2, buf, 0);
	if (ret)
		return ret;

	if ((cmd != CMD_STOP_MEAS) & (cmd != CMD_START_MEAS)) {
		ret = scd4x_send_command(state, CMD_START_MEAS);
		if (ret)
			return ret;
	}

	return 0;
}

static int scd4x_read(struct scd4x_state *state, enum scd4x_cmd cmd,
			void *response, int byte_cnt)
{
	char buf[SCD4X_READ_BUF_SIZE];
	char *rsp = response;
	int i, ret;
	char crc;

	/*
	 * Measurement needs to be stopped before sending commands.
	 * Except for reading measurement and data ready command.
	 */
	if ((cmd != CMD_GET_DATA_READY) & (cmd != CMD_READ_MEAS)) {
		ret = scd4x_send_command(state, CMD_STOP_MEAS);
		if (ret)
			return ret;

		/* execution time for stopping measurement */
		msleep_interruptible(500);
	}

	put_unaligned_be16(cmd, buf);
	ret = scd4x_i2c_xfer(state, buf, 2, buf, byte_cnt);
	if (ret)
		return ret;

	for (i = 0; i < byte_cnt; i += 3) {
		crc = crc8(scd4x_crc8_table, buf + i, 2, CRC8_INIT_VALUE);
		if (crc != buf[i + 2]) {
			dev_err(state->dev, "CRC error\n");
			return -EIO;
		}

		*rsp++ = buf[i];
		*rsp++ = buf[i + 1];
	}

	/* start measurement */
	if ((cmd != CMD_GET_DATA_READY) & (cmd != CMD_READ_MEAS)) {
		ret = scd4x_send_command(state, CMD_START_MEAS);
		if (ret)
			return ret;
	}

	return 0;
}

static int scd4x_write(struct scd4x_state *state, enum scd4x_cmd cmd, uint16_t arg)
{
	char buf[SCD4X_WRITE_BUF_SIZE];
	int ret;
	char crc;

	put_unaligned_be16(cmd, buf);
	put_unaligned_be16(arg, buf + 2);

	crc = crc8(scd4x_crc8_table, buf + 2, 2, CRC8_INIT_VALUE);
	buf[4] = crc;

	/* measurement needs to be stopped before sending commands */
	ret = scd4x_send_command(state, CMD_STOP_MEAS);
	if (ret)
		return ret;

	/* execution time */
	msleep_interruptible(500);

	ret = scd4x_i2c_xfer(state, buf, SCD4X_WRITE_BUF_SIZE, buf, 0);
	if (ret)
		return ret;

	/* start measurement, except for forced calibration command */
	if (cmd != CMD_FRC) {
		ret = scd4x_send_command(state, CMD_START_MEAS);
		if (ret)
			return ret;
	}

	return 0;
}

static int scd4x_write_and_fetch(struct scd4x_state *state, enum scd4x_cmd cmd,
				uint16_t arg, void *response, int byte_cnt)
{
	struct i2c_client *client = to_i2c_client(state->dev);
	char buf[SCD4X_READ_BUF_SIZE];
	char *rsp = response;
	int i, ret;
	char crc;

	ret = scd4x_write(state, CMD_FRC, arg);
	if (ret) {
		scd4x_send_command(state, CMD_START_MEAS);
		return ret;
	}

	/* Execution time */
	msleep_interruptible(400);

	ret = i2c_master_recv(client, buf, byte_cnt);
	if (ret < 0)
		return ret;
	if (ret != byte_cnt)
		return -EIO;

	for (i = 0; i < byte_cnt; i += 3) {
		crc = crc8(scd4x_crc8_table, buf + i, 2, CRC8_INIT_VALUE);
		if (crc != buf[i + 2]) {
			dev_err(state->dev, "CRC error\n");
			scd4x_send_command(state, CMD_START_MEAS);
			return -EIO;
		}

		*rsp++ = buf[i];
		*rsp++ = buf[i + 1];
	}

	ret = scd4x_send_command(state, CMD_START_MEAS);
	if (ret)
		return ret;

	return 0;
}

static int scd4x_read_meas(struct scd4x_state *state)
{
	int i, ret;

	ret = scd4x_read(state, CMD_READ_MEAS, state->meas, 9);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(state->meas); i++)
		state->meas[i] = be16_to_cpu(state->meas[i]);
	/*
	 * Temperature and humidity values are convertered and scaled to
	 * milli deg C and milli percent.
	 */
	state->meas[SCD4X_TEMP] = ((175 * state->meas[SCD4X_TEMP]/65536) - 45)*1000;
	state->meas[SCD4X_HR] = ((100 * state->meas[SCD4X_HR]/65536))*1000;

	return 0;
}

static int scd4x_wait_meas_poll(struct scd4x_state *state)
{
	int tries = 6;
	int ret;

	do {
		uint16_t val;

		ret = scd4x_read(state, CMD_GET_DATA_READY, &val, 3);
		if (ret)
			return -EIO;
		val = be16_to_cpu(val);

		/* new measurement available */
		if (val & 0x7FF)
			break;

		msleep_interruptible(1000);
	} while (--tries);

	if (tries == 0) {
		/* try to start sensor on timeout */
		ret = scd4x_send_command(state, CMD_START_MEAS);
		if (ret)
			dev_err(state->dev, "failed to start measurement: %d\n", ret);
	}

	return tries ? 0 : -ETIMEDOUT;
}

static int scd4x_read_poll(struct scd4x_state *state)
{
	int ret;

	ret = scd4x_wait_meas_poll(state);
	if (ret)
		return ret;

	return scd4x_read_meas(state);
}

static int scd4x_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan, int *val,
			int *val2, long mask)
{
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret;
	uint16_t tmp;

	mutex_lock(&state->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			break;

		ret = scd4x_read_poll(state);
		if (ret) {
			iio_device_release_direct_mode(indio_dev);
			break;
		}
		*val = state->meas[chan->address];
		iio_device_release_direct_mode(indio_dev);
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = scd4x_read(state, CMD_GET_TEMP_OFFSET, &tmp, 3);
		if (ret)
			break;
		*val = 175000 * be16_to_cpu(tmp) / 65536;
		ret = IIO_VAL_INT;
		break;
	}
	mutex_unlock(&state->lock);

	return ret;
}

static int scd4x_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&state->lock);
	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		val = val * 65536 / 175000;
		ret = scd4x_write(state, CMD_SET_TEMP_OFFSET, val);
		if (ret)
			break;
	}
	mutex_unlock(&state->lock);

	return ret;
}

static ssize_t calibration_auto_enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret;
	uint16_t val;

	mutex_lock(&state->lock);
	ret = scd4x_read(state, CMD_GET_ASC, &val, 3);
	mutex_unlock(&state->lock);
	if (ret)
		dev_err(dev, "failed to read automatic calibration");

	val = be16_to_cpu(val);

	return ret ?: sprintf(buf, "%d\n", val);
}

static ssize_t calibration_auto_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	bool val;
	int ret;
	uint16_t value;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	value = val;

	mutex_lock(&state->lock);
	ret = scd4x_write(state, CMD_SET_ASC, value);
	mutex_unlock(&state->lock);
	if (ret)
		dev_err(dev, "failed to set automatic calibration");

	return ret ?: len;
}

static ssize_t calibration_forced_value_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	uint16_t val, arg;
	int ret;

	ret = kstrtou16(buf, 0, &arg);
	if (ret)
		return ret;

	if (arg < SCD4X_FRC_MIN_PPM || arg > SCD4X_FRC_MAX_PPM)
		return -EINVAL;

	mutex_lock(&state->lock);
	ret = scd4x_write_and_fetch(state, CMD_FRC, arg, &val, 3);
	mutex_unlock(&state->lock);

	if (val == 0xff) {
		dev_err(state->dev, "forced calibration has failed");
		return -EINVAL;
	}

	return ret ?: len;
}

static IIO_DEVICE_ATTR_RW(calibration_auto_enable, 0);
static IIO_DEVICE_ATTR_WO(calibration_forced_value, 0);

static struct attribute *scd4x_attrs[] = {
	&iio_dev_attr_calibration_auto_enable.dev_attr.attr,
	&iio_dev_attr_calibration_forced_value.dev_attr.attr,
	NULL
};

static const struct attribute_group scd4x_attr_group = {
	.attrs = scd4x_attrs,
};
static const struct iio_info scd4x_info = {
	.attrs = &scd4x_attr_group,
	.read_raw = scd4x_read_raw,
	.write_raw = scd4x_write_raw,
};

static const struct iio_chan_spec scd4x_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.address = SCD4X_CO2,
		.scan_index = SCD4X_CO2,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED)|
				      BIT(IIO_CHAN_INFO_CALIBBIAS),
		.address = SCD4X_TEMP,
		.scan_index = SCD4X_TEMP,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.address = SCD4X_HR,
		.scan_index = SCD4X_HR,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
};

int __maybe_unused scd4x_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct scd4x_state *state  = iio_priv(indio_dev);
	int ret;

	ret = scd4x_send_command(state, CMD_STOP_MEAS);
	if (ret)
		return ret;

	return regulator_disable(state->vdd);
}
EXPORT_SYMBOL(scd4x_suspend);

int __maybe_unused scd4x_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(state->vdd);
	if (ret)
		return ret;

	return scd4x_send_command(state, CMD_START_MEAS);
}
EXPORT_SYMBOL(scd4x_resume);

static void scd4x_stop_meas(void *data)
{
	struct scd4x_state *state = data;

	scd4x_send_command(state, CMD_STOP_MEAS);
}

static void scd4x_disable_regulator(void *data)
{
	struct scd4x_state *state = data;

	regulator_disable(state->vdd);
}

static irqreturn_t scd4x_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct scd4x_state *state = iio_priv(indio_dev);
	struct {
		int data[3];
		int64_t ts __aligned(8);
	} scan;
	int ret;

	mutex_lock(&state->lock);
	ret = scd4x_read_poll(state);
	memset(&scan, 0, sizeof(scan));
	memcpy(scan.data, state->meas, sizeof(state->meas));
	mutex_unlock(&state->lock);
	if (ret)
		goto out;

	iio_push_to_buffers_with_timestamp(indio_dev, &scan, iio_get_time_ns(indio_dev));
out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int scd4x_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct scd4x_state *st = iio_priv(indio_dev);

	if (state)
		enable_irq(st->irq);
	else
		disable_irq(st->irq);

	return 0;
}

static const struct iio_trigger_ops scd4x_trigger_ops = {
	.set_trigger_state = scd4x_set_trigger_state,
	.validate_device = iio_trigger_validate_own_device,
};

static int scd4x_setup_trigger(struct iio_dev *indio_dev)
{
	struct device *dev = indio_dev->dev.parent;
	struct iio_trigger *trig;
	int ret;

	trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name,
				      iio_device_id(indio_dev));
	if (!trig) {
		dev_err(dev, "failed to allocate trigger\n");
		return -ENOMEM;
	}

	trig->ops = &scd4x_trigger_ops;
	iio_trigger_set_drvdata(trig, indio_dev);

	ret = devm_iio_trigger_register(dev, trig);
	if (ret)
		return ret;

	indio_dev->trig = iio_trigger_get(trig);

	return ret;
}

int scd4x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	static const unsigned long scd4x_scan_masks[] = { 0x07, 0x00 };
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct scd4x_state *state;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	mutex_init(&state->lock);
	state->dev = dev;
	crc8_populate_msb(scd4x_crc8_table, SCD4X_CRC8_POLYNOMIAL);

	indio_dev->info = &scd4x_info;
	indio_dev->name = client->name;
	indio_dev->channels = scd4x_channels;
	indio_dev->num_channels = ARRAY_SIZE(scd4x_channels);
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_TRIGGERED;
	indio_dev->available_scan_masks = scd4x_scan_masks;

	state->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(state->vdd))
		return dev_err_probe(dev, PTR_ERR(state->vdd), "failed to get regulator\n");

	ret = regulator_enable(state->vdd);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, scd4x_disable_regulator, state);
	if (ret)
		return ret;

	ret = scd4x_send_command(state, CMD_STOP_MEAS);
	if (ret) {
		dev_err(dev, "failed to stop measurement: %d\n", ret);
		return ret;
	}

	/* execution time */
	msleep_interruptible(500);

	if (state->irq > 0) {
		ret = scd4x_setup_trigger(indio_dev);
		if (ret) {
			dev_err(dev, "failed to setup trigger: %d\n", ret);
			return ret;
		}
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL, scd4x_trigger_handler, NULL);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret) {
		dev_err(dev, "failed to register iio device\n");
		return ret;
	}

	ret = scd4x_send_command(state, CMD_START_MEAS);
	if (ret) {
		dev_err(dev, "failed to start measurement: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, scd4x_stop_meas, state);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(scd4x_probe);

static const struct of_device_id scd4x_dt_ids[] = {
	{ .compatible = "sensirion,scd4x" },
	{ }
};
MODULE_DEVICE_TABLE(of, scd4x_dt_ids);

static struct i2c_driver scd4x_i2c_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = scd4x_dt_ids,
	},
	.probe = scd4x_probe,
};
module_i2c_driver(scd4x_i2c_driver);

MODULE_AUTHOR("Roan van Dijk <roan@protonic.nl>");
MODULE_DESCRIPTION("Sensirion SCD4X carbon dioxide sensor core driver");
MODULE_LICENSE("GPL v2");
