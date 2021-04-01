// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Techwell TW9900 multi-standard video decoder.
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 * Copyright (C) 2020 Maxime Chevallier <maxime.chevallier@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#define TW9900_REG_CHIP_ID	0x00
#define TW9900_REG_CHIP_STATUS	0x01
#define TW9900_REG_CHIP_STATUS_VLOCK	0x08
#define TW9900_REG_CHIP_STATUS_VDLOSS	0x80
#define TW9900_REG_OUT_FMT_CTL	0x03
#define TW9900_REG_OUT_FMT_CTL_STANDBY	0xA7
#define TW9900_REG_OUT_FMT_CTL_STREAMING	0xA0
#define TW9900_REG_CKHY_HSDLY	0x04
#define TW9900_REG_OUT_CTRL_I	0x05
#define TW9900_REG_ANALOG_CTL	0x06
#define TW9900_REG_CROP_HI	0x07
#define TW9900_REG_VDELAY_LO	0x08
#define TW9900_REG_VACTIVE_LO	0x09
#define TW9900_REG_HACTIVE_LO	0x0B
#define TW9900_REG_CNTRL1	0x0C
#define TW9900_REG_BRIGHT_CTL	0x10
#define TW9900_REG_CONTRAST_CTL	0x11
#define TW9900_REG_VBI_CNTL	0x19
#define TW9900_REG_ANAL_CTL_II	0x1A
#define TW9900_REG_OUT_CTRL_II	0x1B
#define TW9900_REG_STD_SEL	0x1C
#define    TW9900_REG_STD_SEL_AUTODETECT_IN_PROGRESS	BIT(7)
#define TW9900_REG_STDR		0x1D
#define TW9900_REG_MISSCNT	0x26
#define TW9900_REG_MISC_CTL_II	0x2F
#define TW9900_REG_VVBI		0x55

#define TW9900_CHIP_ID		0x00

#define VSYNC_POLL_INTERVAL_MS	20
#define VSYNC_WAIT_MAX_POLLS	50

#define TW9900_STD_NTSC_M	0
#define TW9900_STD_PAL_BDGHI	1
#define TW9900_STD_SECAM	2
#define TW9900_STD_NTSC_4_43	3
#define TW9900_STD_PAL_M	4
#define TW9900_STD_PAL_CN	5
#define TW9900_STD_PAL_60	6
#define TW9900_STD_AUTO		7
#define TW9900_STD_UNKOWN	7

#define TW9900_VIDEO_POLL_TIMEOUT 20

struct regval {
	u8 addr;
	u8 val;
};

struct tw9900_mode {
	u32 width;
	u32 height;
	u32 skip_top;
	u32 std;
	u32 field;
	const struct regval *reg_list;
	int n_regs;
};

struct tw9900 {
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct regulator *regulator;

	bool streaming;

	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler hdl;
	struct media_pad pad;

	struct timer_list timer;
	struct work_struct work_i2c_poll;

	const struct tw9900_mode *cur_mode;
};

#define to_tw9900(sd) container_of(sd, struct tw9900, subdev)

static const struct regval tw9900_init_regs[] = {
	{ TW9900_REG_MISC_CTL_II,	0xE6 },
	{ TW9900_REG_MISSCNT,		0x24 },
	{ TW9900_REG_OUT_FMT_CTL,	0xA7 },
	{ TW9900_REG_ANAL_CTL_II,	0x0A },
	{ TW9900_REG_VDELAY_LO,		0x19 },
	{ TW9900_REG_STD_SEL,		0x00 },
	{ TW9900_REG_VACTIVE_LO,	0xF0 },
	{ TW9900_REG_STD_SEL,		0x07 },
	{ TW9900_REG_CKHY_HSDLY,	0x40 },
	{ TW9900_REG_ANALOG_CTL,	0x80 },
	{ TW9900_REG_CNTRL1,		0xDC },
	{ TW9900_REG_OUT_CTRL_I,	0x98 },
};

static const struct regval tw9900_pal_regs[] = {
	{ TW9900_REG_STD_SEL,		0x01 },
};

static const struct regval tw9900_ntsc_regs[] = {
	{ TW9900_REG_OUT_FMT_CTL,	0xA4 },
	{ TW9900_REG_VDELAY_LO,		0x12 },
	{ TW9900_REG_VACTIVE_LO,	0xF0 },
	{ TW9900_REG_CROP_HI,		0x02 },
	{ TW9900_REG_HACTIVE_LO,	0xD0 },
	{ TW9900_REG_VBI_CNTL,		0x01 },
	{ TW9900_REG_STD_SEL,		0x00 },
};

static const struct tw9900_mode supported_modes[] = {
	{
		.width = 720,
		.height = 576,
		.skip_top = 0,
		.std = V4L2_STD_PAL,
		.field = V4L2_FIELD_NONE,
		.reg_list = tw9900_pal_regs,
		.n_regs = ARRAY_SIZE(tw9900_pal_regs),
	},
	{
		.width = 720,
		.height = 480,
		.skip_top = 0,
		.std = V4L2_STD_NTSC,
		.field = V4L2_FIELD_NONE,
		.reg_list = tw9900_ntsc_regs,
		.n_regs = ARRAY_SIZE(tw9900_ntsc_regs),
	},
};

static int tw9900_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "write reg error: %d\n", ret);

	return ret;
}

static int tw9900_write_array(struct i2c_client *client,
			      const struct regval *regs, int n_regs)
{
	int i, ret = 0;

	for (i = 0; ret == 0 && i <= n_regs; i++)
		ret = tw9900_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

static inline u8 tw9900_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static void tw9900_fill_fmt(const struct tw9900_mode *mode,
			    struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = mode->field;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
}

static int tw9900_cfg_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	tw9900_fill_fmt(tw9900->cur_mode, mbus_fmt);

	return 0;
}

static int tw9900_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= 1)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;

	return 0;
}

static int tw9900_power_on(struct tw9900 *tw9900)
{
	int ret;
	struct device *dev = &tw9900->client->dev;

	if (tw9900->reset_gpio)
		gpiod_set_value_cansleep(tw9900->reset_gpio, 1);

	ret = regulator_enable(tw9900->regulator);
	if (ret < 0)
		goto error;

	usleep_range(50000, 52000);

	if (tw9900->reset_gpio)
		gpiod_set_value_cansleep(tw9900->reset_gpio, 0);

	usleep_range(1000, 2000);

	ret = tw9900_write_array(tw9900->client, tw9900_init_regs,
				 ARRAY_SIZE(tw9900_init_regs));
	if (ret) {
		dev_err(dev, "Failed to init tw9900\n");
		goto error;
	}

	return 0;

error:

	return ret;
}

static void tw9900_power_off(struct tw9900 *tw9900)
{
	if (tw9900->reset_gpio)
		gpiod_set_value_cansleep(tw9900->reset_gpio, 1);

	regulator_disable(tw9900->regulator);
}

static int tw9900_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tw9900 *tw9900 = container_of(ctrl->handler, struct tw9900, hdl);

	if (pm_runtime_suspended(&tw9900->client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		tw9900_write_reg(tw9900->client, 0x10, (u8)ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		tw9900_write_reg(tw9900->client, 0x11, (u8)ctrl->val);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tw9900_s_stream(struct v4l2_subdev *sd, int on)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	struct i2c_client *client = tw9900->client;
	int ret = 0;

	on = !!on;
	if (on == tw9900->streaming)
		return 0;

	if (on) {
		ret = pm_runtime_get_sync(&tw9900->client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			return ret;
		}

		ret = v4l2_ctrl_handler_setup(sd->ctrl_handler);
		if (ret)
			goto put_and_return;

		ret = tw9900_write_array(tw9900->client,
					 tw9900->cur_mode->reg_list,
					 tw9900->cur_mode->n_regs);
		if (ret)
			goto put_and_return;

		ret = tw9900_write_reg(client, TW9900_REG_OUT_FMT_CTL,
				       TW9900_REG_OUT_FMT_CTL_STREAMING);
		if (ret)
			goto put_and_return;

	} else {
		tw9900_write_reg(client, TW9900_REG_OUT_FMT_CTL,
				 TW9900_REG_OUT_FMT_CTL_STANDBY);
		pm_runtime_put(&client->dev);
	}

	tw9900->streaming = on;

	return ret;

put_and_return:
	pm_runtime_put(&client->dev);

	return ret;
}

static int tw9900_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tw9900 *tw9900 = to_tw9900(sd);

	return tw9900_power_on(tw9900);
}

static int tw9900_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tw9900 *tw9900 = to_tw9900(sd);

	tw9900_power_off(tw9900);

	return 0;
}

static int tw9900_subscribe_event(struct v4l2_subdev *sd,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct tw9900_mode *tw9900_get_mode_from_std(v4l2_std_id std)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++)
		if (supported_modes[i].std == std)
			return &supported_modes[i];

	return NULL;
}

static int tw9900_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	const struct tw9900_mode *mode;
	int ret;

	if (!(norm & (V4L2_STD_NTSC | V4L2_STD_PAL)))
		return -EINVAL;

	mode = tw9900_get_mode_from_std(norm);
	if (!mode)
		return -EINVAL;

	ret = tw9900_write_array(tw9900->client, mode->reg_list, mode->n_regs);
	if (ret)
		return ret;

	tw9900->cur_mode = mode;

	return 0;
}


static unsigned int tw9900_get_stream_std(struct tw9900 *tw9900)
{
	u8 std, val;

	/* proper #define */
	val = tw9900_read_reg(tw9900->client, TW9900_REG_STD_SEL);
	std = (val & 0x70) >> 4;

	switch (std) {

	case TW9900_STD_NTSC_M:
		return V4L2_STD_NTSC;
	case TW9900_STD_PAL_BDGHI:
		return V4L2_STD_PAL;
	case TW9900_STD_SECAM:
		return V4L2_STD_SECAM;
	case TW9900_STD_NTSC_4_43:
		return V4L2_STD_NTSC;
	case TW9900_STD_PAL_M:
	case TW9900_STD_PAL_CN:
	case TW9900_STD_PAL_60:
		return V4L2_STD_PAL;
	case TW9900_STD_AUTO:
		return V4L2_STD_UNKNOWN;
	default:
		return V4L2_STD_UNKNOWN;

	}
	return V4L2_STD_UNKNOWN;
}

static int tw9900_g_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct tw9900 *tw9900 = to_tw9900(sd);

	*std = tw9900->cur_mode->std;

	return 0;
}

static void tw9900_start_autodetect(struct tw9900 *tw9900)
{
	tw9900_write_reg(tw9900->client, TW9900_REG_STDR,
			 BIT(TW9900_STD_NTSC_M) | BIT(TW9900_STD_PAL_BDGHI));

	tw9900_write_reg(tw9900->client, TW9900_REG_STD_SEL, TW9900_STD_AUTO);

	tw9900_write_reg(tw9900->client, TW9900_REG_STDR,
			 BIT(TW9900_STD_NTSC_M) | BIT(TW9900_STD_PAL_BDGHI) |
			 BIT(TW9900_STD_AUTO));

	/* Autodetect takes a while to start, and during the starting sequence
	 * the autodetection status is reported as done.
	 */
	msleep(30);
}

static int tw9900_cancel_autodetect(struct tw9900 *tw9900)
{
	return tw9900_s_std(&tw9900->subdev, tw9900->cur_mode->std);
}

static bool tw9900_detect_done(struct tw9900 *tw9900)
{
	u8 val = tw9900_read_reg(tw9900->client, TW9900_REG_STD_SEL);

	return !(val & TW9900_REG_STD_SEL_AUTODETECT_IN_PROGRESS);
}

static int tw9900_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	int i;

	if (tw9900->streaming)
		return -EBUSY;

	tw9900_start_autodetect(tw9900);

	for (i = 0; i < TW9900_VIDEO_POLL_TIMEOUT; i++) {
		if (tw9900_detect_done(tw9900))
			break;

		msleep(20);
	}

	if (!tw9900_detect_done(tw9900)) {
		tw9900_cancel_autodetect(tw9900);
		return -EBUSY;
	}

	*std = tw9900_get_stream_std(tw9900);

	return 0;
}

static int tw9900_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	*norm = V4L2_STD_NTSC | V4L2_STD_PAL;

	return 0;
}

static const struct dev_pm_ops tw9900_pm_ops = {
	SET_RUNTIME_PM_OPS(tw9900_runtime_suspend,
			   tw9900_runtime_resume, NULL)
};

static const struct v4l2_subdev_core_ops tw9900_core_ops = {
	.subscribe_event	= tw9900_subscribe_event,
	.unsubscribe_event	= v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops tw9900_video_ops = {
	.s_std		= tw9900_s_std,
	.g_std		= tw9900_g_std,
	.querystd	= tw9900_querystd,
	.g_tvnorms	= tw9900_g_tvnorms,
	.s_stream	= tw9900_s_stream,
};

static const struct v4l2_subdev_pad_ops tw9900_pad_ops = {
	.enum_mbus_code		= tw9900_enum_mbus_code,
	.get_fmt		= tw9900_cfg_fmt,
	.set_fmt		= tw9900_cfg_fmt,
};

static const struct v4l2_subdev_ops tw9900_subdev_ops = {
	.core	= &tw9900_core_ops,
	.video	= &tw9900_video_ops,
	.pad	= &tw9900_pad_ops,
};

static const struct v4l2_ctrl_ops tw9900_ctrl_ops = {
	.s_ctrl	= tw9900_s_ctrl,
};

static int tw9900_check_id(struct tw9900 *tw9900,
			   struct i2c_client *client)
{
	struct device *dev = &tw9900->client->dev;
	u8 id;

	id = tw9900_read_reg(client, TW9900_CHIP_ID);

	if (id != TW9900_CHIP_ID) {
		dev_err(dev, "Unexpected decoder id(%04x)\n", id);
		return -EINVAL;
	}

	dev_info(dev, "Detected TW9900 (%04x) decoder\n", TW9900_CHIP_ID);

	return 0;
}

static int tw9900_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct v4l2_ctrl_handler *hdl;
	struct tw9900 *tw9900;
	int ret = 0;

	tw9900 = devm_kzalloc(dev, sizeof(*tw9900), GFP_KERNEL);
	if (!tw9900)
		return -ENOMEM;

	tw9900->client = client;
	tw9900->cur_mode = &supported_modes[0];

	tw9900->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(tw9900->reset_gpio))
		tw9900->reset_gpio = NULL;

	tw9900->regulator = devm_regulator_get(&tw9900->client->dev, "vdd");
	if (IS_ERR(tw9900->regulator)) {
		dev_err(dev, "Failed to get power regulator\n");
		return ret;
	}

	v4l2_i2c_subdev_init(&tw9900->subdev, client, &tw9900_subdev_ops);
	tw9900->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				V4L2_SUBDEV_FL_HAS_EVENTS;

	hdl = &tw9900->hdl;

	v4l2_ctrl_handler_init(hdl, 2);

	v4l2_ctrl_new_std(hdl, &tw9900_ctrl_ops, V4L2_CID_BRIGHTNESS,
			  -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &tw9900_ctrl_ops, V4L2_CID_CONTRAST,
			  0, 255, 1, 0x60);

	tw9900->subdev.ctrl_handler = hdl;
	if (hdl->error) {
		int err = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return err;
	}

	ret = tw9900_power_on(tw9900);
	if (ret)
		return ret;

	ret = tw9900_check_id(tw9900, client);
	if (ret)
		goto err_power_off;

	tw9900->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	tw9900->pad.flags = MEDIA_PAD_FL_SOURCE;
	tw9900->subdev.entity.function = MEDIA_ENT_F_DV_DECODER;

	ret = media_entity_pads_init(&tw9900->subdev.entity, 1, &tw9900->pad);
	if (ret < 0)
		goto err_power_off;

	ret = v4l2_async_register_subdev(&tw9900->subdev);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
	media_entity_cleanup(&tw9900->subdev.entity);
err_power_off:
	tw9900_power_off(tw9900);

	return ret;
}

static int tw9900_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tw9900 *tw9900 = to_tw9900(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		tw9900_power_off(tw9900);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct of_device_id tw9900_of_match[] = {
	{ .compatible = "techwell,tw9900" },
	{},
};
MODULE_DEVICE_TABLE(of, tw9900_of_match);

static struct i2c_driver tw9900_i2c_driver = {
	.driver = {
		.name		= "tw9900",
		.pm		= &tw9900_pm_ops,
		.of_match_table	= tw9900_of_match
	},
	.probe	= tw9900_probe,
	.remove	= tw9900_remove,
};

module_i2c_driver(tw9900_i2c_driver);

MODULE_DESCRIPTION("tw9900 decoder driver");
MODULE_LICENSE("GPL v2");
