// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADS1018 - Texas Instruments Analog-to-Digital Converter
 *
 * Copyright 2021 Iain Hunter <iain@hunterembedded.co.uk>
 *
 * Based on ti-ads1015.c
 * Copyright (c) 2016, Intel Corporation.
 *
 * IIO driver for 12 bit SPI ADC ADS1018 from TI
 *
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/init.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/delay.h>


#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>


#define ADS1018_DRV_NAME "ads1018"

#define TI_ADS1018_START_SINGLE_SHOT            BIT(15)

#define TI_ADS1018_MUX_AIN0_SINGLE_ENDED	BIT(14)
#define TI_ADS1018_MUX_AIN1_SINGLE_ENDED	(BIT(14) | BIT(12))

#define TI_ADS1018_MUX_MASK			0x7
#define TI_ADS1018_MUX_SHIFT			12

#define TI_ADS1018_PGA_2_048V			BIT(10)

#define TI_ADS1018_PGA_MASK			0x7
#define TI_ADS1018_PGA_SHIFT			9

#define TI_ADS1018_MODE_SINGLE_SHOT		BIT(8)
#define TI_ADS1018_MODE_CONTINUOUS		0x0

#define TI_ADS1018_MODE_MASK			0x1
#define TI_ADS1018_MODE_SHIFT			8

#define TI_ADS1018_STATE_RATE_1600		BIT(7)
#define TI_ADS1018_STATE_RATE_2400		(BIT(7) | BIT(5))
#define TI_ADS1018_STATE_RATE_3300		(BIT(7) | BIT(6))

#define TI_ADS1018_DR_MASK			0x7
#define TI_ADS1018_DR_SHIFT			5

#define TI_ADS1018_TS_MODE_ADC			0x0
#define TI_ADS1018_TS_MODE_TEMP			BIT(4)

#define TI_ADS1018_PULLUP_DOUT_DISABLE		0x0
#define TI_ADS1018_PULLUP_DOUT_ENABLE		BIT(3)

#define TI_ADS1018_NOP_UPDATE_CONFIG		BIT(1)
#define TI_ADS1018_RESERVED			BIT(0)

#define TI_ADS1018_CONFIGURE    (TI_ADS1018_PGA_2_048V | \
				TI_ADS1018_MODE_CONTINUOUS | \
				TI_ADS1018_STATE_RATE_2400 | \
				TI_ADS1018_TS_MODE_ADC | \
				TI_ADS1018_PULLUP_DOUT_ENABLE | \
				TI_ADS1018_NOP_UPDATE_CONFIG | \
				TI_ADS1018_RESERVED)


#define ADS1018_CHANNELS                        8

#define TI_ADS1018_TIMESTAMP_SIZE (sizeof(int64_t) / sizeof(__be16))

/* val = value, dec = left shift, bits = number of bits of the mask */
#define TI_ADS1018_EXTRACT(val, dec, bits) \
	(((val) >> (dec)) & ((1 << (bits)) - 1))



#define ADS1018_SLEEP_DELAY_MS			2000
#define ADS1018_DEFAULT_PGA			2
#define ADS1018_DEFAULT_DATA_RATE		4
#define ADS1018_DEFAULT_CHAN			0


struct ads1018_channel_data {
	bool enabled;
	unsigned int pga;
	unsigned int data_rate;
};


enum chip_ids {
	TI_ADS1018,
	TI_ADS1118,
};

enum ads1018_channels {
	ADS1018_AIN0_AIN1 = 0,
	ADS1018_AIN0_AIN3,
	ADS1018_AIN1_AIN3,
	ADS1018_AIN2_AIN3,
	ADS1018_AIN0,
	ADS1018_AIN1,
	ADS1018_AIN2,
	ADS1018_AIN3,
	ADS1018_TIMESTAMP,
};

struct ti_ads1018_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

static const unsigned int ads1018_data_rate[] = {
	128, 250, 490, 920, 1600, 2400, 3300, 3300
};

/*
 * Translation from PGA bits to full-scale positive and negative input voltage
 * range in mV
 */
static int ads1018_fullscale_range[] = {
	6144, 4096, 2048, 1024, 512, 256, 256, 256
};


#define ADS1018_V_CHAN(_chan, _addr) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 12,					\
		.storagebits = 16,				\
		.shift = 4,					\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = "AIN"#_chan,				\
}

#define ADS1018_V_DIFF_CHAN(_chan, _chan2, _addr) {		\
	.type = IIO_VOLTAGE,					\
	.differential = 1,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.channel2 = _chan2,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 12,					\
		.storagebits = 16,				\
		.shift = 4,					\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = "AIN"#_chan"-AIN"#_chan2,		\
}


struct ti_ads1018_state {
	struct spi_device	*spi;
	struct spi_transfer	scan_single_xfer[1];
	struct spi_message	scan_single_msg;

	struct iio_trigger      *trig;
	/*
	 * Protects ADC ops, e.g: concurrent sysfs/buffered
	 * data reads, configuration updates
	 */
	struct mutex                   lock;
	struct ads1018_channel_data    channel_data[ADS1018_CHANNELS];
	unsigned int                   current_config;
	unsigned int                   settings;
	unsigned int                   *data_rate;
	/*
	 * Set to true when the ADC is switched to the continuous-conversion
	 * mode and exits from a power-down state.  This flag is used to avoid
	 * getting the stale result from the conversion register.
	 */
	bool	conv_invalid;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	unsigned char	rx_buf[(ADS1018_CHANNELS + TI_ADS1018_TIMESTAMP_SIZE)*2]
				____cacheline_aligned;
	unsigned char	tx_buf[ADS1018_CHANNELS*2];

};



static const struct iio_chan_spec ti_ads1018_channels[] = {
	ADS1018_V_DIFF_CHAN(0, 1, ADS1018_AIN0_AIN1),
	ADS1018_V_DIFF_CHAN(0, 3, ADS1018_AIN0_AIN3),
	ADS1018_V_DIFF_CHAN(1, 3, ADS1018_AIN1_AIN3),
	ADS1018_V_DIFF_CHAN(2, 3, ADS1018_AIN2_AIN3),
	ADS1018_V_CHAN(0, ADS1018_AIN0),
	ADS1018_V_CHAN(1, ADS1018_AIN1),
	ADS1018_V_CHAN(2, ADS1018_AIN2),
	ADS1018_V_CHAN(3, ADS1018_AIN3),
	IIO_CHAN_SOFT_TIMESTAMP(ADS1018_TIMESTAMP),
};

static const struct ti_ads1018_chip_info ti_ads1018_chip_info[] = {
	[TI_ADS1018] = {
		.channels	= ti_ads1018_channels,
		.num_channels	= ARRAY_SIZE(ti_ads1018_channels),
	},
};


static int ti_ads1018_write_config_register(struct ti_ads1018_state *st, int value)
{
	int ret;

	/* Write new value to the first 2 bytes of the 4 byte SPI transfer	*/
	st->tx_buf[0] = (value & 0xFF00)>>8;
	st->tx_buf[1] = value & 0xFF;
	st->tx_buf[2] = 0x00;
	st->tx_buf[3] = 0x00;

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	/* Update the state current config based on result of this write */
	/* the updated config register value is in second 16 bit word    */
	/* which is third and fourth byte                                */
	st->current_config = (st->rx_buf[2] << 8);
	st->current_config |= st->rx_buf[3];

	return 0;
}


#ifdef CONFIG_PM
static int ads1018_set_power_state(struct ti_ads1018_state *st, bool on)
{
	int ret;
	struct spi_device *spi = st->spi;
	struct device *dev = &(spi->dev);

	if (on) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0)
			pm_runtime_put_noidle(dev);
	} else {
		pm_runtime_mark_last_busy(dev);
		ret = pm_runtime_put_autosuspend(dev);
	}

	return ret < 0 ? ret : 0;
}
#else /* !CONFIG_PM */
static int ads1018_set_power_state(struct ti_ads1018_state *st, bool on)
{
	return 0;
}
#endif /* !CONFIG_PM */

static
int ads1018_get_adc_result(struct ti_ads1018_state *st, int chan, int *val)
{
	int ret, pga, dr, conv_time;
	unsigned int old, mask, cfg;
	unsigned int cmd;

	if (chan < 0 || chan >= ADS1018_CHANNELS)
		return -EINVAL;


	old = st->current_config;

	/* For just a data read cmd is 0x0000
	 * Assumption is that previously the configuration has been set to
	 * select channel, gain and sampling frequency
	 */
	cmd = 0x0000;

	st->tx_buf[0] = (cmd & 0xFF00)>>8;
	st->tx_buf[1] = cmd & 0xFF;
	st->tx_buf[2] = 0x00;
	st->tx_buf[3] = 0x00;
	ret = spi_sync(st->spi, &st->scan_single_msg);

	if (ret)
		return ret;

	*val = (st->rx_buf[0] << 8);
	*val |= st->rx_buf[1];

	pga = st->channel_data[chan].pga;
	dr = st->channel_data[chan].data_rate;
	mask =  (TI_ADS1018_MUX_MASK << TI_ADS1018_MUX_SHIFT) |
		(TI_ADS1018_PGA_MASK << TI_ADS1018_PGA_SHIFT) |
		(TI_ADS1018_DR_MASK << TI_ADS1018_DR_SHIFT);

	cfg = chan << TI_ADS1018_MUX_SHIFT | pga << TI_ADS1018_PGA_SHIFT |
		dr << TI_ADS1018_DR_SHIFT;

	cfg = (old & ~mask) | (cfg & mask);


	if (old != cfg || st->conv_invalid) {
		int dr_old;

		/* Update the configuration to the new one  */
		ret = ti_ads1018_write_config_register(st, cfg);
		if (ret)
			return ret;

		dr_old = (old & TI_ADS1018_DR_MASK) >>
				TI_ADS1018_DR_SHIFT;

		conv_time = DIV_ROUND_UP(USEC_PER_SEC, st->data_rate[dr_old]);
		conv_time += DIV_ROUND_UP(USEC_PER_SEC, st->data_rate[dr]);
		conv_time += conv_time / 10; /* 10% internal clock inaccuracy */
		usleep_range(conv_time, conv_time + 1);
		st->conv_invalid = false;
	}

	return 0;

}

static irqreturn_t ti_ads1018_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ti_ads1018_state *st = iio_priv(indio_dev);
	/* Ensure natural alignment of timestamp */
	struct {
		s16 chan;
		s64 timestamp __aligned(8);

	} scan;

	int chan, ret, res;

	memset(&scan, 0, sizeof(scan));

	mutex_lock(&st->lock);
	chan = find_first_bit(indio_dev->active_scan_mask,
			      indio_dev->masklength);
	ret = ads1018_get_adc_result(st, chan, &res);
	if (ret < 0) {
		mutex_unlock(&st->lock);
		goto err;
	}

	scan.chan = res;
	mutex_unlock(&st->lock);

	iio_push_to_buffers_with_timestamp(indio_dev, &scan,
					   iio_get_time_ns(indio_dev));

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ads1018_set_scale(struct ti_ads1018_state *st,
			     struct iio_chan_spec const *chan,
			     int scale, int uscale)
{
	int i;
	int fullscale = div_s64((scale * 1000000LL + uscale) <<
				(chan->scan_type.realbits - 1), 1000000);

	for (i = 0; i < ARRAY_SIZE(ads1018_fullscale_range); i++) {
		if (ads1018_fullscale_range[i] == fullscale) {
			st->channel_data[chan->address].pga = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int ads1018_set_data_rate(struct ti_ads1018_state *st, int chan, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ads1018_data_rate); i++) {
		if (st->data_rate[i] == rate) {
			st->channel_data[chan].data_rate = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int ads1018_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	int ret, idx;
	struct ti_ads1018_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int shift = chan->scan_type.shift;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			break;

		ret = ads1018_set_power_state(st, true);
		if (ret < 0)
			goto release_direct;

		ret = ads1018_get_adc_result(st, chan->address, val);
		if (ret < 0) {
			ads1018_set_power_state(st, false);
			goto release_direct;
		}

		*val = sign_extend32(*val >> shift, 15 - shift);

		ret = ads1018_set_power_state(st, false);
		if (ret < 0)
			goto release_direct;

		ret = IIO_VAL_INT;
release_direct:
		iio_device_release_direct_mode(indio_dev);
		break;
	}
	case IIO_CHAN_INFO_SCALE:
		idx = st->channel_data[chan->address].pga;
		*val = ads1018_fullscale_range[idx];
		*val2 = chan->scan_type.realbits - 1;
		ret = IIO_VAL_FRACTIONAL_LOG2;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		idx = st->channel_data[chan->address].data_rate;
		*val = st->data_rate[idx];
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&st->lock);

	return ret;
}

static int ads1018_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct ti_ads1018_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = ads1018_set_scale(st, chan, val, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ads1018_set_data_rate(st, chan->address, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&st->lock);

	return ret;
}

static int ads1018_buffer_preenable(struct iio_dev *indio_dev)
{
	return ads1018_set_power_state(iio_priv(indio_dev), true);
}

static int ads1018_buffer_postdisable(struct iio_dev *indio_dev)
{
	return ads1018_set_power_state(iio_priv(indio_dev), false);
}

static const struct iio_buffer_setup_ops ti_ads1018_buffer_setup_ops = {
	.preenable	= ads1018_buffer_preenable,
	.postdisable	= ads1018_buffer_postdisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static IIO_CONST_ATTR_NAMED(ads1018_scale_available, scale_available,
	"3 2 1 0.5 0.25 0.125");

static IIO_CONST_ATTR_NAMED(ads1018_sampling_frequency_available,
	sampling_frequency_available, "128 250 490 920 1600 2400 3300");

static struct attribute *ads1018_attributes[] = {
	&iio_const_attr_ads1018_scale_available.dev_attr.attr,
	&iio_const_attr_ads1018_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ads1018_attribute_group = {
	.attrs = ads1018_attributes,
};


static const struct iio_info ti_ads1018_info = {
	.read_raw	= ads1018_read_raw,
	.write_raw	= ads1018_write_raw,
	.attrs          = &ads1018_attribute_group,
};



#ifdef CONFIG_OF
static int ads1018_get_channels_config_of(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ti_ads1018_state *data = iio_priv(indio_dev);
	struct device_node *node;

	if (!spi->dev.of_node ||
	    !of_get_next_child(spi->dev.of_node, NULL))
		return -EINVAL;

	for_each_child_of_node(spi->dev.of_node, node) {
		u32 pval;
		unsigned int channel;
		unsigned int pga = ADS1018_DEFAULT_PGA;
		unsigned int data_rate = ADS1018_DEFAULT_DATA_RATE;

		if (of_property_read_u32(node, "reg", &pval)) {
			dev_err(&spi->dev, "invalid reg on %pOF\n",
				node);
			continue;
		}

		channel = pval;

		if (channel >= ADS1018_CHANNELS) {
			dev_err(&spi->dev,
				"invalid channel index %d on %pOF\n",
				channel, node);
			continue;
		}

		if (!of_property_read_u32(node, "ti,gain", &pval)) {
			pga = pval;
			if (pga > 6) {
				dev_err(&spi->dev, "invalid gain on %pOF\n",
					node);
				of_node_put(node);
				return -EINVAL;
			}
		}

		if (!of_property_read_u32(node, "ti,datarate", &pval)) {
			data_rate = pval;
			if (data_rate > 7) {
				dev_err(&spi->dev,
					"invalid data_rate on %pOF\n",
					node);
				of_node_put(node);
				return -EINVAL;
			}
		}

		data->channel_data[channel].enabled = 1;
		data->channel_data[channel].pga = pga;
		data->channel_data[channel].data_rate = data_rate;

	}

	return 0;
}
#endif

static void ads1018_get_channels_config(struct spi_device *spi)
{
	unsigned int k;

	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ti_ads1018_state *data = iio_priv(indio_dev);
#ifdef CONFIG_OF
	if (!ads1018_get_channels_config_of(spi))
		return;
#endif
	/* fallback on default configuration */
	for (k = 0; k < ADS1018_CHANNELS; ++k) {
		data->channel_data[k].enabled = 0;
		data->channel_data[k].pga = ADS1018_DEFAULT_PGA;
		data->channel_data[k].data_rate = ADS1018_DEFAULT_DATA_RATE;
	}
}

static int ads1018_set_conv_mode(struct ti_ads1018_state *st, int mode)
{
	int updatedConfig;

	updatedConfig = st->current_config & ~(TI_ADS1018_MODE_MASK << TI_ADS1018_MODE_SHIFT);
	updatedConfig |= mode;

	return ti_ads1018_write_config_register(st, updatedConfig);
}


static int ti_ads1018_set_trigger_state(struct iio_trigger *trig, bool enable)
{
	return 0;
}


static const struct iio_trigger_ops ti_ads1018_trigger_ops = {
	.set_trigger_state = ti_ads1018_set_trigger_state,
	.validate_device = iio_trigger_validate_own_device,
};


static int ti_ads1018_probe(struct spi_device *spi)
{
	struct ti_ads1018_state *st;
	struct iio_dev *indio_dev;
	const struct ti_ads1018_chip_info *info;
	int ret;
	int i;
	int config;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->spi = spi;
	config = TI_ADS1018_CONFIGURE;
	spi_set_drvdata(spi, indio_dev);

	mutex_init(&st->lock);


	info = &ti_ads1018_chip_info[TI_ADS1018];

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->modes = INDIO_DIRECT_MODE  |  INDIO_BUFFER_SOFTWARE;
	indio_dev->channels = info->channels;
	indio_dev->num_channels = info->num_channels;
	indio_dev->info = &ti_ads1018_info;
	st->data_rate = (unsigned int *) &ads1018_data_rate;

	// allocate and then register trigger with IIO core
	st->trig = iio_trigger_alloc(indio_dev->dev.parent, "%s-dev%d",
					indio_dev->name,
					iio_device_id(indio_dev));
	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_cleanup_ring;
	}
	st->trig->ops = &ti_ads1018_trigger_ops;

	iio_trigger_register(st->trig);

	// set the default trigger
	indio_dev->trig = iio_trigger_get(st->trig);

	ads1018_get_channels_config(spi);


	/* Find out if DT has enabled any of the channels, only one allowed so
	 * just take the first one
	 */
	for (i = 0; i < info->num_channels; i++)
		if (st->channel_data[i].enabled == 1)
			break;

	/* If a channel was enabled set it by default in the device */
	if (i < info->num_channels) {
		/* mask out the mux bits  */
		config &= ~(TI_ADS1018_MUX_MASK << TI_ADS1018_MUX_SHIFT);
		config |= (i << TI_ADS1018_MUX_SHIFT);
	}

	/*
	 * Always use 4 * 8 bit transfer mode so that the status is read back
	 * in the second 16 bit word. This allows us to track when updated
	 * configuration needs to be applied.
	 */
	st->scan_single_xfer[0].tx_buf = &st->tx_buf[0];
	st->scan_single_xfer[0].rx_buf = &st->rx_buf[0];
	st->scan_single_xfer[0].len = 4;
	st->scan_single_xfer[0].cs_change = 0;
	st->scan_single_xfer[0].bits_per_word = 8;

	spi_message_init_with_transfers(&st->scan_single_msg,
					st->scan_single_xfer, 1);

	ret = ti_ads1018_write_config_register(st, config);
	if (ret < 0) {
		dev_err(&spi->dev, "ads1018 configuration failed\n");
		return ret;
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 ti_ads1018_trigger_handler,
					 &ti_ads1018_buffer_setup_ops);
	if (ret < 0) {
		dev_err(&spi->dev, "iio triggered buffer setup failed\n");
		return ret;
	}


	ret = ads1018_set_conv_mode(st, TI_ADS1018_MODE_CONTINUOUS);
	if (ret)
		return ret;

	st->conv_invalid = true;

	ret = pm_runtime_set_active(&spi->dev);
	if (ret)
		return ret;
	pm_runtime_set_autosuspend_delay(&spi->dev, ADS1018_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&spi->dev);
	pm_runtime_enable(&spi->dev);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&spi->dev, "Failed to register iio device\n");
		goto error_cleanup_ring;
	}

	return 0;

error_cleanup_ring:
	iio_triggered_buffer_cleanup(indio_dev);

	return ret;

}

static int ti_ads1018_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ti_ads1018_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&spi->dev);
	pm_runtime_set_suspended(&spi->dev);
	pm_runtime_put_noidle(&spi->dev);

	/* power down single shot mode */
	return ads1018_set_conv_mode(st, TI_ADS1018_MODE_SINGLE_SHOT);
}



#ifdef CONFIG_PM
static int ads1018_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ti_ads1018_state *st = iio_priv(indio_dev);

	return ads1018_set_conv_mode(st, TI_ADS1018_MODE_SINGLE_SHOT);
}

static int ads1018_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ti_ads1018_state *st = iio_priv(indio_dev);
	int ret;

	ret = ads1018_set_conv_mode(st, TI_ADS1018_MODE_CONTINUOUS);
	if (!ret)
		st->conv_invalid = true;

	return ret;
}
#endif

static const struct dev_pm_ops ads1018_pm_ops = {
	SET_RUNTIME_PM_OPS(ads1018_runtime_suspend,
			   ads1018_runtime_resume, NULL)
};

static const struct spi_device_id ti_ads1018_id[] = {
	{ "ads1018", TI_ADS1018 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ti_ads1018_id);

static const struct of_device_id ads1018_of_table[] = {
	{ .compatible = "ti,ads1018", .data = &ti_ads1018_chip_info[TI_ADS1018] },
	{ },
};
MODULE_DEVICE_TABLE(of, ads1018_of_table);

static struct spi_driver ti_ads1018_driver = {
	.driver = {
		.name = ADS1018_DRV_NAME,
		.of_match_table = ads1018_of_table,
		.pm = &ads1018_pm_ops,
	},
	.probe		= ti_ads1018_probe,
	.remove		= ti_ads1018_remove,
	.id_table	= ti_ads1018_id,
};
module_spi_driver(ti_ads1018_driver);

MODULE_AUTHOR("Iain Hunter <iain@hunterembedded.co.uk>");
MODULE_DESCRIPTION("TI TI_ADS1018 ADC");
MODULE_LICENSE("GPL v2");
