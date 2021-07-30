// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Sieć Badawcza Łukasiewicz - Przemysłowy Instytut Automatyki i Pomiarów PIAP
 * Written by Krzysztof Hałasa
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/gpio/consumer.h>
#include <linux/math64.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

// External clock (extclk) frequencies
#define AR0521_EXTCLK_RATE	  (27 * 1000 * 1000)
#define AR0521_EXTCLK_MIN	  (10 * 1000 * 1000)
#define AR0521_EXTCLK_MAX	  (48 * 1000 * 1000)

// PLL and PLL2
#define AR0521_PLL_MIN		 (320 * 1000 * 1000)
#define AR0521_PLL_MAX		(1280 * 1000 * 1000)

// effective pixel clocks, the registers may be DDR
#define AR0521_PIXEL_CLOCK_MIN	 (168 * 1000 * 1000)
#define AR0521_PIXEL_CLOCK_MAX	 (414 * 1000 * 1000)

#define AR0521_WIDTH_MIN	       8u
#define AR0521_WIDTH_MAX	    2608u
#define AR0521_HEIGHT_MIN	       8u
#define AR0521_HEIGHT_MAX	    1958u

#define AR0521_WIDTH_BLANKING_MIN     572u
#define AR0521_HEIGHT_BLANKING_MIN     28u // must be even
#define AR0521_TOTAL_WIDTH_MIN	     2968u

// AR0521 registers
#define AR0521_REG_VT_PIX_CLK_DIV		0x0300
#define AR0521_REG_FRAME_LENGTH_LINES		0x0340

#define AR0521_REG_CHIP_ID			0x3000
#define AR0521_REG_COARSE_INTEGRATION_TIME	0x3012
#define AR0521_REG_ROW_SPEED			0x3016
#define AR0521_REG_EXTRA_DELAY			0x3018
#define AR0521_REG_RESET			0x301A
#define   AR0521_REG_RESET_DEFAULTS		  0x0238
#define   AR0521_REG_RESET_GROUP_PARAM_HOLD	  0x8000
#define   AR0521_REG_RESET_STREAM		  BIT(2)
#define   AR0521_REG_RESET_RESTART		  BIT(1)
#define   AR0521_REG_RESET_INIT			  BIT(0)

#define AR0521_REG_GREEN1_GAIN			0x3056
#define AR0521_REG_BLUE_GAIN			0x3058
#define AR0521_REG_RED_GAIN			0x305A
#define AR0521_REG_GREEN2_GAIN			0x305C
#define AR0521_REG_GLOBAL_GAIN			0x305E

#define AR0521_REG_HISPI_TEST_MODE		0x3066
#define AR0521_REG_HISPI_TEST_MODE_LP11		  0x0004

#define AR0521_REG_TEST_PATTERN_MODE		0x3070

#define AR0521_REG_SERIAL_FORMAT		0x31AE
#define AR0521_REG_SERIAL_FORMAT_MIPI		  0x0200

#define AR0521_REG_HISPI_CONTROL_STATUS		0x31C6
#define AR0521_REG_HISPI_CONTROL_STATUS_FRAMER_TEST_MODE_ENABLE 0x80

#define be		cpu_to_be16

// regulator supplies
static const char * const ar0521_supply_names[] = {
	"vdd_io",	// I/O (1.8V) supply
	"vdd",		// Core, PLL and MIPI (1.2V) supply
	"vaa",		// Analog (2.7V) supply
};

#define AR0521_NUM_SUPPLIES ARRAY_SIZE(ar0521_supply_names)

struct ar0521_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain, *red_balance, *blue_balance;
	struct v4l2_ctrl *test_pattern;
	struct v4l2_ctrl *hblank, *vblank, *pixrate;
};

struct ar0521_dev {
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_fwnode_endpoint ep;
	struct clk *extclk;
	u32 extclk_freq;

	struct regulator *supplies[AR0521_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;

	// lock to protect all members below
	struct mutex lock;

	struct v4l2_mbus_framefmt fmt;
	struct v4l2_fract frame_interval, current_frame_interval;
	struct ar0521_ctrls ctrls;
	u32 pix_clk;
	unsigned int power_count;
	u16 total_width, total_height, pll_pre, pll_mult, pll_pre2, pll_mult2, extra_delay;
	bool streaming;
};

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level 0-3");

static inline struct ar0521_dev *to_ar0521_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar0521_dev, sd);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ar0521_dev, ctrls.handler)->sd;
}

static inline unsigned int lanes(struct ar0521_dev *sensor)
{
	return sensor->ep.bus.mipi_csi2.num_data_lanes;
}

static const char *mhz(u32 value)
{
	static char buff[32];

	if (value % 1000)
		sprintf(buff, "%u.%06u", value / (1000 * 1000), value % (1000 * 1000));
	else if (value % (1000 * 1000))
		sprintf(buff, "%u.%03u", value / (1000 * 1000), (value / 1000) % 1000);
	else
		sprintf(buff, "%u", value / (1000 * 1000));
	return buff;
}

static u32 div64_round(u64 v, u32 d)
{
	return div_u64(v + (d >> 1), d);
}

static u32 div64_round_up(u64 v, u32 d)
{
	return div_u64(v + d - 1, d);
}

#define HEX_DUMP_WORDS 8
static void dump_i2c(struct ar0521_dev *sensor, const __be16 *data, unsigned int count)
{
	char buffer[5 /* " XXXX" */ * HEX_DUMP_WORDS + 1 /* NUL */];
	u16 addr;
	int i;

	addr = be16_to_cpu(*data++);
	count--;
	while (count) {
		for (i = 0; i < HEX_DUMP_WORDS; i++) {
			sprintf(buffer + 5 * i, " %04X", be16_to_cpu(*data++));
			addr += 2;
			if (!--count)
				break;
		}
		v4l2_dbg(3, debug, &sensor->sd, "WR %04X:%s\n", addr, buffer);
	}
}

// data must be BE16, the first value is the register address
static int ar0521_write_regs(struct ar0521_dev *sensor, const __be16 *data, unsigned int count)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msg;
	int ret;

	if (!sensor->power_count)
		return 0;

	if (debug >= 3)
		dump_i2c(sensor, data, count);

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = (u8 *)data;
	msg.len = count * sizeof(*data);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		v4l2_err(&sensor->sd, "%s: I2C write error\n", __func__);
		return ret;
	}

	return 0;
}

static int ar0521_write_reg(struct ar0521_dev *sensor, u16 reg, u16 val)
{
	__be16 buf[2] = {be(reg), be(val)};

	return ar0521_write_regs(sensor, buf, 2);
}

static int ar0521_set_geometry(struct ar0521_dev *sensor)
{
	// all dimensions are unsigned 12-bit integers
	u16 x = (AR0521_WIDTH_MAX - sensor->fmt.width) / 2;
	u16 y = ((AR0521_HEIGHT_MAX - sensor->fmt.height) / 2) & ~1;
	__be16 regs[] = {
		be(AR0521_REG_FRAME_LENGTH_LINES),
		be(sensor->total_height),
		be(sensor->total_width),
		be(x),
		be(y),
		be(x + sensor->fmt.width - 1),
		be(y + sensor->fmt.height - 1),
		be(sensor->fmt.width),
		be(sensor->fmt.height)
	};

	v4l2_dbg(2, debug, &sensor->sd, "%s()\n", __func__);

	return ar0521_write_regs(sensor, regs, ARRAY_SIZE(regs));
}

static int ar0521_set_gains(struct ar0521_dev *sensor)
{
	int green = sensor->ctrls.gain->val;
	int red = max(green + sensor->ctrls.red_balance->val, 0);
	int blue = max(green + sensor->ctrls.blue_balance->val, 0);
	unsigned int gain = min(red, min(green, blue));
	unsigned int analog = min(gain, 64u); // range is 0 - 127
	__be16 regs[5];

	v4l2_dbg(2, debug, &sensor->sd, "%s()\n", __func__);

	red   = min(red   - analog + 64, 511u);
	green = min(green - analog + 64, 511u);
	blue  = min(blue  - analog + 64, 511u);
	regs[0] = be(AR0521_REG_GREEN1_GAIN);
	regs[1] = be(green << 7 | analog);
	regs[2] = be(blue  << 7 | analog);
	regs[3] = be(red   << 7 | analog);
	regs[4] = be(green << 7 | analog);

	return ar0521_write_regs(sensor, regs, ARRAY_SIZE(regs));
}

static int ar0521_write_mode(struct ar0521_dev *sensor)
{
	__be16 pll_regs[] = {
		be(AR0521_REG_VT_PIX_CLK_DIV),
		/* 0x300 */ be(4), // vt_pix_clk_div = number of bits / 2
		/* 0x302 */ be(1), // vt_sys_clk_div
		/* 0x304 */ be((sensor->pll_pre2 << 8) | sensor->pll_pre),
		/* 0x306 */ be((sensor->pll_mult2 << 8) | sensor->pll_mult),
		/* 0x308 */ be(8), // op_pix_clk_div = 2 * vt_pix_clk_div
		/* 0x30A */ be(1)  // op_sys_clk_div
	};
	u32 num = sensor->current_frame_interval.numerator;
	u32 denom = sensor->current_frame_interval.denominator;
	int ret;

	v4l2_dbg(2, debug, &sensor->sd, "%s()\n", __func__);

	// stop streaming for just a moment
	ret = ar0521_write_reg(sensor, AR0521_REG_RESET, AR0521_REG_RESET_DEFAULTS);
	if (ret)
		return ret;

	ret = ar0521_set_geometry(sensor);
	if (ret)
		return ret;

	ret = ar0521_write_regs(sensor, pll_regs, ARRAY_SIZE(pll_regs));
	if (ret)
		return ret;

	ret = ar0521_write_reg(sensor, AR0521_REG_COARSE_INTEGRATION_TIME, sensor->ctrls.exposure->val);
	if (ret)
		return ret;

	ret = ar0521_write_reg(sensor, AR0521_REG_EXTRA_DELAY, sensor->extra_delay);
	if (ret)
		return ret;

	ret = ar0521_write_reg(sensor, AR0521_REG_RESET, AR0521_REG_RESET_DEFAULTS | AR0521_REG_RESET_STREAM);
	if (ret)
		return ret;

	ret = ar0521_write_reg(sensor, AR0521_REG_TEST_PATTERN_MODE, sensor->ctrls.test_pattern->val);
	if (ret)
		return ret;

	dev_info(&sensor->i2c_client->dev,
		 "AR0521: %ux%u, total %ux%u, pixel clock %s MHz, %u (%u/%u) FPS\n",
		 sensor->fmt.width, sensor->fmt.height, sensor->total_width, sensor->total_height,
		 mhz(sensor->pix_clk), (num + denom / 2) / denom, num, denom);
	return 0;
}

static int ar0521_set_stream(struct ar0521_dev *sensor, bool on)
{
	int ret;

	v4l2_dbg(2, debug, &sensor->sd, "%s(%u)\n", __func__, on);

	ret = ar0521_write_mode(sensor);
	if (ret)
		return ret;

	if (on) {
		ret = ar0521_set_gains(sensor);
		if (ret)
			return ret;

		// normal output on clock and data lanes
		ret = ar0521_write_reg(sensor, AR0521_REG_HISPI_CONTROL_STATUS, 0);
		if (ret)
			return ret;
	} else {
		// reset gain, the sensor may produce all white pixels without this
		ret = ar0521_write_reg(sensor, AR0521_REG_GLOBAL_GAIN, 0x2000);
		if (ret)
			return ret;

		// set LP-11 on clock and data lanes
		ret = ar0521_write_reg(sensor, AR0521_REG_HISPI_CONTROL_STATUS,
				       AR0521_REG_HISPI_CONTROL_STATUS_FRAMER_TEST_MODE_ENABLE);
		if (ret)
			return ret;
	}

	// start streaming (possibly with LP-11 on all lines)
	return ar0521_write_reg(sensor, AR0521_REG_RESET,
				AR0521_REG_RESET_DEFAULTS |
				AR0521_REG_RESET_RESTART |
				AR0521_REG_RESET_STREAM);
}

static u32 calc_pll(struct ar0521_dev *sensor, int num, u32 freq, u16 *pre_ptr, u16 *mult_ptr)
{
	u16 pre = 1, mult = 1, new_pre;
	u32 pll = AR0521_PLL_MAX + 1;

	for (new_pre = 1; new_pre < 64; new_pre++) {
		u32 new_pll;
		u32 new_mult = div64_round_up((u64)freq * new_pre, sensor->extclk_freq);

		if (new_mult < 32)
			continue; // minimum value
		if (new_mult > 254)
			break; // maximum, larger pre won't work either
		if (sensor->extclk_freq * (u64)new_mult < AR0521_PLL_MIN * new_pre)
			continue;
		if (sensor->extclk_freq * (u64)new_mult > AR0521_PLL_MAX * new_pre)
			break; // larger pre won't work either
		new_pll = div64_round_up(sensor->extclk_freq * (u64)new_mult, new_pre);
		if (new_pll < pll) {
			pll = new_pll;
			pre = new_pre;
			mult = new_mult;
		}
	}

	pll = div64_round(sensor->extclk_freq * (u64)mult, pre);
	*pre_ptr = pre;
	*mult_ptr = mult;
	return pll;
}

static void ar0521_adj_fmt(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = clamp(ALIGN(fmt->width, 4), AR0521_WIDTH_MIN, AR0521_WIDTH_MAX);
	fmt->height = clamp(ALIGN(fmt->height, 4), AR0521_HEIGHT_MIN, AR0521_HEIGHT_MAX);
	fmt->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

#define DIV 4
static void ar0521_calc_mode(struct ar0521_dev *sensor)
{
	unsigned int speed_mod = 4 / lanes(sensor); // 1 with 4 DDR lanes
	u64 pix_clk; // for calculations
	u32 pixels, num, denom, new_total_height, new_pixels;
	u16 total_width, total_height;

	total_width = max(sensor->fmt.width + AR0521_WIDTH_BLANKING_MIN, AR0521_TOTAL_WIDTH_MIN);
	total_height = sensor->fmt.height + AR0521_HEIGHT_BLANKING_MIN;

	pixels = total_width * total_height;
	num = sensor->frame_interval.numerator;
	denom = sensor->frame_interval.denominator;

	// calculate approximate pixel clock first
	pix_clk = div64_round_up(pixels * (u64)num, denom);
	if (pix_clk > AR0521_PIXEL_CLOCK_MAX) {
		u32 cnt;
		// have to recalculate FPS
		num = pix_clk = AR0521_PIXEL_CLOCK_MAX;
		denom = pixels;
		// try to reduce the numbers a bit
		for (cnt = 2; cnt * cnt < denom; cnt++)
			while (num % cnt == 0 && denom % cnt == 0) {
				num /= cnt;
				denom /= cnt;
			}
	} else if (pix_clk < AR0521_PIXEL_CLOCK_MIN)
		// we will compensate with total_height and extra_delay
		pix_clk = AR0521_PIXEL_CLOCK_MIN;

	sensor->current_frame_interval.numerator = num;
	sensor->current_frame_interval.denominator = denom;

	// PLL1 drives pixel clock - dual rate
	pix_clk = calc_pll(sensor, 1, pix_clk * (DIV / 2), &sensor->pll_pre, &sensor->pll_mult);
	pix_clk = div64_round(pix_clk, (DIV / 2));
	calc_pll(sensor, 2, pix_clk * (DIV / 2) * speed_mod, &sensor->pll_pre2, &sensor->pll_mult2);

	// let's see if we can do better
	new_total_height = (div64_round((u64)pix_clk * denom, num) / total_width) & ~1; // must be even
	if (new_total_height > total_height) {
		total_height = new_total_height;
		pixels = total_width * total_height;
	}

	// maybe there is still room for improvement
	new_pixels = div64_round(pix_clk * denom, num);
	sensor->extra_delay = 0;
	if (new_pixels > pixels)
		sensor->extra_delay = new_pixels - pixels;

	sensor->pix_clk = pix_clk;
	sensor->total_width = total_width;
	sensor->total_height = total_height;
}

static int ar0521_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	struct v4l2_mbus_framefmt *fmt;

	if (format->pad)
		return -EINVAL;

	v4l2_dbg(1, debug, &sensor->sd, "%s(%u)\n", __func__, format->which);

	mutex_lock(&sensor->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt = v4l2_subdev_get_try_format(&sensor->sd, sd_state, 0 /* pad */);
	else
		fmt = &sensor->fmt;

	format->format = *fmt;

	mutex_unlock(&sensor->lock);
	return 0;
}

static int ar0521_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int ret = 0;

	if (format->pad)
		return -EINVAL;

	v4l2_dbg(1, debug, &sensor->sd, "%s(%u)\n", __func__, format->which);

	ar0521_adj_fmt(&format->format);

	mutex_lock(&sensor->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, sd_state, 0 /* pad */);
		*fmt = format->format;
	} else {
		sensor->fmt = format->format;
		ar0521_calc_mode(sensor);
		ret = ar0521_write_mode(sensor);
	}

	mutex_unlock(&sensor->lock);
	return ret;
}

static int ar0521_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int ret;

	// v4l2_ctrl_lock() locks our own mutex

	v4l2_dbg(1, debug, &sensor->sd, "%s(0x%X)\n", __func__, ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_HBLANK:
	case V4L2_CID_VBLANK:
		sensor->total_width = sensor->fmt.width + sensor->ctrls.hblank->val;
		sensor->total_height = sensor->fmt.width + sensor->ctrls.vblank->val;
		ret = ar0521_set_geometry(sensor);
		break;
	case V4L2_CID_GAIN:
	case V4L2_CID_RED_BALANCE:
	case V4L2_CID_BLUE_BALANCE:
		ret = ar0521_set_gains(sensor);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ar0521_write_reg(sensor, AR0521_REG_COARSE_INTEGRATION_TIME, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ar0521_write_reg(sensor, AR0521_REG_TEST_PATTERN_MODE, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ar0521_ctrl_ops = {
	.s_ctrl = ar0521_s_ctrl,
};

static const char * const test_pattern_menu[] = {
	"Disabled",
	"Solid color",
	"Color bars",
	"Faded color bars"
};

static int ar0521_init_controls(struct ar0521_dev *sensor)
{
	const struct v4l2_ctrl_ops *ops = &ar0521_ctrl_ops;
	struct ar0521_ctrls *ctrls = &sensor->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	v4l2_ctrl_handler_init(hdl, 32);

	// we can use our own mutex for the ctrl lock
	hdl->lock = &sensor->lock;

	// manual gain
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN, 0, 511, 1, 0);
	ctrls->red_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_RED_BALANCE, -512, 511, 1, 0);
	ctrls->blue_balance = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BLUE_BALANCE, -512, 511, 1, 0);

	// alternate for frame interval
	ctrls->hblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HBLANK, AR0521_WIDTH_BLANKING_MIN, 4094, 1, AR0521_WIDTH_BLANKING_MIN);
	ctrls->vblank = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VBLANK, AR0521_HEIGHT_BLANKING_MIN, 4094, 2, AR0521_HEIGHT_BLANKING_MIN);
	// Read-only
	ctrls->pixrate = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_PIXEL_RATE, AR0521_PIXEL_CLOCK_MIN, AR0521_PIXEL_CLOCK_MAX, 1, AR0521_PIXEL_CLOCK_MIN);

	// manual exposure time
	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE, 0, 65535, 1, 0);

	ctrls->test_pattern = v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
							   ARRAY_SIZE(test_pattern_menu) - 1,
							   0, 0, test_pattern_menu);

	if (hdl->error) {
		ret = hdl->error;
		goto free_ctrls;
	}

	sensor->sd.ctrl_handler = hdl;
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(hdl);
	return ret;
}

static const struct initial_reg {
	u16 addr, value;
} initial_regs[] = {
	// corrections_recommended_bayer
	{0x3042, 0x0004}, // RNC:enable b/w rnc mode
	{0x3044, 0x4580}, // RNC:enable row noise correction
	{0x30EE, 0x1136}, // RNC:rnc scaling factor-->initial recommended setting
	{0x3120, 0x0001}, // recommended setting for dither
	{0x3F2C, 0x442E}, // GTH_THRES_RTN: 7max,7min filtered out of every 46
	{0x30D2, 0x0000}, // CRM/CC: enable crm on Visible and CC rows
	{0x30D4, 0x0000}, // CC: CC enabled with 16 samples per column
	{0x30D6, 0x2FFF}, // CC: bw mode enabled/12 bit data resolution/bw mode
	{0x30DA, 0x0FFF}, // CC: column correction clip level 2 is 0
	{0x30DC, 0x0FFF}, // CC: column correction clip level 3 is 0
	{0x30DE, 0x0000}, // CC: Group FPN correction
	{0x31E0, 0x0781}, // Fuse/2DDC: enable 2ddc
	{0x3180, 0x9434}, // FDOC:fdoc settings with fdoc every frame turned of
	{0x3172, 0x0206}, // txlo clk divider options
	{0x3F00, 0x0017}, // BM_T0
	{0x3F02, 0x02DD}, // BM_T1
	{0x3F04, 0x0020}, // if Ana_gain less than 2, use noise_floor0, multipl
	{0x3F06, 0x0040}, // if Ana_gain between 4 and 7, use noise_floor2 and
	{0x3F08, 0x0070}, // if Ana_gain between 4 and 7, use noise_floor2 and
	{0x3F0A, 0x0101}, // Define noise_floor0(low address) and noise_floor1
	{0x3F0C, 0x0302}, // Define noise_floor2 and noise_floor3
	{0x3F1E, 0x0022},
	{0x3F1A, 0x01FF}, // cross factor 2
	{0x3F14, 0x0505}, // single k factor 2
	{0x3F44, 0x0707}, // couple k factor 2
	{0x3F18, 0x01FF}, // cross factor 1
	{0x3F12, 0x0505}, // single k factor 1
	{0x3F42, 0x1511}, // couple k factor 1
	{0x3F16, 0x01FF}, // cross factor 0
	{0x3F10, 0x0505}, // single k factor 0
	{0x3F40, 0x1511}, // couple k factor 0

	// analog_setup_recommended_12bit
	{0x3EB6, 0x004C}, // ECL
	{0x3EBA, 0xAAAA},
	{0x3EBC, 0x0086}, // Bias currents for FSC/ECL
	{0x3EC0, 0x1E00}, // SFbin/SH mode settings
	{0x3EC2, 0x100B}, // CLK divider for ramp for 12 bit 400MHz mode only
	{0x3EC4, 0x3300}, // FSC clamps for HDR mode and adc comp power down co
	{0x3EC6, 0xEA44}, // VLN and clk gating controls
	{0x3EC8, 0x6F6F}, // Txl0 and Txlo1 settings for normal mode
	{0x3ECA, 0x2F4A}, // CDAC/Txlo2/RSTGHI/RSTGLO settings
	{0x3ECC, 0x0506}, // RSTDHI/RSTDLO/CDAC/TXHI settings
	{0x3ECE, 0x203B}, // Ramp buffer settings and Booster enable (bits 0-5)
	{0x3ED0, 0x13F0}, // TXLO from atest/sf bin settings
	{0x3ED2, 0x9A3D}, // Booster settings for reference rows/columns
	{0x3ED4, 0x862F}, // TXLO open loop/row driver settings
	{0x3ED6, 0x4081}, // Txlatch fr cfpn rows/vln bias
	{0x3ED8, 0x4003}, // Ramp step setting for 12 bit 400 Mhz mode
	{0x3EDA, 0x9A80}, // ramp offset for T1/normal and rst under range
	{0x3EDC, 0xC000}, // over range for rst and under range for sig
	{0x3EDE, 0xC103}, // over range for sig and col dec clk settings
	{0x3426, 0x1600}, // ADC offset distribution pulse
	{0x342A, 0x0038}, // pulse_config
	{0x3F3E, 0x0001}, // Switch ADC from 10 bit to 12 bit mode
	{0x341A, 0x6051},
	{0x3420, 0x6051},

	// analog_setup_recommended_10bit
	{0x3EC2, 0x100A}, // CLK divider for ramp for 10 bit 400MH
	{0x3ED8, 0x8003}, // Ramp step setting for 10 bit 400 Mhz
	{0x341A, 0x4735}, // Samp&Hold pulse in ADC
	{0x3420, 0x4735}, // Samp&Hold pulse in ADC
	{0x3426, 0x8A1A}, // ADC offset distribution pulse
	{0x342A, 0x0018}, // pulse_config
	{0x3ED2, 0xA53D}, // Ramp offset
	{0x3EDA, 0xA580}, // Ramp Offset
	{0x3EBA, 0xAAAD},
	{0x3EB6, 0x004C},
	{0x3F3E, 0x0000}, // Switch ADC from 12 bit to 10 bit mode

	// new RNC 10bit
	{0x30EE, 0x1136}, // RNC:rnc scaling factor=*54/64 (32/38*64=53.9)
	{0x3F2C, 0x442E}, // GTH_THRES_RTN: 4max,4min filtered out of every 46 samples and
	// for 10bit mode
	{0x301E, 0x00AA}, // PEDESTAL+2 :+2 is a workaround for 10bit mode +0.5 Rounding
	{0x3120, 0x0005}, // p1 dither enabled for 10bit mode

	{0x0112, 0x0808}, // 8-bit/8-bit mode
	{0x31BC, 0x068C}, // don't use continuous clock mode while shut down
	{0x30FA, 0xFD00}, // GPIO0 = flash, GPIO1 = shutter
	{0x31B0, 0x008B}, // frame_preamble - FIXME check WRT lanes#
	{0x31B2, 0x0050}, // line_preamble - FIXME check WRT lanes#
};

static __be16 pixel_timing_recommended[] = {
	be(0x3D00), // first register address
	/* 3D00 */ be(0x043E), be(0x4760), be(0xFFFF), be(0xFFFF), be(0x8000), be(0x0510), be(0xAF08), be(0x0252),
	/* 3D10 */ be(0x486F), be(0x5D5D), be(0x8056), be(0x8313), be(0x0087), be(0x6A48), be(0x6982), be(0x0280),
	/* 3D20 */ be(0x8359), be(0x8D02), be(0x8020), be(0x4882), be(0x4269), be(0x6A95), be(0x5988), be(0x5A83),
	/* 3D30 */ be(0x5885), be(0x6280), be(0x6289), be(0x6097), be(0x5782), be(0x605C), be(0xBF18), be(0x0961),
	/* 3D40 */ be(0x5080), be(0x2090), be(0x4390), be(0x4382), be(0x5F8A), be(0x5D5D), be(0x9C63), be(0x8063),
	/* 3D50 */ be(0xA960), be(0x9757), be(0x8260), be(0x5CFF), be(0xBF10), be(0x1681), be(0x0802), be(0x8000),
	/* 3D60 */ be(0x141C), be(0x6000), be(0x6022), be(0x4D80), be(0x5C97), be(0x6A69), be(0xAC6F), be(0x4645),
	/* 3D70 */ be(0x4400), be(0x0513), be(0x8069), be(0x6AC6), be(0x5F95), be(0x5F70), be(0x8040), be(0x4A81),
	/* 3D80 */ be(0x0300), be(0xE703), be(0x0088), be(0x4A83), be(0x40FF), be(0xFFFF), be(0xFD70), be(0x8040),
	/* 3D90 */ be(0x4A85), be(0x4FA8), be(0x4F8C), be(0x0070), be(0xBE47), be(0x8847), be(0xBC78), be(0x6B89),
	/* 3DA0 */ be(0x6A80), be(0x6986), be(0x6B8E), be(0x6B80), be(0x6980), be(0x6A88), be(0x7C9F), be(0x866B),
	/* 3DB0 */ be(0x8765), be(0x46FF), be(0xE365), be(0xA679), be(0x4A40), be(0x4580), be(0x44BC), be(0x7000),
	/* 3DC0 */ be(0x8040), be(0x0802), be(0x10EF), be(0x0104), be(0x3860), be(0x5D5D), be(0x5682), be(0x1300),
	/* 3DD0 */ be(0x8648), be(0x8202), be(0x8082), be(0x598A), be(0x0280), be(0x2048), be(0x3060), be(0x8042),
	/* 3DE0 */ be(0x9259), be(0x865A), be(0x8258), be(0x8562), be(0x8062), be(0x8560), be(0x9257), be(0x8221),
	/* 3DF0 */ be(0x10FF), be(0xB757), be(0x9361), be(0x1019), be(0x8020), be(0x9043), be(0x8E43), be(0x845F),
	/* 3E00 */ be(0x835D), be(0x805D), be(0x8163), be(0x8063), be(0xA060), be(0x9157), be(0x8260), be(0x5CFF),
	/* 3E10 */ be(0xFFFF), be(0xFFE5), be(0x1016), be(0x2048), be(0x0802), be(0x1C60), be(0x0014), be(0x0060),
	/* 3E20 */ be(0x2205), be(0x8120), be(0x908F), be(0x6A80), be(0x6982), be(0x5F9F), be(0x6F46), be(0x4544),
	/* 3E30 */ be(0x0005), be(0x8013), be(0x8069), be(0x6A80), be(0x7000), be(0x0000), be(0x0000), be(0x0000),
	/* 3E40 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	/* 3E50 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	/* 3E60 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	/* 3E70 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	/* 3E80 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	/* 3E90 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	/* 3EA0 */ be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000), be(0x0000),
	/* 3EB0 */ be(0x0000), be(0x0000), be(0x0000)};

static void ar0521_power_off(struct ar0521_dev *sensor)
{
	int i;

	v4l2_dbg(1, debug, &sensor->sd, "%s()\n", __func__);
	clk_disable_unprepare(sensor->extclk);

	if (sensor->reset_gpio)
		gpiod_set_value(sensor->reset_gpio, 1); // assert RESET signal

	for (i = AR0521_NUM_SUPPLIES - 1; i >= 0; i--) {
		if (sensor->supplies[i])
			regulator_disable(sensor->supplies[i]);
	}
}

static int ar0521_power_on(struct ar0521_dev *sensor)
{
	int i, ret;
	unsigned int nlanes = lanes(sensor);

	v4l2_dbg(1, debug, &sensor->sd, "%s()\n", __func__);
	for (i = 0; i < AR0521_NUM_SUPPLIES; i++)
		if (sensor->supplies[i]) {
			ret = regulator_enable(sensor->supplies[i]);
			if (ret < 0)
				goto off;

			usleep_range(1000, 1500); // min 1 ms
		}

	ret = clk_prepare_enable(sensor->extclk);
	if (ret < 0) {
		v4l2_err(&sensor->sd, "error enabling sensor clock\n");
		goto off;
	}
	usleep_range(1000, 1500); // min 1 ms

	if (sensor->reset_gpio)
		gpiod_set_value(sensor->reset_gpio, 0); // deassert RESET signal
	usleep_range(4500, 5000); // min 45000 clocks

	for (i = 0; i < ARRAY_SIZE(initial_regs); i++)
		if (ar0521_write_reg(sensor, initial_regs[i].addr, initial_regs[i].value))
			goto off;

	ret = ar0521_write_regs(sensor, pixel_timing_recommended, ARRAY_SIZE(pixel_timing_recommended));
	if (ret)
		goto off;

	ret = ar0521_write_reg(sensor, AR0521_REG_SERIAL_FORMAT, AR0521_REG_SERIAL_FORMAT_MIPI | nlanes);
	if (ret)
		goto off;

	// set MIPI test mode - disabled for now
	ret = ar0521_write_reg(sensor, AR0521_REG_HISPI_TEST_MODE,
			       ((0x40 << nlanes) - 0x40) | AR0521_REG_HISPI_TEST_MODE_LP11);
	if (ret)
		goto off;

	ret = ar0521_write_reg(sensor, AR0521_REG_ROW_SPEED, 0x110 | 4 / nlanes);
	if (ret)
		goto off;

	ar0521_calc_mode(sensor);

	ret = ar0521_set_stream(sensor, 0);
	if (ret)
		goto off;

	return 0;
off:
	ar0521_power_off(sensor);
	return ret;
}

static int ar0521_s_power(struct v4l2_subdev *sd, int on)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);

	v4l2_dbg(1, debug, &sensor->sd, "%s(%u, %s)\n", __func__, sensor->power_count, on ? "on" : "off");

	if (on) {
		if (!sensor->power_count++) {
			int ret = ar0521_power_on(sensor);

			if (ret) {
				sensor->power_count = 0; // undo
				return ret;
			}
		}
	} else if (!--sensor->power_count)
		ar0521_power_off(sensor);

	return 0;
}

static int ar0521_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);

	if (code->pad || code->index)
		return -EINVAL;

	code->code = sensor->fmt.code;
	v4l2_dbg(1, debug, &sensor->sd, "%s() = %X\n", __func__, code->code);
	return 0;
}

static int ar0521_g_frame_interval(struct v4l2_subdev *sd, struct v4l2_subdev_frame_interval *fi)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);

	mutex_lock(&sensor->lock);
	fi->interval = sensor->current_frame_interval;
	mutex_unlock(&sensor->lock);
	v4l2_dbg(1, debug, &sensor->sd, "%s() = %u/%u\n", __func__,
		 fi->interval.numerator, fi->interval.denominator);
	return 0;
}

static int ar0521_s_frame_interval(struct v4l2_subdev *sd, struct v4l2_subdev_frame_interval *fi)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int ret;

	if (fi->pad)
		return -EINVAL;

	v4l2_dbg(1, debug, &sensor->sd, "%s(%u/%u)\n", __func__,
		 fi->interval.numerator, fi->interval.denominator);
	mutex_lock(&sensor->lock);

	if (sensor->streaming) {
		ret = -EBUSY;
		goto out;
	}

	sensor->frame_interval = fi->interval;
	ar0521_calc_mode(sensor);
	ret = ar0521_write_mode(sensor);
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ar0521_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ar0521_dev *sensor = to_ar0521_dev(sd);
	int ret;

	v4l2_dbg(1, debug, &sensor->sd, "%s(%i)\n", __func__, enable);
	mutex_lock(&sensor->lock);

	ret = ar0521_set_stream(sensor, enable);
	sensor->streaming = enable;

	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_subdev_core_ops ar0521_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.s_power = ar0521_s_power,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ar0521_video_ops = {
	.g_frame_interval = ar0521_g_frame_interval,
	.s_frame_interval = ar0521_s_frame_interval,
	.s_stream = ar0521_s_stream,
};

static const struct v4l2_subdev_pad_ops ar0521_pad_ops = {
	.enum_mbus_code = ar0521_enum_mbus_code,
	.get_fmt = ar0521_get_fmt,
	.set_fmt = ar0521_set_fmt,
};

static const struct v4l2_subdev_ops ar0521_subdev_ops = {
	.core = &ar0521_core_ops,
	.video = &ar0521_video_ops,
	.pad = &ar0521_pad_ops,
};

static int ar0521_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct ar0521_dev *sensor;
	unsigned int cnt, nlanes;
	int ret;

	v4l2_dbg(1, debug, client, "%s()\n", __func__);
	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->i2c_client = client;
	sensor->fmt.code = MEDIA_BUS_FMT_SGRBG8_1X8;
	sensor->fmt.width = AR0521_WIDTH_MAX;
	sensor->fmt.height = AR0521_HEIGHT_MAX;
	sensor->fmt.field = V4L2_FIELD_NONE;
	sensor->frame_interval.numerator = 30;
	sensor->frame_interval.denominator = 1;

	endpoint = fwnode_graph_get_next_endpoint(of_fwnode_handle(dev->of_node), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &sensor->ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "could not parse endpoint\n");
		return ret;
	}

	if (sensor->ep.bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(dev, "invalid bus type, must be MIPI CSI2\n");
		return -EINVAL;
	}

	nlanes = lanes(sensor);
	switch (nlanes) {
	case 1:
	case 2:
	case 4:
		break;
	default:
		dev_err(dev, "invalid number of MIPI data lane%s\n", nlanes > 1 ? "s" : "");
		return -EINVAL;
	}

	// get master clock (extclk)
	sensor->extclk = devm_clk_get(dev, "extclk");
	if (IS_ERR(sensor->extclk)) {
		dev_err(dev, "failed to get extclk\n");
		return PTR_ERR(sensor->extclk);
	}

	ret = clk_set_rate(sensor->extclk, AR0521_EXTCLK_RATE);
	if (ret < 0) {
		dev_err(dev, "error setting clock rate\n");
		return ret;
	}

	sensor->extclk_freq = clk_get_rate(sensor->extclk);

	if (sensor->extclk_freq < AR0521_EXTCLK_MIN ||
	    sensor->extclk_freq > AR0521_EXTCLK_MAX) {
		dev_err(dev, "extclk frequency out of range: %u Hz\n", sensor->extclk_freq);
		return -EINVAL;
	}

	// request optional reset pin (usually active low) and assert it
	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);

	v4l2_i2c_subdev_init(&sensor->sd, client, &ar0521_subdev_ops);

	sensor->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	for (cnt = 0; cnt < AR0521_NUM_SUPPLIES; cnt++) {
		struct regulator *supply = devm_regulator_get(dev, ar0521_supply_names[cnt]);

		if (IS_ERR(supply)) {
			dev_info(dev, "no %s regulator found: %li\n", ar0521_supply_names[cnt], PTR_ERR(supply));
			return PTR_ERR(supply);
		}
		sensor->supplies[cnt] = supply;
	}

	mutex_init(&sensor->lock);

	ret = ar0521_init_controls(sensor);
	if (ret)
		goto entity_cleanup;

	ret = v4l2_async_register_subdev(&sensor->sd);
	if (ret)
		goto free_ctrls;

	ar0521_adj_fmt(&sensor->fmt);

	dev_info(dev, "AR0521 driver initialized, master clock frequency: %s MHz, %u MIPI data lanes\n",
		 mhz(sensor->extclk_freq), nlanes);
	return 0;

free_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
entity_cleanup:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static int ar0521_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0521_dev *sensor = to_ar0521_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls.handler);
	mutex_destroy(&sensor->lock);
	return 0;
}

static const struct i2c_device_id ar0521_id[] = {
	{"ar0521", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ar0521_id);

static const struct of_device_id ar0521_dt_ids[] = {
	{.compatible = "onnn,ar0521"},
	{}
};
MODULE_DEVICE_TABLE(of, ar0521_dt_ids);

static struct i2c_driver ar0521_i2c_driver = {
	.driver = {
		.name  = "ar0521",
		.of_match_table	= ar0521_dt_ids,
	},
	.id_table = ar0521_id,
	.probe    = ar0521_probe,
	.remove   = ar0521_remove,
};

module_i2c_driver(ar0521_i2c_driver);

MODULE_DESCRIPTION("AR0521 MIPI Camera subdev driver");
MODULE_AUTHOR("Krzysztof Hałasa <khalasa@piap.pl>");
MODULE_LICENSE("GPL v2");
