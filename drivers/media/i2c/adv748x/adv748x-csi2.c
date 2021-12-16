// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Analog Devices ADV748X CSI-2 Transmitter
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 */

#include <linux/module.h>
#include <linux/mutex.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include "adv748x.h"

/* Describes a format bit depth and CSI-2 defined Data Type. */
struct adv748x_csi2_format_info {
	u8 dt;
	u8 bpp;
};

static int adv748x_csi2_get_format_info(struct adv748x_csi2 *tx,
					u32 mbus_code,
					struct adv748x_csi2_format_info *fmt)
{
	switch (mbus_code) {
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV8_2X8:
		fmt->dt = 0x1e;
		fmt->bpp = 16;
		break;
	case MEDIA_BUS_FMT_YUYV10_2X10:
	case MEDIA_BUS_FMT_YUYV10_1X20:
		fmt->dt = 0x1f;
		fmt->bpp = 20;
		break;
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
	case MEDIA_BUS_FMT_RGB565_2X8_BE:
		fmt->dt = 0x22;
		fmt->bpp = 16;
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
		fmt->dt = 0x23;
		fmt->bpp = 18;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		fmt->dt = 0x24;
		fmt->bpp = 24;
		break;
	default:
		dev_err(tx->state->dev,
			"Unsupported media bus code: %u\n", mbus_code);
		return -EINVAL;
	}

	return 0;
}

int adv748x_csi2_set_virtual_channel(struct adv748x_csi2 *tx, unsigned int vc)
{
	return tx_write(tx, ADV748X_CSI_VC_REF, vc << ADV748X_CSI_VC_REF_SHIFT);
}

/**
 * adv748x_csi2_register_link : Register and link internal entities
 *
 * @tx: CSI2 private entity
 * @v4l2_dev: Video registration device
 * @src: Source subdevice to establish link
 * @src_pad: Pad number of source to link to this @tx
 * @enable: Link enabled flag
 *
 * Ensure that the subdevice is registered against the v4l2_device, and link the
 * source pad to the sink pad of the CSI2 bus entity.
 */
static int adv748x_csi2_register_link(struct adv748x_csi2 *tx,
				      struct v4l2_device *v4l2_dev,
				      struct v4l2_subdev *src,
				      unsigned int src_pad,
				      bool enable)
{
	int ret;

	if (!src->v4l2_dev) {
		ret = v4l2_device_register_subdev(v4l2_dev, src);
		if (ret)
			return ret;
	}

	ret = media_create_pad_link(&src->entity, src_pad,
				    &tx->sd.entity, ADV748X_CSI2_SINK,
				    enable ? MEDIA_LNK_FL_ENABLED : 0);
	if (ret)
		return ret;

	if (enable)
		tx->src = src;

	return 0;
}

/* -----------------------------------------------------------------------------
 * v4l2_subdev_internal_ops
 *
 * We use the internal registered operation to be able to ensure that our
 * incremental subdevices (not connected in the forward path) can be registered
 * against the resulting video path and media device.
 */

static int adv748x_csi2_registered(struct v4l2_subdev *sd)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);
	struct adv748x_state *state = tx->state;
	int ret;

	adv_dbg(state, "Registered %s (%s)", is_txa(tx) ? "TXA":"TXB",
			sd->name);

	/*
	 * Link TXA to AFE and HDMI, and TXB to AFE only as TXB cannot output
	 * HDMI.
	 *
	 * The HDMI->TXA link is enabled by default, as is the AFE->TXB one.
	 */
	if (is_afe_enabled(state)) {
		ret = adv748x_csi2_register_link(tx, sd->v4l2_dev,
						 &state->afe.sd,
						 ADV748X_AFE_SOURCE,
						 is_txb(tx));
		if (ret)
			return ret;

		/* TXB can output AFE signals only. */
		if (is_txb(tx))
			state->afe.tx = tx;
	}

	/* Register link to HDMI for TXA only. */
	if (is_txb(tx) || !is_hdmi_enabled(state))
		return 0;

	ret = adv748x_csi2_register_link(tx, sd->v4l2_dev, &state->hdmi.sd,
					 ADV748X_HDMI_SOURCE, true);
	if (ret)
		return ret;

	/* The default HDMI output is TXA. */
	state->hdmi.tx = tx;

	return 0;
}

static const struct v4l2_subdev_internal_ops adv748x_csi2_internal_ops = {
	.registered = adv748x_csi2_registered,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_video_ops
 */

static int adv748x_csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);
	struct v4l2_subdev *src;

	src = adv748x_get_remote_sd(&tx->pads[ADV748X_CSI2_SINK]);
	if (!src)
		return -EPIPE;

	return v4l2_subdev_call(src, video, s_stream, enable);
}

static const struct v4l2_subdev_video_ops adv748x_csi2_video_ops = {
	.s_stream = adv748x_csi2_s_stream,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 *
 * The CSI2 bus pads are ignorant to the data sizes or formats.
 * But we must support setting the pad formats for format propagation.
 */

static int adv748x_csi2_init_cfg(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state)
{
	/* One route for each virtual channel. Route 0 enabled by default. */
	struct v4l2_subdev_route routes[ADV748X_CSI2_STREAMS] = {
		{
			.sink_pad = ADV748X_CSI2_SINK,
			.sink_stream = 0,
			.source_pad = ADV748X_CSI2_SOURCE,
			.source_stream = 0,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		},
		{
			.sink_pad = ADV748X_CSI2_SINK,
			.sink_stream = 0,
			.source_pad = ADV748X_CSI2_SOURCE,
			.source_stream = 1,
		},
		{
			.sink_pad = ADV748X_CSI2_SINK,
			.sink_stream = 0,
			.source_pad = ADV748X_CSI2_SOURCE,
			.source_stream = 2,
		},
		{
			.sink_pad = ADV748X_CSI2_SINK,
			.sink_stream = 0,
			.source_pad = ADV748X_CSI2_SOURCE,
			.source_stream = 3,
		},
	};
	struct v4l2_subdev_krouting routing;
	int ret;

	routing.num_routes = ADV748X_CSI2_STREAMS;
	routing.routes = routes;

	v4l2_subdev_lock_state(state);
	ret = v4l2_subdev_set_routing(sd, state, &routing);
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int adv748x_csi2_set_format(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_format *sdformat)
{
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	/* Do not allow to set format on the multiplexed source pad. */
	if (sdformat->pad == ADV748X_CSI2_SOURCE)
		return -EINVAL;

	v4l2_subdev_lock_state(sd_state);
	fmt = v4l2_state_get_stream_format(sd_state, sdformat->pad,
					   sdformat->stream);
	if (!fmt) {
		ret = -EINVAL;
		goto out;
	};
	*fmt = sdformat->format;

	/* Propagate format to the other end of the route. */
	fmt = v4l2_subdev_state_get_opposite_stream_format(sd_state, sdformat->pad,
							   sdformat->stream);
	if (!fmt) {
		ret = -EINVAL;
		goto out;
	}
	*fmt = sdformat->format;

out:
	v4l2_subdev_unlock_state(sd_state);

	return ret;
}

static int adv748x_csi2_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
					struct v4l2_mbus_config *config)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);

	if (pad != ADV748X_CSI2_SOURCE)
		return -EINVAL;

	config->type = V4L2_MBUS_CSI2_DPHY;
	switch (tx->active_lanes) {
	case 1:
		config->flags = V4L2_MBUS_CSI2_1_LANE;
		break;

	case 2:
		config->flags = V4L2_MBUS_CSI2_2_LANE;
		break;

	case 3:
		config->flags = V4L2_MBUS_CSI2_3_LANE;
		break;

	case 4:
		config->flags = V4L2_MBUS_CSI2_4_LANE;
		break;
	}

	return 0;
}

static int adv748x_csi2_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				       struct v4l2_mbus_frame_desc *fd)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);
	struct adv748x_csi2_format_info info = {};
	struct v4l2_mbus_frame_desc_entry *entry;
	struct v4l2_subdev_route *route;
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	if (pad != ADV748X_CSI2_SOURCE)
		return -EINVAL;

	state = v4l2_subdev_lock_active_state(sd);

	/* A single route is available. */
	route = &state->routing.routes[0];
	fmt = v4l2_state_get_stream_format(state, pad, route->source_stream);
	if (!fmt) {
		ret = -EINVAL;
		goto out;
	}

	ret = adv748x_csi2_get_format_info(tx, fmt->code, &info);
	if (ret)
		goto out;

	memset(fd, 0, sizeof(*fd));

	/* A single stream is available. */
	fd->num_entries = 1;
	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;

	entry = &fd->entry[0];
	entry->stream = 0;
	entry->flags = V4L2_MBUS_FRAME_DESC_FL_LEN_MAX;
	entry->length = fmt->width * fmt->height * info.bpp / 8;
	entry->pixelcode = fmt->code;
	entry->bus.csi2.vc = route->source_stream;
	entry->bus.csi2.dt = info.dt;

out:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static const struct v4l2_subdev_pad_ops adv748x_csi2_pad_ops = {
	.init_cfg = adv748x_csi2_init_cfg,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = adv748x_csi2_set_format,
	.get_mbus_config = adv748x_csi2_get_mbus_config,
	.get_frame_desc = adv748x_csi2_get_frame_desc,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_ops
 */

static const struct v4l2_subdev_ops adv748x_csi2_ops = {
	.video = &adv748x_csi2_video_ops,
	.pad = &adv748x_csi2_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Subdev module and controls
 */

int adv748x_csi2_set_pixelrate(struct v4l2_subdev *sd, s64 rate)
{
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);

	if (!tx->pixel_rate)
		return -EINVAL;

	return v4l2_ctrl_s_ctrl_int64(tx->pixel_rate, rate);
}

static int adv748x_csi2_s_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_PIXEL_RATE:
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops adv748x_csi2_ctrl_ops = {
	.s_ctrl = adv748x_csi2_s_ctrl,
};

static int adv748x_csi2_init_controls(struct adv748x_csi2 *tx)
{

	v4l2_ctrl_handler_init(&tx->ctrl_hdl, 1);

	tx->pixel_rate = v4l2_ctrl_new_std(&tx->ctrl_hdl,
					   &adv748x_csi2_ctrl_ops,
					   V4L2_CID_PIXEL_RATE, 1, INT_MAX,
					   1, 1);

	tx->sd.ctrl_handler = &tx->ctrl_hdl;
	if (tx->ctrl_hdl.error) {
		v4l2_ctrl_handler_free(&tx->ctrl_hdl);
		return tx->ctrl_hdl.error;
	}

	return v4l2_ctrl_handler_setup(&tx->ctrl_hdl);
}

int adv748x_csi2_init(struct adv748x_state *state, struct adv748x_csi2 *tx)
{
	int ret;

	if (!is_tx_enabled(tx))
		return 0;

	adv748x_subdev_init(&tx->sd, state, &adv748x_csi2_ops,
			    MEDIA_ENT_F_VID_IF_BRIDGE,
			    V4L2_SUBDEV_FL_MULTIPLEXED,
			    is_txa(tx) ? "txa" : "txb");

	/* Ensure that matching is based upon the endpoint fwnodes */
	tx->sd.fwnode = of_fwnode_handle(state->endpoints[tx->port]);

	/* Register internal ops for incremental subdev registration */
	tx->sd.internal_ops = &adv748x_csi2_internal_ops;

	tx->pads[ADV748X_CSI2_SINK].flags = MEDIA_PAD_FL_SINK;
	tx->pads[ADV748X_CSI2_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&tx->sd.entity, ADV748X_CSI2_NR_PADS,
				     tx->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(&tx->sd);
	if (ret)
		goto err_free_media;

	ret = adv748x_csi2_init_controls(tx);
	if (ret)
		goto err_free_state;

	ret = v4l2_async_register_subdev(&tx->sd);
	if (ret)
		goto err_free_ctrl;

	return 0;

err_free_ctrl:
	v4l2_ctrl_handler_free(&tx->ctrl_hdl);
err_free_state:
	v4l2_subdev_cleanup(&tx->sd);
err_free_media:
	media_entity_cleanup(&tx->sd.entity);

	return ret;
}

void adv748x_csi2_cleanup(struct adv748x_csi2 *tx)
{
	if (!is_tx_enabled(tx))
		return;

	v4l2_async_unregister_subdev(&tx->sd);
	v4l2_subdev_cleanup(&tx->sd);
	media_entity_cleanup(&tx->sd.entity);
	v4l2_ctrl_handler_free(&tx->ctrl_hdl);
}
