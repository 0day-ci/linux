// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Analog Devices ADDI9036 ToF camera sensor.
 *
 * Copyright (C) 2019-2020 Analog Devices, All Rights Reserved.
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define FW_FILE_NAME	"adi/addi9036-fw.bin"
#define ADDI_MAGIC	"ADDI9036"

struct addi9036_mode_info {
	u32 width;
	u32 height;
	u32 pixel_rate;
	u32 link_freq_idx;
};

struct addi9036_mode_fw_block {
	const struct reg_sequence *mode_regs;
	ssize_t regs_count;
};

struct addi9036_fw_header {
	unsigned char magic[8];
	__le32 modes_nr;
} __packed;

struct addi9036_mode_block {
	__le32 mode_id;
	__le32 size_bytes;
	__le16 data[];
} __packed;

struct addi9036 {
	struct regmap *regmap;
	struct device *dev;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_rect crop;

	const struct addi9036_mode_info *current_mode;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *link_freq;
	/* custom controls */
	struct v4l2_ctrl *set_operating_range;

	struct mutex lock;
	bool streaming;

	struct gpio_desc *rst_gpio;

	/* firmware blocks for each operating mode */
	struct addi9036_mode_fw_block *mode_fw_blocks;
	u8 curr_operating_mode;

	const struct firmware *fw;
};

static inline struct addi9036 *to_addi9036(struct v4l2_subdev *sd)
{
	return container_of(sd, struct addi9036, sd);
}

#define V4L2_CID_ADDI9036_OPERATING_MODE  (V4L2_CID_USER_ADDI9036_BASE + 0)

static const struct reg_sequence addi9036_powerup_setting[] = {
	{ 0xc4c0, 0x001c },
	{ 0xc4c3, 0x001c },
	{ 0xc4d7, 0x0000 },
	{ 0xc4d5, 0x0002 },
	{ 0xc4da, 0x0001 },
	{ 0xc4f0, 0x0000 },
	{ 0xc427, 0x0003 },
	{ 0xc427, 0x0001 },
	{ 0xc427, 0x0000 },
	{ 0xc426, 0x0030 },
	{ 0xc426, 0x0010 },
	{ 0xc426, 0x0000 },
	{ 0xc423, 0x0080 },
	{ 0xc431, 0x0080 },
	{ 0x4001, 0x0007 },
	{ 0x7c22, 0x0004 }
};

static const struct reg_sequence addi9036_powerdown_setting[] = {
	{ 0xc022, 0x0001 },
	{ 0x4001, 0x0006 },
	{ 0x7c22, 0x0004 },
	{ 0xc431, 0x0082 },
	{ 0xc423, 0x0000 },
	{ 0xc426, 0x0020 },
	{ 0xc427, 0x0002 },
	{ 0xc4c0, 0x003c },
	{ 0xc4c3, 0x003c },
	{ 0xc4d5, 0x0003 },
	{ 0xc4da, 0x0000 },
	{ 0xc4d7, 0x0001 },
	{ 0xc4f0, 0x0001 }
};

static const struct reg_sequence addi9036_standby_setting[] = {
	{ 0xc020, 0x0000 },
	{ 0xc024, 0x0000 },
	{ 0xc028, 0x0000 },
	{ 0xc022, 0x0001 }
};

static const s64 link_freq_tbl[] = {
	110529000,
	221184000,
};

/* Elements of the structure must be ordered ascending by width & height */
static const struct addi9036_mode_info addi9036_mode_info_data[] = {
	{
		.width = 640,
		.height = 480,
		.pixel_rate = 36864000,
		.link_freq_idx = 0 /* an index in link_freq_tbl[] */
	},
	{
		.width = 640,
		.height = 960,
		.pixel_rate = 73728000,
		.link_freq_idx = 1 /* an index in link_freq_tbl[] */
	},
};

static bool addi9306_readable_register(struct device *dev, unsigned int reg)
{
	if (((reg >= 0x4000) && (reg <= 0x6fff)) ||
	    ((reg >= 0x7c00) && (reg <= 0x7fff)) ||
	    ((reg >= 0xc000) && (reg <= 0xc200)) ||
	    ((reg >= 0xc300) && (reg <= 0xc6bf)))
		return true;
	else
		return false;
}

static bool addi9306_writeable_register(struct device *dev, unsigned int reg)
{
	if (((reg >= 0x4000) && (reg <= 0x6fff)) ||
	    ((reg >= 0x7c00) && (reg <= 0x7fff)) ||
	    ((reg >= 0xc000) && (reg <= 0xc113)) ||
	    ((reg >= 0xc300) && (reg <= 0xc7ff)))
		return true;
	else
		return false;
}

static const struct regmap_config addi9036_i2c_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = 0xc7ff,
	.writeable_reg = addi9306_writeable_register,
	.readable_reg = addi9306_readable_register,
	.cache_type = REGCACHE_NONE,
};

static int addi9036_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct addi9036 *addi9036 = to_addi9036(sd);
	int ret;

	if (addi9036->rst_gpio)
		gpiod_set_value_cansleep(addi9036->rst_gpio, 0);

	ret = regmap_register_patch(addi9036->regmap, addi9036_powerup_setting,
				    ARRAY_SIZE(addi9036_powerup_setting));
	if (ret)
		dev_err(addi9036->dev, "Could not set power up registers\n");

	return ret;
}

static int addi9036_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct addi9036 *addi9036 = to_addi9036(sd);
	int ret;

	ret = regmap_register_patch(addi9036->regmap,
				    addi9036_powerdown_setting,
				    ARRAY_SIZE(addi9036_powerdown_setting));
	if (ret)
		dev_err(addi9036->dev, "could not set power down registers\n");

	if (addi9036->rst_gpio)
		gpiod_set_value_cansleep(addi9036->rst_gpio, 1);

	return ret;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int addi9036_g_register(struct v4l2_subdev *sd,
			       struct v4l2_dbg_register *reg)
{
	struct addi9036 *addi9036 = to_addi9036(sd);
	unsigned int read_val;
	int ret;

	reg->size = 2;
	ret = regmap_read(addi9036->regmap, reg->reg, &read_val);
	reg->val = read_val;

	return ret;
}

static int addi9036_s_register(struct v4l2_subdev *sd,
			       const struct v4l2_dbg_register *reg)
{
	struct addi9036 *addi9036 = to_addi9036(sd);

	return regmap_write(addi9036->regmap, reg->reg, reg->val);
}
#endif

static int addi9036_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct addi9036 *addi9036 = container_of(ctrl->handler,
						 struct addi9036, ctrls);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_ADDI9036_OPERATING_MODE:
		addi9036->curr_operating_mode = ctrl->val;
		break;
	case V4L2_CID_PIXEL_RATE:
	case V4L2_CID_LINK_FREQ:
		break;
	default:
		dev_err(addi9036->dev, "%s > Unhandled: %x  param=%x\n",
			__func__, ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops addi9036_ctrl_ops = {
	.s_ctrl = addi9036_s_ctrl,
};

static const struct v4l2_ctrl_config addi9036_ctrl_operating_mode = {
	.ops		= &addi9036_ctrl_ops,
	.id		= V4L2_CID_ADDI9036_OPERATING_MODE,
	.name		= "Operating Mode",
	.type		= V4L2_CTRL_TYPE_INTEGER,
	.def		= 0,
	.min		= 0,
	.max		= 1,
	.step		= 1,
};

static int addi9036_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR12_1X12;

	return 0;
}

static int addi9036_enum_frame_size(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->code != MEDIA_BUS_FMT_SBGGR12_1X12)
		return -EINVAL;

	if (fse->index >= ARRAY_SIZE(addi9036_mode_info_data))
		return -EINVAL;

	fse->min_width = addi9036_mode_info_data[fse->index].width;
	fse->max_width = addi9036_mode_info_data[fse->index].width;
	fse->min_height = addi9036_mode_info_data[fse->index].height;
	fse->max_height = addi9036_mode_info_data[fse->index].height;

	return 0;
}

static struct v4l2_mbus_framefmt *
addi9036_get_pad_format(struct addi9036 *addi9036,
			struct v4l2_subdev_pad_config *cfg, unsigned int pad,
			enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&addi9036->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &addi9036->fmt;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static int addi9036_get_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *format)
{
	struct addi9036 *addi9036 = to_addi9036(sd);
	struct v4l2_mbus_framefmt *pad_format;

	pad_format = addi9036_get_pad_format(addi9036, cfg, format->pad,
					     format->which);
	if (IS_ERR(pad_format))
		return PTR_ERR(pad_format);

	format->format = *pad_format;

	return 0;
}

static struct v4l2_rect *
addi9036_get_pad_crop(struct addi9036 *addi9036,
		      struct v4l2_subdev_pad_config *cfg,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&addi9036->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &addi9036->crop;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static int addi9036_set_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *format)
{
	struct addi9036 *addi9036 = to_addi9036(sd);
	struct v4l2_mbus_framefmt *framefmt;
	struct v4l2_rect *crop;
	const struct addi9036_mode_info *new_mode;
	int ret;

	dev_dbg(addi9036->dev, "set_fmt: %x %dx%d %d\n",
		format->format.code, format->format.width,
		format->format.height, format->which);

	mutex_lock(&addi9036->lock);

	crop = addi9036_get_pad_crop(addi9036, cfg, format->pad,
				     format->which);
	if (IS_ERR(crop))
		return PTR_ERR(crop);

	new_mode = v4l2_find_nearest_size(addi9036_mode_info_data,
					  ARRAY_SIZE(addi9036_mode_info_data),
					  width, height, format->format.width,
					  format->format.height);
	crop->width = new_mode->width;
	crop->height = new_mode->height;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ret = v4l2_ctrl_s_ctrl_int64(addi9036->pixel_rate,
					     new_mode->pixel_rate);
		if (ret < 0)
			return ret;

		ret = v4l2_ctrl_s_ctrl(addi9036->link_freq,
				       new_mode->link_freq_idx);
		if (ret < 0)
			return ret;

		addi9036->current_mode = new_mode;
	}

	framefmt = addi9036_get_pad_format(addi9036, cfg, format->pad,
					   format->which);
	if (IS_ERR(framefmt))
		return PTR_ERR(framefmt);

	framefmt->width = crop->width;
	framefmt->height = crop->height;
	framefmt->code = MEDIA_BUS_FMT_SBGGR12_1X12;
	framefmt->field = V4L2_FIELD_NONE;
	framefmt->colorspace = V4L2_COLORSPACE_SRGB;

	format->format = *framefmt;

	mutex_unlock(&addi9036->lock);

	return 0;
}

static int addi9036_entity_init_cfg(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_subdev_format fmt = { 0 };

	fmt.which = cfg ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.width = addi9036_mode_info_data[1].width;
	fmt.format.height = addi9036_mode_info_data[1].height;

	addi9036_set_format(subdev, cfg, &fmt);

	return 0;
}

static int addi9036_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_selection *sel)
{
	struct addi9036 *addi9036 = to_addi9036(sd);
	struct v4l2_rect *crop;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	crop = addi9036_get_pad_crop(addi9036, cfg, sel->pad, sel->which);
	if (IS_ERR(crop))
		return PTR_ERR(crop);

	sel->r = *crop;

	return 0;
}

static int addi9036_start_streaming(struct addi9036 *addi9036)
{
	uint8_t mode = addi9036->curr_operating_mode;

	if (addi9036->mode_fw_blocks[mode].mode_regs == NULL) {
		dev_err(addi9036->dev, "Selected mode has no data\n");
		return -EINVAL;
	}

	return regmap_multi_reg_write(addi9036->regmap,
			addi9036->mode_fw_blocks[mode].mode_regs,
			addi9036->mode_fw_blocks[mode].regs_count);
}

static int addi9036_stop_streaming(struct addi9036 *addi9036)
{
	return regmap_register_patch(addi9036->regmap,
				     addi9036_standby_setting,
				     ARRAY_SIZE(addi9036_standby_setting));
}

static int addi9036_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct addi9036 *addi9036 = to_addi9036(subdev);
	int ret = 0;

	dev_dbg(addi9036->dev, "s_stream: %d\n", enable);

	mutex_lock(&addi9036->lock);
	if (addi9036->streaming == enable) {
		mutex_unlock(&addi9036->lock);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(addi9036->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(addi9036->dev);
			goto err_unlock;
		}

		ret = addi9036_start_streaming(addi9036);
		if (ret)
			goto err_rpm_put;
	} else {
		addi9036_stop_streaming(addi9036);
		pm_runtime_put(addi9036->dev);
	}

	addi9036->streaming = enable;
	mutex_unlock(&addi9036->lock);

	return ret;

err_rpm_put:
	pm_runtime_put(addi9036->dev);
err_unlock:
	mutex_unlock(&addi9036->lock);

	return ret;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static const struct v4l2_subdev_core_ops addi9036_core_ops = {
	.g_register	= addi9036_g_register,
	.s_register	= addi9036_s_register,
};
#endif

static const struct dev_pm_ops addi9036_pm_ops = {
	SET_RUNTIME_PM_OPS(addi9036_power_on, addi9036_power_off, NULL)
};

static const struct v4l2_subdev_video_ops addi9036_video_ops = {
	.s_stream	= addi9036_s_stream,
};

static const struct v4l2_subdev_pad_ops addi9036_subdev_pad_ops = {
	.init_cfg		= addi9036_entity_init_cfg,
	.enum_mbus_code		= addi9036_enum_mbus_code,
	.enum_frame_size	= addi9036_enum_frame_size,
	.get_fmt		= addi9036_get_format,
	.set_fmt		= addi9036_set_format,
	.get_selection		= addi9036_get_selection,
};

static const struct v4l2_subdev_ops addi9036_subdev_ops = {
	.core	= &addi9036_core_ops,
	.video	= &addi9036_video_ops,
	.pad	= &addi9036_subdev_pad_ops,
};

static int addi9036_g_modes_from_firmware(struct v4l2_subdev *sd)
{
	struct addi9036 *addi9036 = to_addi9036(sd);
	const struct firmware *fw = addi9036->fw;
	const struct addi9036_fw_header *fw_head;
	const struct addi9036_mode_block *mode_block;
	unsigned int reg_nr, chunk_len, pos, modes_nr, i, j, k;
	struct reg_sequence *reg_seq;

	if (fw->size < sizeof(struct addi9036_fw_header) ||
	    fw->size >= 0x4000000)
		goto err_size;

	fw_head = (struct addi9036_fw_header *)fw->data;

	if (memcmp(fw_head->magic, ADDI_MAGIC, ARRAY_SIZE(fw_head->magic))) {
		dev_err(addi9036->dev, "FW loading failed: Invalid magic\n");
		return -EINVAL;
	}

	modes_nr = le32_to_cpu(fw_head->modes_nr);
	if (modes_nr == 0) {
		dev_err(addi9036->dev, "FW should contain at least 1 mode.\n");
		return -EINVAL;
	}

	__v4l2_ctrl_modify_range(addi9036->set_operating_range,
				 addi9036->set_operating_range->minimum,
				 modes_nr - 1, 1, 0);

	addi9036->mode_fw_blocks = devm_kcalloc(addi9036->dev, modes_nr,
			     sizeof(struct addi9036_mode_fw_block), GFP_KERNEL);
	if (!addi9036->mode_fw_blocks)
		return -ENOMEM;

	pos = sizeof(struct addi9036_fw_header);

	for (i = 0; i < modes_nr; i++) {
		if (fw->size < pos + sizeof(struct addi9036_mode_block))
			goto err_size;

		mode_block = (struct addi9036_mode_block *)(fw->data + pos);
		chunk_len = le32_to_cpu(mode_block->size_bytes);
		pos += sizeof(struct addi9036_mode_block);

		if (fw->size < pos + chunk_len)
			goto err_size;

		reg_nr = chunk_len / sizeof(uint16_t) / 2;
		reg_seq = devm_kcalloc(addi9036->dev, reg_nr,
				       sizeof(struct reg_sequence), GFP_KERNEL);
		if (!reg_seq)
			return -ENOMEM;

		for (j = 0, k = 0; j < reg_nr * 2; j += 2, k++) {
			reg_seq[k].reg = le16_to_cpu(mode_block->data[j]);
			reg_seq[k].def = le16_to_cpu(mode_block->data[j + 1]);
		}

		addi9036->mode_fw_blocks[i].mode_regs = reg_seq;
		addi9036->mode_fw_blocks[i].regs_count = reg_nr;

		pos += chunk_len;
	}
	return 0;

err_size:
	dev_err(addi9036->dev, "FW loading failed: Invalid size\n");
	return -EINVAL;
}

static int addi9036_mode_firmware_load(struct v4l2_subdev *sd)
{
	struct addi9036 *addi9036 = to_addi9036(sd);
	int ret;

	ret = request_firmware(&addi9036->fw, FW_FILE_NAME, addi9036->dev);
	if (ret < 0) {
		dev_err(addi9036->dev, "FW request failed\n");
		return ret;
	}

	ret = addi9036_g_modes_from_firmware(sd);

	release_firmware(addi9036->fw);
	if (ret < 0) {
		dev_err(addi9036->dev, "FW parsing failed\n");
		return ret;
	}

	return 0;
}

static int addi9036_probe(struct i2c_client *client)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct addi9036 *addi9036;
	int ret;

	dev_dbg(dev, "%s: i2c addr = 0x%x\n", __func__, client->addr);

	addi9036 = devm_kzalloc(dev, sizeof(struct addi9036), GFP_KERNEL);
	if (!addi9036)
		return -ENOMEM;

	addi9036->dev = dev;

	addi9036->regmap = devm_regmap_init_i2c(client,
						&addi9036_i2c_regmap_config);
	if (IS_ERR(addi9036->regmap)) {
		dev_err(dev, "Error initializing i2c regmap\n");
		return PTR_ERR(addi9036->regmap);
	}

	mutex_init(&addi9036->lock);

	endpoint = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &bus_cfg);
	fwnode_handle_put(endpoint);
	if (ret < 0) {
		dev_err(dev, "parsing endpoint node failed\n");
		return ret;
	}

	addi9036->rst_gpio = gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(addi9036->rst_gpio))
		dev_info(dev, "Unable to get \"reset\" gpio\n");

	v4l2_ctrl_handler_init(&addi9036->ctrls, 3);

	addi9036->pixel_rate = v4l2_ctrl_new_std(&addi9036->ctrls,
						  &addi9036_ctrl_ops,
						  V4L2_CID_PIXEL_RATE,
						  1, INT_MAX, 1, 1);
	addi9036->link_freq = v4l2_ctrl_new_int_menu(&addi9036->ctrls,
						     &addi9036_ctrl_ops,
						     V4L2_CID_LINK_FREQ,
						     ARRAY_SIZE(
							     link_freq_tbl) - 1,
						     0, link_freq_tbl);
	if (addi9036->link_freq)
		addi9036->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	addi9036->set_operating_range = v4l2_ctrl_new_custom(&addi9036->ctrls,
						&addi9036_ctrl_operating_mode,
						NULL);

	ret = addi9036->ctrls.error;
	if (ret) {
		dev_err(dev, "%s: control initialization error %d\n",
			__func__, ret);
		goto free_ctrl;
	}
	addi9036->sd.ctrl_handler = &addi9036->ctrls;

	v4l2_i2c_subdev_init(&addi9036->sd, client, &addi9036_subdev_ops);
	addi9036->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	addi9036->pad.flags = MEDIA_PAD_FL_SOURCE;
	addi9036->sd.dev = &client->dev;
	addi9036->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&addi9036->sd.entity, 1, &addi9036->pad);
	if (ret < 0) {
		dev_err(dev, "could not register media entity\n");
		goto free_ctrl;
	}

	ret = addi9036_mode_firmware_load(&addi9036->sd);
	if (ret < 0)
		return ret;

	addi9036_entity_init_cfg(&addi9036->sd, NULL);

	ret = v4l2_async_register_subdev(&addi9036->sd);
	if (ret < 0) {
		dev_err(dev, "could not register v4l2 device\n");
		goto free_entity;
	}

	ret = addi9036_power_on(dev);
	if (ret < 0) {
		dev_err(dev, "Could not power on the device\n");
		goto free_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

free_entity:
	media_entity_cleanup(&addi9036->sd.entity);
free_ctrl:
	v4l2_ctrl_handler_free(&addi9036->ctrls);
	mutex_destroy(&addi9036->lock);

	return ret;
}

static int addi9036_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct addi9036 *addi9036 = to_addi9036(sd);

	v4l2_async_unregister_subdev(&addi9036->sd);
	media_entity_cleanup(&addi9036->sd.entity);
	if (addi9036->rst_gpio)
		gpiod_put(addi9036->rst_gpio);
	v4l2_ctrl_handler_free(&addi9036->ctrls);
	mutex_destroy(&addi9036->lock);

	pm_runtime_disable(addi9036->dev);
	if (!pm_runtime_status_suspended(addi9036->dev))
		addi9036_power_off(addi9036->dev);
	pm_runtime_set_suspended(addi9036->dev);

	return 0;
}

static const struct of_device_id addi9036_of_match[] = {
	{ .compatible = "adi,addi9036" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, addi9036_of_match);

static struct i2c_driver addi9036_i2c_driver = {
	.driver			= {
		.of_match_table = addi9036_of_match,
		.name		= "addi9036",
		.pm		= &addi9036_pm_ops,
	},
	.probe_new		= addi9036_probe,
	.remove			= addi9036_remove,
};

module_i2c_driver(addi9036_i2c_driver);

MODULE_DESCRIPTION("Analog Devices ADDI9036 Camera Driver");
MODULE_AUTHOR("Bogdan Togorean");
MODULE_LICENSE("GPL v2");
