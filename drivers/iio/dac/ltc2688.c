// SPDX-License-Identifier: GPL-2.0
/*
 * LTC2688 16 channel, 16 bit Voltage Output SoftSpan DAC driver
 *
 * Copyright 2021 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/limits.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#define LTC2688_DAC_CHANNELS			16

#define LTC2688_CMD_CH_CODE(x)			(0x00 + (x))
#define LTC2688_CMD_CH_SETTING(x)		(0x10 + (x))
#define LTC2688_CMD_CH_OFFSET(x)		(0X20 + (x))
#define LTC2688_CMD_CH_GAIN(x)			(0x30 + (x))
#define LTC2688_CMD_CH_CODE_UPDATE(x)		(0x40 + (x))

#define LTC2688_CMD_CONFIG			0x70
#define LTC2688_CMD_POWERDOWN			0x71
#define LTC2688_CMD_A_B_SELECT			0x72
#define LTC2688_CMD_SW_TOGGLE			0x73
#define LTC2688_CMD_TOGGLE_DITHER_EN		0x74
#define LTC2688_CMD_THERMAL_STAT		0x77
#define LTC2688_CMD_UPDATE_ALL			0x7C
#define LTC2688_CMD_NOOP			0xFF

#define LTC2688_READ_OPERATION			0x80

/* Channel Settings */
#define LTC2688_CH_SPAN_MSK			GENMASK(2, 0)
#define LTC2688_CH_OVERRANGE_MSK		BIT(3)
#define LTC2688_CH_TD_SEL_MSK			GENMASK(5, 4)
#define LTC2688_CH_DIT_PER_MSK			GENMASK(8, 6)
#define LTC2688_CH_DIT_PH_MSK			GENMASK(10, 9)
#define LTC2688_CH_MODE_MSK			BIT(11)

/* Configuration register */
#define LTC2688_CONFIG_RST			BIT(15)
#define LTC2688_CONFIG_EXT_REF			BIT(1)

#define LTC2688_DITHER_FREQ_AVAIL_N		5

enum {
	LTC2688_SPAN_RANGE_0V_5V,
	LTC2688_SPAN_RANGE_0V_10V,
	LTC2688_SPAN_RANGE_M5V_5V,
	LTC2688_SPAN_RANGE_M10V_10V,
	LTC2688_SPAN_RANGE_M15V_15V,
	LTC2688_SPAN_RANGE_MAX
};

enum {
	LTC2688_MODE_DEFAULT,
	LTC2688_MODE_DITHER_TOGGLE,
};

struct ltc2688_chan {
	long dither_frequency[LTC2688_DITHER_FREQ_AVAIL_N];
	bool overrange;
	bool toggle_chan;
	u8 mode;
};

struct ltc2688_state {
	struct spi_device *spi;
	struct regmap *regmap;
	struct regulator_bulk_data regulators[2];
	struct ltc2688_chan channels[LTC2688_DAC_CHANNELS];
	/* lock to protect against multiple access to the device and shared data */
	struct mutex lock;
	int vref;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u8 tx_data[3] ____cacheline_aligned;
	u8 rx_data[3];
};

static int ltc2688_spi_read(void *context, const void *reg, size_t reg_size,
			    void *val, size_t val_size)
{
	struct ltc2688_state *st = context;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = reg,
			.bits_per_word = 8,
			/*
			 * This means that @val will also be part of the
			 * transfer as there's no pad bits. That's fine as these
			 * bits are don't care for the device and we fill
			 * @val with the proper value afterwards. Using regmap
			 * pad bits to get reg_size right would just make the
			 * write part more cumbersome than this...
			 */
			.len = reg_size + 2,
			.cs_change = 1,
		}, {
			.tx_buf = st->tx_data,
			.rx_buf = st->rx_data,
			.bits_per_word = 8,
			.len = 3,
		},
	};
	int ret;

	ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
	if (ret)
		return ret;

	memcpy(val, &st->rx_data[1], val_size);

	return 0;
}

static int ltc2688_spi_write(void *context, const void *data, size_t count)
{
	struct ltc2688_state *st = context;

	return spi_write(st->spi, data, count);
}

static int ltc2688_span_get(const struct ltc2688_state *st, int c)
{
	int ret, reg, span;

	ret = regmap_read(st->regmap, LTC2688_CMD_CH_SETTING(c), &reg);
	if (ret)
		return ret;

	span = FIELD_GET(LTC2688_CH_SPAN_MSK, reg);
	/* sanity check to make sure we don't get any weird value from the HW */
	if (span >= LTC2688_SPAN_RANGE_MAX)
		return -EIO;

	return span;
}

static const int ltc2688_span_helper[LTC2688_SPAN_RANGE_MAX][2] = {
	{0, 5000}, {0, 10000}, {-5000, 5000}, {-10000, 10000}, {-15000, 15000},
};

static int ltc2688_scale_get(const struct ltc2688_state *st, int c, int *val)
{
	const struct ltc2688_chan *chan = &st->channels[c];
	int span, fs;

	span = ltc2688_span_get(st, c);
	if (span < 0)
		return span;

	fs = ltc2688_span_helper[span][1] - ltc2688_span_helper[span][0];
	if (chan->overrange)
		fs = mult_frac(fs, 105, 100);

	*val = DIV_ROUND_CLOSEST(fs * st->vref, 4096);

	return IIO_VAL_FRACTIONAL_LOG2;
}

static int ltc2688_offset_get(const struct ltc2688_state *st, int c, int *val)
{
	int span;

	span = ltc2688_span_get(st, c);
	if (span < 0)
		return span;

	if (ltc2688_span_helper[span][0] < 0)
		*val = -32768;
	else
		*val = 0;

	return IIO_VAL_INT;
}

enum {
	LT2688_INPUT_A,
	LT2688_INPUT_B,
};

static int ltc2688_dac_code_write(struct ltc2688_state *st, u32 chan, u32 input,
				  u16 code)
{
	struct ltc2688_chan *c = &st->channels[chan];
	int ret, reg;

	/* 2 LSBs set to 0 if writing dither amplitude */
	if (!c->toggle_chan && input == LT2688_INPUT_B) {
		if (code > GENMASK(13, 0))
			return -EINVAL;

		code <<= 2;
	}

	mutex_lock(&st->lock);
	/* select the correct input register to read from */
	ret = regmap_update_bits(st->regmap, LTC2688_CMD_A_B_SELECT, BIT(chan),
				 input << chan);
	if (ret)
		goto unlock;

	/*
	 * If in dither/toggle mode the dac should be updated by an
	 * external signal (or sw toggle) and not here.
	 */
	if (c->mode == LTC2688_MODE_DEFAULT)
		reg = LTC2688_CMD_CH_CODE_UPDATE(chan);
	else
		reg = LTC2688_CMD_CH_CODE(chan);

	ret = regmap_write(st->regmap, reg, code);
unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int ltc2688_dac_code_read(struct ltc2688_state *st, u32 chan, u32 input,
				 u32 *code)
{
	struct ltc2688_chan *c = &st->channels[chan];
	int ret;

	mutex_lock(&st->lock);
	ret = regmap_update_bits(st->regmap, LTC2688_CMD_A_B_SELECT, BIT(chan),
				 input << chan);
	if (ret)
		goto unlock;

	ret = regmap_read(st->regmap, LTC2688_CMD_CH_CODE(chan), code);
unlock:
	mutex_unlock(&st->lock);

	if (!c->toggle_chan && input == LT2688_INPUT_B)
		*code >>= 2;

	return ret;
}

static const int ltc2688_raw_range[] = {0, 1, U16_MAX};

static int ltc2688_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long m)
{
	switch (m) {
	case IIO_CHAN_INFO_RAW:
		*vals = ltc2688_raw_range;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static int ltc2688_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long m)
{
	struct ltc2688_state *st = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ltc2688_dac_code_read(st, chan->channel, LT2688_INPUT_A,
					    val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		return ltc2688_offset_get(st, chan->channel, val);
	case IIO_CHAN_INFO_SCALE:
		*val2 = 16;
		return ltc2688_scale_get(st, chan->channel, val);
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = regmap_read(st->regmap,
				  LTC2688_CMD_CH_OFFSET(chan->channel), val);
		if (ret)
			return ret;

		/* Just 13 bits used. 2LSB ignored */
		*val >>= 2;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		ret = regmap_read(st->regmap,
				  LTC2688_CMD_CH_GAIN(chan->channel), val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ltc2688_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct ltc2688_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val > U16_MAX || val < 0)
			return -EINVAL;

		return ltc2688_dac_code_write(st, chan->channel, LT2688_INPUT_A,
					      val);
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val > GENMASK(13, 0))
			return -EINVAL;

		return regmap_write(st->regmap,
				    LTC2688_CMD_CH_OFFSET(chan->channel),
				    val << 2);
	case IIO_CHAN_INFO_CALIBSCALE:
		return regmap_write(st->regmap,
				    LTC2688_CMD_CH_GAIN(chan->channel), val);
	default:
		return -EINVAL;
	}
}

enum {
	LTC2688_DITHER_TOGGLE_ENABLE,
	LTC2688_POWERDOWN,
	LTC2688_INPUT_B,
	LTC2688_INPUT_B_AVAIL,
	LTC2688_SW_TOGGLE,
	LTC2688_DITHER_FREQ,
	LTC2688_DITHER_FREQ_AVAIL,
};

static int ltc2688_dither_toggle_set(struct ltc2688_state *st, u32 chan, bool en)
{
	struct ltc2688_chan *c = &st->channels[chan];
	int ret;

	mutex_lock(&st->lock);
	ret = regmap_update_bits(st->regmap, LTC2688_CMD_TOGGLE_DITHER_EN,
				 BIT(chan), en << chan);
	if (ret)
		goto unlock;

	c->mode = en ? LTC2688_MODE_DITHER_TOGGLE : LTC2688_MODE_DEFAULT;
unlock:
	mutex_unlock(&st->lock);

	return ret;
}

static ssize_t ltc2688_reg_bool_get(const struct ltc2688_state *st, u32 chan,
				    u32 reg, char *buf)
{
	int ret;
	u32 val;

	ret = regmap_read(st->regmap, reg, &val);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", !!(val & BIT(chan)));
}

static ssize_t ltc2688_reg_bool_set(const struct ltc2688_state *st, u32 chan,
				    u32 reg, const char *buf)
{
	int ret;
	bool en;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;

	return regmap_update_bits(st->regmap, reg, BIT(chan), en << chan);
}

static ssize_t ltc2688_dither_freq_avail(const struct ltc2688_state *st,
					 u32 chan, char *buf)
{
	const struct ltc2688_chan *c = &st->channels[chan];
	int sz = 0;
	u32 f;

	for (f = 0; f < ARRAY_SIZE(c->dither_frequency); f++)
		sz += sysfs_emit_at(buf, sz, "%ld ", c->dither_frequency[f]);

	buf[sz - 1] = '\n';

	return sz;
}

static ssize_t ltc2688_dither_freq_get(const struct ltc2688_state *st, u32 chan,
				       char *buf)
{
	const struct ltc2688_chan *c = &st->channels[chan];
	u32 reg, freq;
	int ret;

	ret = regmap_read(st->regmap, LTC2688_CMD_CH_SETTING(chan), &reg);
	if (ret)
		return ret;

	freq = FIELD_GET(LTC2688_CH_DIT_PER_MSK, reg);
	if (freq >= ARRAY_SIZE(c->dither_frequency))
		return -EIO;

	return sysfs_emit(buf, "%ld\n", c->dither_frequency[freq]);
}

static int ltc2688_dither_freq_set(const struct ltc2688_state *st, u32 chan,
				   const char *buf)
{
	const struct ltc2688_chan *c = &st->channels[chan];
	long val;
	u32 freq;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	for (freq = 0; freq < ARRAY_SIZE(c->dither_frequency); freq++) {
		if (val == c->dither_frequency[freq])
			break;
	}

	if (freq == ARRAY_SIZE(c->dither_frequency))
		return -EINVAL;

	return regmap_update_bits(st->regmap,
				  LTC2688_CMD_CH_SETTING(chan),
				  LTC2688_CH_DIT_PER_MSK,
				  FIELD_PREP(LTC2688_CH_DIT_PER_MSK, freq));
}

static ssize_t ltc2688_read_ext(struct iio_dev *indio_dev,
				uintptr_t private,
				const struct iio_chan_spec *chan,
				char *buf)
{
	struct ltc2688_state *st = iio_priv(indio_dev);
	u32 val;
	int ret;

	switch (private) {
	case LTC2688_DITHER_TOGGLE_ENABLE:
		return ltc2688_reg_bool_get(st, chan->channel,
					    LTC2688_CMD_TOGGLE_DITHER_EN, buf);
	case LTC2688_POWERDOWN:
		return ltc2688_reg_bool_get(st, chan->channel,
					    LTC2688_CMD_POWERDOWN, buf);
	case LTC2688_INPUT_B:
		ret = ltc2688_dac_code_read(st, chan->channel, LT2688_INPUT_B,
					    &val);
		if (ret)
			return ret;

		return sysfs_emit(buf, "%u\n", val);
	case LTC2688_INPUT_B_AVAIL:
		/* dither amplitude has 1/4 of the span */
		return sysfs_emit(buf, "[%u %u %u]\n", ltc2688_raw_range[0],
				  ltc2688_raw_range[1],
				  ltc2688_raw_range[2] / 4);
	case LTC2688_SW_TOGGLE:
		return ltc2688_reg_bool_get(st, chan->channel,
					    LTC2688_CMD_SW_TOGGLE, buf);
	case LTC2688_DITHER_FREQ:
		return ltc2688_dither_freq_get(st, chan->channel, buf);
	case LTC2688_DITHER_FREQ_AVAIL:
		return ltc2688_dither_freq_avail(st, chan->channel, buf);
	default:
		return -EINVAL;
	}
}

static ssize_t ltc2688_write_ext(struct iio_dev *indio_dev,
				 uintptr_t private,
				 const struct iio_chan_spec *chan,
				 const char *buf, size_t len)
{
	struct ltc2688_state *st = iio_priv(indio_dev);
	u16 val;
	int ret;
	bool en;

	switch (private) {
	case LTC2688_DITHER_TOGGLE_ENABLE:
		ret = kstrtobool(buf, &en);
		if (ret)
			return ret;

		ret = ltc2688_dither_toggle_set(st, chan->channel, en);
		if (ret)
			return ret;

		return len;
	case LTC2688_POWERDOWN:
		ret = ltc2688_reg_bool_set(st, chan->channel,
					   LTC2688_CMD_POWERDOWN, buf);
		if (ret)
			return ret;

		return len;
	case LTC2688_INPUT_B:
		ret = kstrtou16(buf, 10, &val);
		if (ret)
			return ret;

		ret = ltc2688_dac_code_write(st, chan->channel, LT2688_INPUT_B,
					     val);
		if (ret)
			return ret;

		return len;
	case LTC2688_SW_TOGGLE:
		ret = ltc2688_reg_bool_set(st, chan->channel,
					   LTC2688_CMD_SW_TOGGLE, buf);
		if (ret)
			return ret;

		return len;
	case LTC2688_DITHER_FREQ:
		ret = ltc2688_dither_freq_set(st, chan->channel, buf);
		if (ret)
			return ret;

		return len;
	default:
		return -EINVAL;
	}
}

static int ltc2688_get_dither_phase(struct iio_dev *dev,
				    const struct iio_chan_spec *chan)
{
	struct ltc2688_state *st = iio_priv(dev);
	int ret, regval;

	ret = regmap_read(st->regmap, LTC2688_CMD_CH_SETTING(chan->channel),
			  &regval);
	if (ret)
		return ret;

	return FIELD_GET(LTC2688_CH_DIT_PH_MSK, regval);
}

static int ltc2688_set_dither_phase(struct iio_dev *dev,
				    const struct iio_chan_spec *chan,
				    unsigned int phase)
{
	struct ltc2688_state *st = iio_priv(dev);

	return regmap_update_bits(st->regmap,
				  LTC2688_CMD_CH_SETTING(chan->channel),
				  LTC2688_CH_DIT_PH_MSK,
				  FIELD_PREP(LTC2688_CH_DIT_PH_MSK, phase));
}

static int ltc2688_reg_access(struct iio_dev *indio_dev,
			      unsigned int reg,
			      unsigned int writeval,
			      unsigned int *readval)
{
	struct ltc2688_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);
	else
		return regmap_write(st->regmap, reg, writeval);
}

static const char * const ltc2688_dither_phase[] = {
	"0", "90", "180", "270",
};

static const struct iio_enum ltc2688_dither_phase_enum = {
	.items = ltc2688_dither_phase,
	.num_items = ARRAY_SIZE(ltc2688_dither_phase),
	.set = ltc2688_set_dither_phase,
	.get = ltc2688_get_dither_phase,
};

#define LTC2688_CHAN_EXT_INFO(_name, _what, _shared) {	\
	.name = _name,					\
	.read = ltc2688_read_ext,			\
	.write = ltc2688_write_ext,			\
	.private = (_what),				\
	.shared = (_shared),				\
}

/*
 * For toggle mode we only expose the symbol attr (sw_toggle) in case a TGPx is
 * not provided in dts.
 */
#define LTC2688_CHAN_TOGGLE(t, name) ({							\
	static const struct iio_chan_spec_ext_info t ## _ext_info[] = {			\
		LTC2688_CHAN_EXT_INFO("raw1", LTC2688_INPUT_B, IIO_SEPARATE),		\
		LTC2688_CHAN_EXT_INFO("toggle_en", LTC2688_DITHER_TOGGLE_ENABLE,	\
				      IIO_SEPARATE),					\
		LTC2688_CHAN_EXT_INFO("powerdown", LTC2688_POWERDOWN, IIO_SEPARATE),	\
		LTC2688_CHAN_EXT_INFO(name, LTC2688_SW_TOGGLE, IIO_SEPARATE),		\
		{}									\
	};										\
	t ## _ext_info;									\
})

static struct iio_chan_spec_ext_info ltc2688_dither_ext_info[] = {
	LTC2688_CHAN_EXT_INFO("dither_raw", LTC2688_INPUT_B, IIO_SEPARATE),
	LTC2688_CHAN_EXT_INFO("dither_raw_available", LTC2688_INPUT_B_AVAIL,
			      IIO_SEPARATE),
	/*
	 * Not IIO_ENUM because the available freq needs to be computed at
	 * probe. We could still use it, but it didn't felt much right.
	 *
	 */
	LTC2688_CHAN_EXT_INFO("dither_frequency", LTC2688_DITHER_FREQ,
			      IIO_SEPARATE),
	LTC2688_CHAN_EXT_INFO("dither_frequency_available",
			      LTC2688_DITHER_FREQ_AVAIL, IIO_SEPARATE),
	IIO_ENUM("dither_phase", IIO_SEPARATE, &ltc2688_dither_phase_enum),
	IIO_ENUM_AVAILABLE("dither_phase", IIO_SEPARATE,
			   &ltc2688_dither_phase_enum),
	LTC2688_CHAN_EXT_INFO("dither_en", LTC2688_DITHER_TOGGLE_ENABLE,
			      IIO_SEPARATE),
	LTC2688_CHAN_EXT_INFO("powerdown", LTC2688_POWERDOWN, IIO_SEPARATE),
	{}
};

static const struct iio_chan_spec_ext_info ltc2688_ext_info[] = {
	LTC2688_CHAN_EXT_INFO("powerdown", LTC2688_POWERDOWN, IIO_SEPARATE),
	{}
};

#define LTC2688_CHANNEL(_chan) {					\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.output = 1,							\
	.channel = (_chan),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_CALIBSCALE) |		\
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_OFFSET) |	\
		BIT(IIO_CHAN_INFO_CALIBBIAS) | BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_RAW),		\
	.ext_info = ltc2688_ext_info					\
}

struct iio_chan_spec ltc2688_channels[] = {
	LTC2688_CHANNEL(0),
	LTC2688_CHANNEL(1),
	LTC2688_CHANNEL(2),
	LTC2688_CHANNEL(3),
	LTC2688_CHANNEL(4),
	LTC2688_CHANNEL(5),
	LTC2688_CHANNEL(6),
	LTC2688_CHANNEL(7),
	LTC2688_CHANNEL(8),
	LTC2688_CHANNEL(9),
	LTC2688_CHANNEL(10),
	LTC2688_CHANNEL(11),
	LTC2688_CHANNEL(12),
	LTC2688_CHANNEL(13),
	LTC2688_CHANNEL(14),
	LTC2688_CHANNEL(15),
};

enum {
	LTC2688_CHAN_TD_TGP1,
	LTC2688_CHAN_TD_TGP2,
	LTC2688_CHAN_TD_TGP3,
	LTC2688_CHAN_TD_MAX
};

static void ltc2688_clk_disable(void *clk)
{
	clk_disable_unprepare(clk);
}

/* Helper struct to deal with dither channels binded to TGPx pins */
struct ltc2688_dither_helper {
	u8 chan[LTC2688_DAC_CHANNELS];
	u8 n_chans;
};

static const char * const ltc2688_clk_names[LTC2688_CHAN_TD_MAX] = {
	"TGP1", "TGP2", "TGP3",
};

static const int ltc2688_period[LTC2688_DITHER_FREQ_AVAIL] = {
	4, 8, 16, 32, 64,
};

static void ltc2688_dither_compute_freq_avail(struct ltc2688_state *st,
					      const struct ltc2688_dither_helper *tgp,
					      unsigned long rate)
{
	u32 e;

	for (e = 0; e < tgp->n_chans; e++) {
		int c = tgp->chan[e];
		struct ltc2688_chan *chan = &st->channels[c];
		u32 f;

		for (f = 0; f < ARRAY_SIZE(chan->dither_frequency); f++)
			chan->dither_frequency[f] = DIV_ROUND_CLOSEST(rate, ltc2688_period[f]);
	}
}

static int ltc2688_tgp_setup(struct ltc2688_state *st, long clk_mask,
			     const struct ltc2688_dither_helper *tgp)
{
	int ret, bit;

	for_each_set_bit(bit, &clk_mask, LTC2688_CHAN_TD_MAX) {
		struct clk *clk;
		unsigned long rate;

		clk = devm_clk_get(&st->spi->dev, ltc2688_clk_names[bit]);
		if (IS_ERR(clk))
			return dev_err_probe(&st->spi->dev, PTR_ERR(clk),
					     "failed to get clk: %s\n",
					     ltc2688_clk_names[bit]);

		ret = clk_prepare_enable(clk);
		if (ret)
			return dev_err_probe(&st->spi->dev, ret,
					     "failed to enable clk: %s\n",
					     ltc2688_clk_names[bit]);

		ret = devm_add_action_or_reset(&st->spi->dev,
					       ltc2688_clk_disable, clk);
		if (ret)
			return ret;

		/* this will only be non zero for dither channels */
		if (!tgp[bit].n_chans)
			continue;

		rate = clk_get_rate(clk);
		ltc2688_dither_compute_freq_avail(st, &tgp[bit], rate);
	}

	return 0;
}

static int ltc2688_span_lookup(const struct ltc2688_state *st, int min, int max)
{
	u32 span;

	for (span = 0; span < ARRAY_SIZE(ltc2688_span_helper); span++) {
		if (min == ltc2688_span_helper[span][0] &&
		    max == ltc2688_span_helper[span][1])
			return span;
	}

	return -EINVAL;
}

static int ltc2688_channel_config(struct ltc2688_state *st)
{
	struct fwnode_handle *fwnode = dev_fwnode(&st->spi->dev), *child;
	struct ltc2688_dither_helper tgp[LTC2688_CHAN_TD_MAX] = {0};
	u32 reg, clk_input, val, mask, tmp[2];
	unsigned long clk_msk = 0;
	int ret, span;

	fwnode_for_each_available_child_node(fwnode, child) {
		struct ltc2688_chan *chan;

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret) {
			fwnode_handle_put(child);
			return dev_err_probe(&st->spi->dev, ret,
					     "Failed to get reg property\n");
		}

		if (reg >= LTC2688_DAC_CHANNELS) {
			fwnode_handle_put(child);
			return dev_err_probe(&st->spi->dev, -EINVAL,
					     "reg bigger than: %d\n",
					     LTC2688_DAC_CHANNELS);
		}

		chan = &st->channels[reg];
		if (fwnode_property_read_bool(child, "adi,toggle-mode")) {
			chan->toggle_chan = true;
			/* assume sw toggle ABI */
			ltc2688_channels[reg].ext_info = LTC2688_CHAN_TOGGLE(__s, "symbol");
		}

		ret = fwnode_property_read_u32_array(child, "adi,output-range-millivolt",
						     tmp, ARRAY_SIZE(tmp));
		if (!ret) {
			span = ltc2688_span_lookup(st, tmp[0], tmp[1]);
			if (span < 0)
				return dev_err_probe(&st->spi->dev, -EINVAL,
						     "output range not valid:[%d %d]\n",
						     tmp[0], tmp[1]);

			mask |= LTC2688_CH_SPAN_MSK;
			val |= FIELD_PREP(LTC2688_CH_SPAN_MSK, span);
		}

		ret = fwnode_property_read_u32(child, "adi,toggle-dither-input",
					       &clk_input);
		if (!ret) {
			int cur_chan = tgp[clk_input].n_chans;

			if (clk_input > LTC2688_CHAN_TD_TGP3) {
				fwnode_handle_put(child);
				return dev_err_probe(&st->spi->dev, -EINVAL,
						     "toggle-dither-input inv value(%d)\n",
						     clk_input);
			}

			mask |= LTC2688_CH_TD_SEL_MSK;
			/*
			 * 0 means software toggle which is the default mode.
			 * Hence the +1.
			 */
			val |= FIELD_PREP(LTC2688_CH_TD_SEL_MSK, clk_input + 1);
			clk_msk |= BIT(clk_input);
			/*
			 * If a TGPx is given, we automatically assume a dither
			 * capable channel (unless toggle is already enabled).
			 * Hence, we prepar our TGPx <-> channel map to make it
			 * easier to handle the clocks and available frequency
			 * calculations... On top of this we just set here the
			 * dither bit in the channel settings. It won't have any
			 * effect until the global toggle/dither bit is enabled.
			 */
			if (!chan->toggle_chan) {
				tgp[clk_input].chan[cur_chan] = reg;
				tgp[clk_input].n_chans++;
				mask |= LTC2688_CH_MODE_MSK;
				val |= FIELD_PREP(LTC2688_CH_MODE_MSK, 1);
				ltc2688_channels[reg].ext_info = ltc2688_dither_ext_info;
			} else {
				/* wait, no sw toggle after all */
				ltc2688_channels[reg].ext_info = LTC2688_CHAN_TOGGLE(__no_s, NULL);
			}
		}

		if (fwnode_property_read_bool(child, "adi,overrange")) {
			chan->overrange = true;
			val |= LTC2688_CH_OVERRANGE_MSK;
			mask |= BIT(3);
		}

		if (!mask)
			continue;

		ret = regmap_update_bits(st->regmap,
					 LTC2688_CMD_CH_SETTING(reg), mask,
					 val);
		if (ret) {
			fwnode_handle_put(child);
			return dev_err_probe(&st->spi->dev, -EINVAL,
					     "failed to set chan settings\n");
		}

		mask = 0;
		val = 0;
	}

	return ltc2688_tgp_setup(st, clk_msk, tgp);
}

static int ltc2688_setup(struct ltc2688_state *st, struct regulator *vref)
{
	struct gpio_desc *gpio;
	int ret;

	/*
	 * If we have a reset pin, use that to reset the board, If not, use
	 * the reset bit.
	 */
	gpio = devm_gpiod_get_optional(&st->spi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return dev_err_probe(&st->spi->dev, PTR_ERR(gpio),
				     "Failed to get reset gpio");
	if (gpio) {
		usleep_range(1000, 1200);
		/* bring device out of reset */
		gpiod_set_value_cansleep(gpio, 0);
	} else {
		ret = regmap_update_bits(st->regmap, LTC2688_CMD_CONFIG,
					 LTC2688_CONFIG_RST,
					 LTC2688_CONFIG_RST);
		if (ret < 0)
			return ret;
	}

	usleep_range(10000, 12000);

	ret = ltc2688_channel_config(st);
	if (ret)
		return ret;

	if (!vref)
		return 0;

	return regmap_update_bits(st->regmap, LTC2688_CMD_CONFIG,
				  LTC2688_CONFIG_EXT_REF, BIT(1));
}

static void ltc2688_bulk_disable(void *data)
{
	struct ltc2688_state *st = data;

	regulator_bulk_disable(ARRAY_SIZE(st->regulators), st->regulators);
}

static void ltc2688_disable_regulator(void *regulator)
{
	regulator_disable(regulator);
}

static bool ltc2688_reg_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTC2688_CMD_CH_CODE(0) ... LTC2688_CMD_CH_GAIN(15):
		return true;
	case LTC2688_CMD_CONFIG ... LTC2688_CMD_THERMAL_STAT:
		return true;
	default:
		return false;
	}
}

static bool ltc2688_reg_writable(struct device *dev, unsigned int reg)
{
	if (reg <= LTC2688_CMD_UPDATE_ALL && reg != LTC2688_CMD_THERMAL_STAT)
		return true;

	return false;
}

struct regmap_bus ltc2688_regmap_bus = {
	.read = ltc2688_spi_read,
	.write = ltc2688_spi_write,
	.read_flag_mask = LTC2688_READ_OPERATION,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG
};

static const struct regmap_config ltc2688_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.readable_reg = ltc2688_reg_readable,
	.writeable_reg = ltc2688_reg_writable,
	/* ignoring the no op command */
	.max_register = LTC2688_CMD_UPDATE_ALL
};

static const struct iio_info ltc2688_info = {
	.write_raw = ltc2688_write_raw,
	.read_raw = ltc2688_read_raw,
	.read_avail = ltc2688_read_avail,
	.debugfs_reg_access = ltc2688_reg_access,
};

static int ltc2688_probe(struct spi_device *spi)
{
	struct ltc2688_state *st;
	struct iio_dev *indio_dev;
	struct regulator *vref_reg;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;
	/* Just this once. No need to di it in every regmap read */
	st->tx_data[0] = LTC2688_CMD_NOOP;
	mutex_init(&st->lock);

	st->regmap = devm_regmap_init(&spi->dev, &ltc2688_regmap_bus, st,
				      &ltc2688_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(st->regmap),
				     "Failed to init regmap");

	st->regulators[0].supply = "vcc";
	st->regulators[1].supply = "iovcc";
	ret = devm_regulator_bulk_get(&spi->dev, ARRAY_SIZE(st->regulators),
				      st->regulators);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "Failed to get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(st->regulators), st->regulators);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to enable regulators\n");

	ret = devm_add_action_or_reset(&spi->dev, ltc2688_bulk_disable, st);
	if (ret)
		return ret;

	vref_reg = devm_regulator_get_optional(&spi->dev, "vref");
	if (!IS_ERR(vref_reg)) {
		ret = regulator_enable(vref_reg);
		if (ret)
			return dev_err_probe(&spi->dev, ret,
					     "Failed to enable vref regulators\n");

		ret = devm_add_action_or_reset(&spi->dev,
					       ltc2688_disable_regulator,
					       vref_reg);
		if (ret)
			return ret;

		ret = regulator_get_voltage(vref_reg);
		if (ret < 0)
			return dev_err_probe(&spi->dev, ret,
					     "Failed to get vref\n");

		st->vref = ret / 1000;
	} else {
		if (PTR_ERR(vref_reg) != -ENODEV)
			return dev_err_probe(&spi->dev, PTR_ERR(vref_reg),
					     "Failed to get vref regulator");

		vref_reg = NULL;
		/* internal reference */
		st->vref = 4096;
	}

	indio_dev->name = "ltc2688";
	indio_dev->info = &ltc2688_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc2688_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltc2688_channels);

	ret = ltc2688_setup(st, vref_reg);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&st->spi->dev, indio_dev);
}

static const struct of_device_id ltc2688_of_id[] = {
	{ .compatible = "adi,ltc2688" },
	{}
};
MODULE_DEVICE_TABLE(of, ltc2688_of_id);

static const struct spi_device_id ltc2688_id[] = {
	{ "ltc2688" },
	{}
};
MODULE_DEVICE_TABLE(spi, ltc2688_id);

static struct spi_driver ltc2688_driver = {
	.driver = {
		.name = "ltc2688",
		.of_match_table = ltc2688_of_id,
	},
	.probe = ltc2688_probe,
	.id_table = ltc2688_id,
};
module_spi_driver(ltc2688_driver);

MODULE_AUTHOR("Nuno Sá <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Analog Devices LTC2688 DAC");
MODULE_LICENSE("GPL");
