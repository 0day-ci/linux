// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera ISP driver.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/freezer.h>
#include <linux/keembay-isp-ctl.h>
#include <linux/kthread.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "keembay-isp.h"
#include "keembay-cam-xlink.h"
#include "keembay-vpu-cmd.h"
#include "keembay-vpu-pipe.h"
#include "keembay-vpu-src.h"

#define KMB_ISP_DRV_NAME "keembay-camera-isp"

/* Xlink channel configuration */
#define KMB_ISP_CH_DATA_SIZE	1024
#define KMB_ISP_CH_TIMEOUT_MS	5000

/* Predefined event queue length */
#define KMB_ISP_EVT_Q_LEN	8

/* Wait timeout for stopping isp source */
#define KMB_STOP_SOURCE_TIMEOUT_MS	1000

enum kmb_isp_stop_method {
	KMB_ISP_STOP_SYNC = 0,
	KMB_ISP_STOP_FORCE = 1,
};

static const struct kmb_isp_source_format source_fmts[] = {
	{
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_RGGB,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_RAW_8,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_GRBG,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_RAW_8,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_GBRG,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_RAW_8,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_RAW_8,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_RGGB,
		.bpp = 10,
		.rx_data_type = IC_IPIPE_RAW_10,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_GRBG,
		.bpp = 10,
		.rx_data_type = IC_IPIPE_RAW_10,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_GBRG,
		.bpp = 10,
		.rx_data_type = IC_IPIPE_RAW_10,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 10,
		.rx_data_type = IC_IPIPE_RAW_10,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_RGGB,
		.bpp = 12,
		.rx_data_type = IC_IPIPE_RAW_12,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_GRBG,
		.bpp = 12,
		.rx_data_type = IC_IPIPE_RAW_12,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_GBRG,
		.bpp = 12,
		.rx_data_type = IC_IPIPE_RAW_12,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 12,
		.rx_data_type = IC_IPIPE_RAW_12,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_YUYV8_1_5X8,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_YUV_420_B8,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_UYYVYY8_0_5X24,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_YUV_420_B8,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_YUV8_1X24,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_YUV_420_B8,
		.dest_fmts = {
			MEDIA_BUS_FMT_YUYV8_1_5X8,
			MEDIA_BUS_FMT_UYYVYY8_0_5X24,
			MEDIA_BUS_FMT_YUV8_1X24,
		},
	},
	{
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 8,
		.rx_data_type = IC_IPIPE_RAW_8,
		.dest_fmts = {
			MEDIA_BUS_FMT_Y8_1X8,
			MEDIA_BUS_FMT_Y10_1X10,
		},
	},
	{
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.bayer_pattern = KMB_IC_BAYER_FORMAT_BGGR,
		.bpp = 10,
		.rx_data_type = IC_IPIPE_RAW_10,
		.dest_fmts = {
			MEDIA_BUS_FMT_Y8_1X8,
			MEDIA_BUS_FMT_Y10_1X10,
		},
	},
};

static inline const struct kmb_isp_source_format *
kmb_isp_get_src_fmt_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(source_fmts); i++) {
		if (source_fmts[i].code == code)
			return &source_fmts[i];
	}

	return NULL;
}

static inline bool
kmb_isp_valid_destination_format(struct v4l2_mbus_framefmt *mbus_fmt, u32 code)
{
	const struct kmb_isp_source_format *src_fmt =
		kmb_isp_get_src_fmt_by_code(mbus_fmt->code);
	unsigned int i;

	if (!src_fmt)
		return false;

	for (i = 0; i < KMB_ISP_MAX_DEST_FMTS; i++)
		if (src_fmt->dest_fmts[i] == code)
			return true;

	return false;
}

static void kmb_isp_meta_buf_done(struct kmb_isp *kmb_isp,
				  struct kmb_metadata_buf *meta_buf,
				  enum vb2_buffer_state state)
{
	/* Remove isp config on buf done */
	mutex_lock(&kmb_isp->meta_q_lock);
	list_del(&meta_buf->list);
	mutex_unlock(&kmb_isp->meta_q_lock);

	vb2_buffer_done(&meta_buf->vb.vb2_buf, state);
	dev_dbg(kmb_isp->dev, "Meta buf done %u state %d",
		meta_buf->vb.sequence, state);
}

static struct kmb_metadata_buf *
kmb_isp_find_params_by_addr(struct kmb_isp *kmb_isp, dma_addr_t addr)
{
	struct kmb_metadata_buf *meta_buf;
	struct list_head *node = NULL;

	mutex_lock(&kmb_isp->meta_q_lock);

	list_for_each(node, &kmb_isp->meta_params_process_q) {
		meta_buf = list_entry(node, struct kmb_metadata_buf, list);
		if (meta_buf->params.dma_addr_isp == addr) {
			mutex_unlock(&kmb_isp->meta_q_lock);
			return meta_buf;
		}
	}

	mutex_unlock(&kmb_isp->meta_q_lock);

	return NULL;
}

static struct kmb_metadata_buf *
kmb_isp_find_stats_by_seq(struct kmb_isp *kmb_isp, u32 sequence)
{
	struct kmb_metadata_buf *meta_buf;
	struct list_head *node = NULL;

	mutex_lock(&kmb_isp->meta_q_lock);

	list_for_each(node, &kmb_isp->meta_stats_process_q) {
		meta_buf = list_entry(node, struct kmb_metadata_buf, list);
		if (meta_buf->vb.sequence == sequence) {
			mutex_unlock(&kmb_isp->meta_q_lock);
			return meta_buf;
		}
	}

	mutex_unlock(&kmb_isp->meta_q_lock);

	return NULL;
}

static void kmb_isp_fill_stats_update_flags(struct kmb_metadata_buf *stats_buf,
					    struct kmb_metadata_buf *param_buf)
{
	struct kmb_isp_stats *user_stats =
		vb2_plane_vaddr(&stats_buf->vb.vb2_buf, 0);

	user_stats->update.ae_awb =
		param_buf->params.isp->raw.awb_stats_en;
	user_stats->update.af =
		param_buf->params.isp->raw.af_stats_en;
	user_stats->update.luma_hist =
		param_buf->params.isp->raw.luma_hist_en;
	user_stats->update.rgb_hist =
		param_buf->params.isp->raw.awb_rgb_hist_en;
	user_stats->update.flicker_rows =
		param_buf->params.isp->raw.flicker_accum_en;
	/* Dehaze stats is always enabled */
	user_stats->update.dehaze = true;
}

static int kmb_isp_process_config(struct kmb_isp *kmb_isp)
{
	struct kmb_metadata_buf *param_buf;
	struct kmb_metadata_buf *stats_buf;
	struct kmb_ic_ev cfg_evt;
	int ret;

	mutex_lock(&kmb_isp->meta_q_lock);

	if (list_empty(&kmb_isp->meta_params_pending_q)) {
		mutex_unlock(&kmb_isp->meta_q_lock);
		return -EAGAIN;
	}
	param_buf = list_first_entry(&kmb_isp->meta_params_pending_q,
				     struct kmb_metadata_buf, list);

	if (list_empty(&kmb_isp->meta_stats_pending_q)) {
		mutex_unlock(&kmb_isp->meta_q_lock);
		return -EAGAIN;
	}
	stats_buf = list_first_entry(&kmb_isp->meta_stats_pending_q,
				     struct kmb_metadata_buf, list);

	list_del(&stats_buf->list);
	list_del(&param_buf->list);

	mutex_unlock(&kmb_isp->meta_q_lock);

	param_buf->vb.sequence = kmb_isp->sequence++;
	stats_buf->vb.sequence = param_buf->vb.sequence;

	/* Update header version, user data key and image width */
	param_buf->params.isp->header_version = KMB_VPU_ISP_ABI_VERSION;
	param_buf->params.isp->num_exposures = 1;
	param_buf->params.isp->user_data_key =
		param_buf->vb.sequence;
	param_buf->params.isp->image_data_width =
		kmb_isp->source_fmt->bpp;
	param_buf->params.isp->bayer_order =
		kmb_isp->source_fmt->bayer_pattern;

	/* Set stats addresses */
	memcpy(param_buf->params.isp->raw.stats, stats_buf->stats.raw,
	       sizeof(param_buf->params.isp->raw.stats));
	param_buf->params.isp->dehaze.stats_addr =
		stats_buf->stats.dehaze_stats_addr;

	memset(&cfg_evt, 0, sizeof(cfg_evt));
	cfg_evt.ctrl = KMB_IC_EVENT_TYPE_CONFIG_ISP;
	cfg_evt.ev_info.seq_nr = param_buf->vb.sequence;
	cfg_evt.ev_info.user_data_base_addr01 = param_buf->params.dma_addr_isp;
	dev_dbg(kmb_isp->dev, "Process config addr %llx",
		param_buf->params.dma_addr_isp);
	ret = kmb_cam_xlink_write_msg(kmb_isp->xlink_cam,
				      kmb_isp->config_chan_id,
				      (u8 *)&cfg_evt, sizeof(cfg_evt));
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Error on process config %d", ret);
		vb2_buffer_done(&param_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		vb2_buffer_done(&stats_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		return ret;
	}

	/* Update stats update flags */
	kmb_isp_fill_stats_update_flags(stats_buf, param_buf);

	/* Add to items to the processing list */
	mutex_lock(&kmb_isp->meta_q_lock);
	list_add_tail(&param_buf->list, &kmb_isp->meta_params_process_q);
	list_add_tail(&stats_buf->list, &kmb_isp->meta_stats_process_q);
	mutex_unlock(&kmb_isp->meta_q_lock);

	return 0;
}

static int kmb_isp_worker_thread(void *isp)
{
	struct kmb_metadata_buf *meta_params;
	struct kmb_metadata_buf *meta_stats;
	struct kmb_isp *kmb_isp = isp;
	struct v4l2_event v4l2_evt;
	struct kmb_ic_ev cfg_evt;
	bool stopped = false;
	u32 base_addr;
	int ret;

	memset(&v4l2_evt, 0, sizeof(v4l2_evt));

	set_freezable();

	while (!kthread_should_stop()) {
		try_to_freeze();

		if (stopped) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			continue;
		}

		memset(&cfg_evt, 0x00, sizeof(cfg_evt));
		cfg_evt.ctrl = KMB_IC_EVENT_MAX;
		ret = kmb_cam_xlink_read_msg(kmb_isp->xlink_cam,
					     kmb_isp->config_chan_id,
					     (u8 *)&cfg_evt, sizeof(cfg_evt));
		if (ret < 0) {
			stopped = true;
			complete_all(&kmb_isp->source_stopped);
			continue;
		}
		base_addr = cfg_evt.ev_info.user_data_base_addr01;

		meta_params = kmb_isp_find_params_by_addr(kmb_isp, base_addr);

		switch (cfg_evt.ctrl) {
		case KMB_IC_EVENT_TYPE_READOUT_START:
			if (meta_params) {
				v4l2_evt.type = V4L2_EVENT_FRAME_SYNC;
				v4l2_evt.u.frame_sync.frame_sequence =
					meta_params->vb.sequence;
				v4l2_subdev_notify_event(&kmb_isp->subdev,
							 &v4l2_evt);
			} else {
				dev_err(kmb_isp->dev, "Ouch readout no buf");
			}
			/* Process next isp configuration on readout start */
			kmb_isp_process_config(kmb_isp);
			break;
		case KMB_IC_EVENT_TYPE_ISP_END:
			if (meta_params)
				kmb_isp_meta_buf_done(kmb_isp, meta_params,
						      VB2_BUF_STATE_DONE);
			else
				dev_err(kmb_isp->dev, "Ouch no params buf");
			break;
		case KMB_IC_EVENT_TYPE_STATS_READY:
			meta_stats = NULL;
			if (meta_params)
				meta_stats =
					kmb_isp_find_stats_by_seq(kmb_isp,
								  meta_params->vb.sequence);

			if (meta_stats)
				kmb_isp_meta_buf_done(kmb_isp, meta_stats,
						      VB2_BUF_STATE_DONE);
			else
				dev_err(kmb_isp->dev, "Ouch no stats buf");
			break;
		case KMB_IC_ERROR_SRC_MIPI_CFG_SKIPPED:
			if (meta_params) {
				kmb_isp_meta_buf_done(kmb_isp, meta_params,
						      VB2_BUF_STATE_ERROR);
				meta_stats =
					kmb_isp_find_stats_by_seq(kmb_isp,
								  meta_params->vb.sequence);
				if (meta_stats)
					kmb_isp_meta_buf_done(kmb_isp,
							      meta_stats,
							      VB2_BUF_STATE_ERROR);
			}
			break;
		case KMB_IC_ERROR_SRC_MIPI_CFG_MISSING:
			/* Process new configuration when vpu is starving */
			kmb_isp_process_config(kmb_isp);
			break;
		case KMB_IC_EVENT_TYPE_SOURCE_STOPPED:
			complete_all(&kmb_isp->source_stopped);
			stopped = true;
			break;
		default:
			dev_dbg(kmb_isp->dev, "Received event %d",
				cfg_evt.ctrl);
			break;
		}
	}

	return 0;
}

static int kmb_isp_configure_vpu_source(struct kmb_isp *kmb_isp)
{
	struct kmb_ic_source_config *src_cfg;
	struct v4l2_subdev_format *src_fmt;
	struct kmb_ic_ev mipi_cfg_evt;
	struct v4l2_subdev *subdev;
	struct media_pad *rpd;
	s64 link_freq;
	int ret;

	if (WARN_ON(!kmb_isp->source_fmt))
		return -EINVAL;

	/* Get sensor remote pad we need information for pixel clock */
	rpd = media_entity_remote_pad(&kmb_isp->pads[KMB_ISP_SINK_PAD_SENSOR]);
	if (!rpd || !is_media_entity_v4l2_subdev(rpd->entity))
		return -EINVAL;

	subdev = media_entity_to_v4l2_subdev(rpd->entity);
	if (!subdev)
		return -EINVAL;

	src_cfg = kmb_isp->msg_vaddr;
	memset(src_cfg, 0, sizeof(*src_cfg));

	src_fmt = &kmb_isp->active_pad_fmt[KMB_ISP_SINK_PAD_SENSOR];

	/* Full size isp destination is always set on first src pad */
	src_cfg->camera_output_size.w = src_fmt->format.width;
	src_cfg->camera_output_size.h = src_fmt->format.height;
	src_cfg->no_exposure = 1;

	src_cfg->crop_window.x1 = 0;
	src_cfg->crop_window.x2 = src_cfg->camera_output_size.w;
	src_cfg->crop_window.y1 = 0;
	src_cfg->crop_window.y2 = src_cfg->camera_output_size.h;

	src_cfg->bayer_format = kmb_isp->source_fmt->bayer_pattern;
	src_cfg->bpp = kmb_isp->source_fmt->bpp;

	src_cfg->mipi_rx_data.no_controller = kmb_isp->csi2_config.rx_id;
	src_cfg->mipi_rx_data.data_mode = 1;
	src_cfg->mipi_rx_data.no_lanes = kmb_isp->csi2_config.num_lanes;
	src_cfg->mipi_rx_data.data_type = kmb_isp->source_fmt->rx_data_type;

	link_freq = v4l2_get_link_freq(subdev->ctrl_handler, src_cfg->bpp,
				       src_cfg->mipi_rx_data.no_lanes * 2);
	if (link_freq < 0)
		return link_freq;

	src_cfg->mipi_rx_data.lane_rate_mbps = link_freq * 2;

	src_cfg->metadata_width = src_fmt->format.width;
	src_cfg->metadata_height = 0;
	src_cfg->metadata_data_type = IC_IPIPE_EMBEDDED_8BIT;

	memset(&mipi_cfg_evt, 0, sizeof(mipi_cfg_evt));
	mipi_cfg_evt.ctrl = KMB_IC_EVENT_TYPE_CONFIG_SOURCE;
	mipi_cfg_evt.ev_info.user_data_base_addr01 = kmb_isp->msg_phy_addr;
	ret = kmb_cam_xlink_write_msg(kmb_isp->xlink_cam,
				      kmb_isp->config_chan_id,
				      (u8 *)&mipi_cfg_evt,
				      sizeof(mipi_cfg_evt));
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Error config source xlink msg %d", ret);
		return ret;
	}

	ret = kmb_cam_xlink_read_msg(kmb_isp->xlink_cam,
				     kmb_isp->config_chan_id,
				     (u8 *)&mipi_cfg_evt,
				     sizeof(mipi_cfg_evt));
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Error source xlink msg ack %d", ret);
		return ret;
	}
	if (mipi_cfg_evt.ctrl != KMB_IC_EVENT_TYPE_SOURCE_CONFIGURED) {
		dev_err(kmb_isp->dev, "Error source configured %d",
			mipi_cfg_evt.ctrl);
		return -EINVAL;
	}

	return 0;
}

static int kmb_isp_start_source(struct kmb_isp *kmb_isp)
{
	struct kmb_ic_ev cfg_evt;
	int ret;

	if (WARN_ON(kmb_isp->source_streaming))
		return -EINVAL;

	memset(&cfg_evt, 0, sizeof(cfg_evt));
	cfg_evt.ctrl = KMB_IC_EVENT_TYPE_START_SOURCE;
	cfg_evt.ev_info.inst_id = 0;
	ret = kmb_cam_xlink_write_msg(kmb_isp->xlink_cam,
				      kmb_isp->config_chan_id,
				      (u8 *)&cfg_evt, sizeof(cfg_evt));
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Error start source xlink msg %d", ret);
		return ret;
	}

	ret = kmb_cam_xlink_read_msg(kmb_isp->xlink_cam,
				     kmb_isp->config_chan_id,
				     (u8 *)&cfg_evt, sizeof(cfg_evt));
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Error start source msg ack %d", ret);
		return ret;
	}
	if (cfg_evt.ctrl != KMB_IC_EVENT_TYPE_SOURCE_STARTED) {
		dev_err(kmb_isp->dev, "Error source started ack %d",
			cfg_evt.ctrl);
		return -EINVAL;
	}

	init_completion(&kmb_isp->source_stopped);
	kmb_isp->thread = kthread_run(kmb_isp_worker_thread, kmb_isp,
				      "kmb_isp_thread");
	if (IS_ERR(kmb_isp->thread)) {
		ret = PTR_ERR(kmb_isp->thread);
		kmb_isp->thread = NULL;
		dev_err(kmb_isp->dev, "Thread run failed %d", ret);
		return ret;
	}

	kmb_isp->source_streaming = true;

	return 0;
}

static int kmb_isp_stop_source(struct kmb_isp *kmb_isp,
			       enum kmb_isp_stop_method method)
{
	struct kmb_ic_ev cfg_evt;
	unsigned long timeout;
	int ret;

	if (WARN_ON(!kmb_isp->source_streaming))
		return -EINVAL;

	switch (method) {
	case KMB_ISP_STOP_SYNC:
		memset(&cfg_evt, 0, sizeof(cfg_evt));
		cfg_evt.ctrl = KMB_IC_EVENT_TYPE_STOP_SOURCE;
		cfg_evt.ev_info.inst_id = 0;

		ret = kmb_cam_xlink_write_msg(kmb_isp->xlink_cam,
					      kmb_isp->config_chan_id,
					      (u8 *)&cfg_evt,
					      sizeof(cfg_evt));
		if (ret < 0) {
			dev_err(kmb_isp->dev,
				"Error stop source xlink msg %d", ret);
			return ret;
		}

		timeout = msecs_to_jiffies(KMB_STOP_SOURCE_TIMEOUT_MS);
		ret = wait_for_completion_timeout(&kmb_isp->source_stopped,
						  timeout);
		if (ret == 0) {
			dev_err(kmb_isp->dev, "Source stopped timeout");
			return -ETIMEDOUT;
		}
		break;
	case KMB_ISP_STOP_FORCE:
		/* Stop ISP without notifying VPU. */
		break;
	default:
		dev_err(kmb_isp->dev, "Invalid stop method %d", method);
		return -EINVAL;
	}

	ret = kthread_stop(kmb_isp->thread);
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Thread stop failed %d", ret);
		return ret;
	}
	kmb_isp->thread = NULL;

	kmb_isp->source_streaming = false;

	return 0;
}

static void kmb_isp_discard_all_params(struct kmb_isp *kmb_isp)
{
	struct kmb_metadata_buf *meta_buf;
	struct list_head *next = NULL;
	struct list_head *pos = NULL;

	mutex_lock(&kmb_isp->meta_q_lock);
	list_for_each_safe(pos, next, &kmb_isp->meta_params_pending_q) {
		meta_buf = list_entry(pos, struct kmb_metadata_buf, list);
		list_del(&meta_buf->list);
		vb2_buffer_done(&meta_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	list_for_each_safe(pos, next, &kmb_isp->meta_params_process_q) {
		meta_buf = list_entry(pos, struct kmb_metadata_buf, list);
		list_del(&meta_buf->list);
		vb2_buffer_done(&meta_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&kmb_isp->meta_q_lock);
}

/* Params metadata buffer ops */
static int kmb_isp_queue_params_buf(void *priv, struct kmb_metadata_buf *meta_buf)
{
	struct kmb_isp *kmb_isp = priv;

	if (WARN_ON(!priv || !meta_buf))
		return -EINVAL;

	INIT_LIST_HEAD(&meta_buf->list);

	mutex_lock(&kmb_isp->meta_q_lock);
	list_add_tail(&meta_buf->list, &kmb_isp->meta_params_pending_q);
	mutex_unlock(&kmb_isp->meta_q_lock);

	return 0;
}

static void kmb_isp_queue_params_flush(void *priv)
{
	struct kmb_isp *kmb_isp = priv;

	kmb_isp_discard_all_params(kmb_isp);
}

/* Statistics metadata buffer ops */
static const struct kmb_metabuf_queue_ops isp_params_queue_ops = {
	.queue = kmb_isp_queue_params_buf,
	.flush = kmb_isp_queue_params_flush,
};

static void kmb_isp_discard_all_stats(struct kmb_isp *kmb_isp)
{
	struct kmb_metadata_buf *meta_buf;
	struct list_head *next = NULL;
	struct list_head *pos = NULL;

	mutex_lock(&kmb_isp->meta_q_lock);
	list_for_each_safe(pos, next, &kmb_isp->meta_stats_pending_q) {
		meta_buf = list_entry(pos, struct kmb_metadata_buf, list);
		list_del(&meta_buf->list);
		vb2_buffer_done(&meta_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	list_for_each_safe(pos, next, &kmb_isp->meta_stats_process_q) {
		meta_buf = list_entry(pos, struct kmb_metadata_buf, list);
		list_del(&meta_buf->list);
		vb2_buffer_done(&meta_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&kmb_isp->meta_q_lock);
}

static int kmb_isp_queue_stats_buf(void *priv, struct kmb_metadata_buf *meta_buf)
{
	struct kmb_isp *kmb_isp = priv;

	if (WARN_ON(!priv || !meta_buf))
		return -EINVAL;

	INIT_LIST_HEAD(&meta_buf->list);

	mutex_lock(&kmb_isp->meta_q_lock);
	list_add_tail(&meta_buf->list, &kmb_isp->meta_stats_pending_q);
	mutex_unlock(&kmb_isp->meta_q_lock);

	return 0;
}

static void kmb_isp_queue_stats_flush(void *priv)
{
	struct kmb_isp *kmb_isp = priv;

	kmb_isp_discard_all_stats(kmb_isp);
}

static const struct kmb_metabuf_queue_ops isp_stats_queue_ops = {
	.queue = kmb_isp_queue_stats_buf,
	.flush = kmb_isp_queue_stats_flush,
};

static int kmb_isp_subdev_get_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *fmt)
{
	struct kmb_isp *kmb_isp = v4l2_get_subdevdata(sd);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
	} else {
		mutex_lock(&kmb_isp->lock);
		fmt->format = kmb_isp->active_pad_fmt[fmt->pad].format;
		mutex_unlock(&kmb_isp->lock);
	}

	return 0;
}

static int kmb_isp_config_pipe_src(struct kmb_isp *kmb_isp,
				   struct kmb_pipeline *pipe,
				   struct v4l2_mbus_framefmt *mbus_fmt)
{
	const struct kmb_isp_source_format *fmt_info;
	int ret;

	fmt_info = kmb_isp_get_src_fmt_by_code(mbus_fmt->code);
	if (!fmt_info) {
		dev_err(kmb_isp->dev, "Format code not supported %d",
			mbus_fmt->code);
		return -EINVAL;
	}

	/* Clean any previous configurations */
	memset(&kmb_isp->pipe_cfg, 0x00, sizeof(kmb_isp->pipe_cfg));
	kmb_isp->pipe_cfg.pipe_type = PIPE_TYPE_ISP_ISP_ULL;
	kmb_isp->pipe_cfg.src_type = SRC_TYPE_ALLOC_VPU_DATA_MIPI;
	kmb_isp->pipe_cfg.pipe_trans_hub = PIPE_TRANSFORM_HUB_NONE;

	kmb_isp->pipe_cfg.in_isp_res.w = mbus_fmt->width;
	kmb_isp->pipe_cfg.in_isp_res.h = mbus_fmt->height;

	kmb_isp->pipe_cfg.in_data_width = fmt_info->bpp;
	kmb_isp->pipe_cfg.in_data_packed = 1;

	kmb_isp->pipe_cfg.in_isp_stride = (kmb_isp->pipe_cfg.in_isp_res.w *
				kmb_isp->pipe_cfg.in_isp_res.h *
				kmb_isp->pipe_cfg.in_data_width) / 8;

	/* Always set to 8 required by the vpu firmware */
	kmb_isp->pipe_cfg.out_data_width = 8;

	/* Isp does not have scaler */
	kmb_isp->pipe_cfg.out_isp_res = kmb_isp->pipe_cfg.in_isp_res;

	ret = kmb_pipe_config_src(pipe, &kmb_isp->pipe_cfg);
	if (ret < 0)
		return ret;

	kmb_isp->source_fmt = fmt_info;

	return 0;
}

static int kmb_isp_subdev_set_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *sd_fmt)
{
	struct kmb_isp *kmb_isp = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mbus_fmt;
	struct kmb_channel_cfg channel_cfg;
	struct kmb_pipeline *pipe;
	int ret;

	mutex_lock(&kmb_isp->lock);
	if (sd_fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		pipe = &kmb_isp->try_pipe;
		mbus_fmt = v4l2_subdev_get_try_format(sd, cfg, sd_fmt->pad);
	} else {
		pipe = &kmb_isp->active_pipe;
		mbus_fmt = &kmb_isp->active_pad_fmt[sd_fmt->pad].format;
	}
	mutex_unlock(&kmb_isp->lock);

	switch (sd_fmt->pad) {
	case KMB_ISP_SINK_PAD_SENSOR:
		ret = kmb_isp_config_pipe_src(kmb_isp, pipe, &sd_fmt->format);
		if (ret < 0)
			return ret;

		/* Configure first isp control channel */
		channel_cfg.frm_res.w = sd_fmt->format.width;
		channel_cfg.frm_res.h = sd_fmt->format.height;
		channel_cfg.id = kmb_isp->config_chan_id;
		kmb_pipe_config_dest(pipe, PIPE_OUTPUT_ID_ISP_CTRL,
				     &channel_cfg);

		/* Set default resolution of destination channel */
		channel_cfg.frm_res.w = sd_fmt->format.width;
		channel_cfg.frm_res.h = sd_fmt->format.height;
		channel_cfg.id = kmb_isp->capture_chan_id;
		kmb_pipe_config_dest(pipe, PIPE_OUTPUT_ID_0, &channel_cfg);

		sd_fmt->format.width = channel_cfg.frm_res.w;
		sd_fmt->format.height = channel_cfg.frm_res.h;
		break;
	case KMB_ISP_SRC_PAD_VID: {
		struct v4l2_mbus_framefmt *mbus_src_fmt;

		mutex_lock(&kmb_isp->lock);
		if (sd_fmt->which == V4L2_SUBDEV_FORMAT_TRY)
			mbus_src_fmt =
				v4l2_subdev_get_try_format(sd, cfg,
							   KMB_ISP_SINK_PAD_SENSOR);
		else
			mbus_src_fmt =
				&kmb_isp->active_pad_fmt[KMB_ISP_SINK_PAD_SENSOR].format;
		mutex_unlock(&kmb_isp->lock);

		if (!kmb_isp_valid_destination_format(mbus_src_fmt,
						      sd_fmt->format.code))
			return -EINVAL;

		channel_cfg.frm_res.w = sd_fmt->format.width;
		channel_cfg.frm_res.h = sd_fmt->format.height;
		channel_cfg.id = kmb_isp->capture_chan_id;
		kmb_pipe_config_dest(pipe, PIPE_OUTPUT_ID_0, &channel_cfg);

		sd_fmt->format.width = channel_cfg.frm_res.w;
		sd_fmt->format.height = channel_cfg.frm_res.h;
		break;
	}
	case KMB_ISP_SINK_PAD_PARAM:
	case KMB_ISP_SRC_PAD_STATS:
		/* Isp config metadata sink format can be just fixed */
		if (sd_fmt->format.code != MEDIA_BUS_FMT_FIXED)
			return -EINVAL;
		break;
	}

	mutex_lock(&kmb_isp->lock);
	*mbus_fmt = sd_fmt->format;
	mutex_unlock(&kmb_isp->lock);

	return 0;
}

static int
kmb_isp_subdev_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->pad) {
	case KMB_ISP_SINK_PAD_SENSOR:
		if (code->index >= ARRAY_SIZE(source_fmts))
			return -EINVAL;

		code->code = source_fmts[code->index].code;
		break;
	case KMB_ISP_SRC_PAD_VID: {
		struct kmb_isp *kmb_isp = v4l2_get_subdevdata(sd);
		const struct kmb_isp_source_format *src_fmt;
		struct v4l2_mbus_framefmt *mbus_src_fmt;

		mutex_lock(&kmb_isp->lock);

		if (code->which == V4L2_SUBDEV_FORMAT_TRY)
			mbus_src_fmt =
				v4l2_subdev_get_try_format(sd, cfg,
							   KMB_ISP_SINK_PAD_SENSOR);
		else
			mbus_src_fmt =
				&kmb_isp->active_pad_fmt[KMB_ISP_SINK_PAD_SENSOR].format;

		mutex_unlock(&kmb_isp->lock);

		src_fmt = kmb_isp_get_src_fmt_by_code(mbus_src_fmt->code);
		if (!src_fmt)
			return -EINVAL;

		if (code->index >= ARRAY_SIZE(src_fmt->dest_fmts))
			return -EINVAL;

		if (!src_fmt->dest_fmts[code->index])
			return -EINVAL;

		code->code = src_fmt->dest_fmts[code->index];
		break;
	}
	case KMB_ISP_SINK_PAD_PARAM:
	case KMB_ISP_SRC_PAD_STATS:
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_FIXED;
		break;
	}

	return 0;
}

static int kmb_isp_src_s_stream(struct kmb_isp *kmb_isp, int enable)
{
	struct v4l2_subdev *subdev;
	struct media_pad *remote;
	int ret;

	remote = media_entity_remote_pad(&kmb_isp->pads[KMB_ISP_SINK_PAD_SENSOR]);
	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return -EINVAL;

	subdev = media_entity_to_v4l2_subdev(remote->entity);
	if (!subdev)
		return -EINVAL;

	ret = v4l2_subdev_call(subdev, video, s_stream, enable);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		dev_err(kmb_isp->dev, "Cannot set source stream %d", enable);

	return ret != -ENOIOCTLCMD ? ret : 0;
}

static int kmb_isp_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct kmb_isp *kmb_isp = v4l2_get_subdevdata(sd);
	int ret;

	mutex_lock(&kmb_isp->lock);

	/* Don't send isp config on stream disable */
	if (enable) {
		ret = kmb_cam_xlink_open_channel(kmb_isp->xlink_cam,
						 kmb_isp->config_chan_id);
		if (ret < 0) {
			dev_err(kmb_isp->dev,
				"Fail to open xlink channel %d", ret);
			goto error_unlock;
		}

		ret = kmb_isp_configure_vpu_source(kmb_isp);
		if (ret)
			goto error_close_xlink_channel;

		/* Process first configuration on stream enable */
		ret = kmb_isp_process_config(kmb_isp);
		if (ret)
			goto error_close_xlink_channel;

		ret = kmb_isp_start_source(kmb_isp);
		if (ret)
			goto error_discard_metadata;

		ret = kmb_isp_src_s_stream(kmb_isp, enable);
		if (ret)
			goto error_isp_stop_source;

		kmb_isp->isp_streaming = true;
	} else {
		/* Try top to stop the source synchronized */
		if (kmb_isp->source_streaming)
			kmb_isp_stop_source(kmb_isp, KMB_ISP_STOP_SYNC);

		kmb_cam_xlink_close_channel(kmb_isp->xlink_cam,
					    kmb_isp->config_chan_id);

		/* Force stop isp if still streaming after channel is closed */
		if (kmb_isp->source_streaming)
			kmb_isp_stop_source(kmb_isp, KMB_ISP_STOP_FORCE);

		/* Discard all unprocessed params and statistics */
		kmb_isp_discard_all_params(kmb_isp);
		kmb_isp_discard_all_stats(kmb_isp);

		kmb_isp_src_s_stream(kmb_isp, enable);

		kmb_isp->isp_streaming = false;
		kmb_isp->sequence = 0;
	}

	mutex_unlock(&kmb_isp->lock);

	return 0;

error_isp_stop_source:
	kmb_isp_stop_source(kmb_isp, KMB_ISP_STOP_FORCE);
error_discard_metadata:
	kmb_isp_discard_all_params(kmb_isp);
	kmb_isp_discard_all_stats(kmb_isp);
error_close_xlink_channel:
	xlink_close_channel(&kmb_isp->xlink_cam->handle,
			    kmb_isp->config_chan_id);
error_unlock:
	mutex_unlock(&kmb_isp->lock);

	return ret;
}

static int kmb_isp_subscribe_event(struct v4l2_subdev *sd,
				   struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	return v4l2_event_subscribe(fh, sub, KMB_ISP_EVT_Q_LEN, NULL);
}

/* sub-device core operations */
static struct v4l2_subdev_core_ops kmb_isp_subdev_core_ops = {
	.subscribe_event = kmb_isp_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

/* sub-device video operations */
static struct v4l2_subdev_video_ops kmb_isp_subdev_video_ops = {
	.s_stream = kmb_isp_s_stream,
};

/* sub-device pad operations */
static struct v4l2_subdev_pad_ops kmb_isp_subdev_pad_ops = {
	.set_fmt = kmb_isp_subdev_set_fmt,
	.get_fmt = kmb_isp_subdev_get_fmt,
	.enum_mbus_code = kmb_isp_subdev_enum_mbus_code,
};

/* sub-device operations */
static const struct v4l2_subdev_ops kmb_isp_subdev_ops = {
	.core = &kmb_isp_subdev_core_ops,
	.video = &kmb_isp_subdev_video_ops,
	.pad = &kmb_isp_subdev_pad_ops,
};

/* sub-device internal operations */
static int kmb_isp_open(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh)
{
	struct kmb_isp *kmb_isp = v4l2_get_subdevdata(sd);

	return kmb_pipe_request(&kmb_isp->active_pipe);
}

static int kmb_isp_close(struct v4l2_subdev *sd,
			 struct v4l2_subdev_fh *fh)
{
	struct kmb_isp *kmb_isp = v4l2_get_subdevdata(sd);

	kmb_pipe_release(&kmb_isp->active_pipe);

	return 0;
}

static const struct v4l2_subdev_internal_ops kmb_isp_internal_ops = {
	.open = kmb_isp_open,
	.close = kmb_isp_close,
};

/**
 * kmb_isp_init - Initialize Kmb isp subdevice
 * @kmb_isp: Pointer to kmb isp device
 * @dev: Pointer to camera device for which isp will be associated with
 * @csi2_config: Csi2 configuration
 * @xlink_cam: Xlink camera communication handle
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_isp_init(struct kmb_isp *kmb_isp, struct device *dev,
		 struct kmb_isp_csi2_config *csi2_config,
		 struct kmb_xlink_cam *xlink_cam)
{
	int ret;

	v4l2_subdev_init(&kmb_isp->subdev, &kmb_isp_subdev_ops);
	v4l2_set_subdevdata(&kmb_isp->subdev, kmb_isp);

	ret = kmb_pipe_init(&kmb_isp->active_pipe, dev, xlink_cam);
	if (ret < 0)
		return ret;

	ret = kmb_pipe_init(&kmb_isp->try_pipe, dev, xlink_cam);
	if (ret < 0)
		goto error_cleanup_active_pipeline;

	INIT_LIST_HEAD(&kmb_isp->meta_params_pending_q);
	INIT_LIST_HEAD(&kmb_isp->meta_params_process_q);
	INIT_LIST_HEAD(&kmb_isp->meta_stats_pending_q);
	INIT_LIST_HEAD(&kmb_isp->meta_stats_process_q);

	kmb_isp->isp_streaming = false;

	kmb_isp->dev = dev;
	kmb_isp->xlink_cam = xlink_cam;

	ret = kmb_cam_xlink_alloc_channel(kmb_isp->xlink_cam);
	if (ret < 0)
		goto error_cleanup_try_pipeline;

	kmb_isp->config_chan_id = ret;

	/* Video nodes are connected only to active pipes */
	kmb_isp->params.dma_dev = dev;
	kmb_isp->params.pipe = &kmb_isp->active_pipe;
	kmb_isp->params.queue_ops = &isp_params_queue_ops;
	kmb_isp->params.priv = kmb_isp;
	kmb_isp->params.type = KMB_METADATA_PARAMS;
	ret = kmb_metadata_init(&kmb_isp->params);
	if (ret < 0)
		goto error_free_config_channel_id;

	kmb_isp->stats.dma_dev = dev;
	kmb_isp->stats.pipe = &kmb_isp->active_pipe;
	kmb_isp->stats.queue_ops = &isp_stats_queue_ops;
	kmb_isp->stats.priv = kmb_isp;
	kmb_isp->stats.type = KMB_METADATA_STATS;
	ret = kmb_metadata_init(&kmb_isp->stats);
	if (ret < 0)
		goto error_metadata_params_cleanup;

	ret = kmb_cam_xlink_alloc_channel(kmb_isp->xlink_cam);
	if (ret < 0)
		goto error_metadata_stats_cleanup;

	kmb_isp->capture_chan_id = ret;
	kmb_isp->capture.dma_dev = dev;
	kmb_isp->capture.pipe = &kmb_isp->active_pipe;
	kmb_isp->capture.chan_id = kmb_isp->capture_chan_id;
	kmb_isp->capture.xlink_cam = kmb_isp->xlink_cam;
	ret = kmb_video_init(&kmb_isp->capture, "kmb-video-capture");
	if (ret < 0)
		goto error_free_capture_channel_id;

	kmb_isp->csi2_config = *csi2_config;

	mutex_init(&kmb_isp->lock);
	mutex_init(&kmb_isp->meta_q_lock);

	return 0;

error_free_capture_channel_id:
	kmb_cam_xlink_free_channel(kmb_isp->xlink_cam,
				   kmb_isp->capture_chan_id);
error_metadata_stats_cleanup:
	kmb_metadata_cleanup(&kmb_isp->stats);
error_metadata_params_cleanup:
	kmb_metadata_cleanup(&kmb_isp->params);
error_free_config_channel_id:
	kmb_cam_xlink_free_channel(kmb_isp->xlink_cam,
				   kmb_isp->config_chan_id);
error_cleanup_try_pipeline:
	kmb_pipe_cleanup(&kmb_isp->try_pipe);
error_cleanup_active_pipeline:
	kmb_pipe_cleanup(&kmb_isp->active_pipe);

	return ret;
}

/**
 * kmb_isp_cleanup - Cleanup kmb isp sub-device resourcess allocated in init
 * @kmb_isp: Pointer to kmb isp sub-device
 */
void kmb_isp_cleanup(struct kmb_isp *kmb_isp)
{
	kmb_video_cleanup(&kmb_isp->capture);
	kmb_cam_xlink_free_channel(kmb_isp->xlink_cam,
				   kmb_isp->capture_chan_id);

	kmb_metadata_cleanup(&kmb_isp->stats);
	kmb_metadata_cleanup(&kmb_isp->params);

	kmb_cam_xlink_free_channel(kmb_isp->xlink_cam,
				   kmb_isp->config_chan_id);

	mutex_destroy(&kmb_isp->meta_q_lock);
	mutex_destroy(&kmb_isp->lock);
}

/**
 * kmb_isp_register_entities - Register entities
 * @kmb_isp: pointer to kmb isp device
 * @v4l2_dev: pointer to V4L2 device drivers
 *
 * Register all entities in the pipeline and create
 * links between them.
 *
 * Return: 0 if successful, error code otherwise.
 */
int kmb_isp_register_entities(struct kmb_isp *kmb_isp,
			      struct v4l2_device *v4l2_dev)
{
	struct media_pad *pads = kmb_isp->pads;
	struct device *dev = kmb_isp->dev;
	int ret;

	/* Memory for xlink messages */
	kmb_isp->msg_vaddr = NULL;
	kmb_isp->msg_phy_addr = 0;
	kmb_isp->msg_vaddr = dma_alloc_coherent(kmb_isp->dev,
						KMB_ISP_CH_DATA_SIZE,
						&kmb_isp->msg_phy_addr, 0);
	if (!kmb_isp->msg_vaddr) {
		dev_err(dev, "Fail to allocate msg dma memory");
		return -ENOMEM;
	}

	kmb_isp->subdev.internal_ops = &kmb_isp_internal_ops;
	kmb_isp->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				 V4L2_SUBDEV_FL_HAS_EVENTS;
	kmb_isp->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	strscpy(kmb_isp->subdev.name, KMB_ISP_DRV_NAME,
		sizeof(kmb_isp->subdev.name));

	pads[KMB_ISP_SINK_PAD_SENSOR].flags = MEDIA_PAD_FL_SINK;
	pads[KMB_ISP_SINK_PAD_PARAM].flags = MEDIA_PAD_FL_SINK;
	pads[KMB_ISP_SRC_PAD_STATS].flags = MEDIA_PAD_FL_SOURCE;
	pads[KMB_ISP_SRC_PAD_VID].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&kmb_isp->subdev.entity,
				     KMB_ISP_PADS_NUM, pads);
	if (ret < 0) {
		dev_err(dev, "Fail to init media entity");
		return ret;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, &kmb_isp->subdev);
	if (ret < 0) {
		dev_err(dev, "Fail to register media entity");
		return ret;
	}

	/* Register video nodes */
	ret = kmb_metadata_register(&kmb_isp->params, v4l2_dev);
	if (ret < 0)
		goto error_unregister_subdev;

	ret = media_create_pad_link(&kmb_isp->params.video.entity,
				    0,
				    &kmb_isp->subdev.entity,
				    KMB_ISP_SINK_PAD_PARAM,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Fail to link %s->%s entities",
			kmb_isp->params.video.entity.name,
			kmb_isp->subdev.entity.name);
		goto error_unregister_params;
	}

	ret = kmb_metadata_register(&kmb_isp->stats, v4l2_dev);
	if (ret < 0)
		goto error_unregister_params;

	ret = media_create_pad_link(&kmb_isp->subdev.entity,
				    KMB_ISP_SRC_PAD_STATS,
				    &kmb_isp->stats.video.entity, 0,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Fail to link %s->%s entities",
			kmb_isp->stats.video.entity.name,
			kmb_isp->subdev.entity.name);
		goto error_unregister_stats;
	}

	ret = kmb_video_register(&kmb_isp->capture, v4l2_dev);
	if (ret < 0)
		goto error_unregister_stats;

	ret = media_create_pad_link(&kmb_isp->subdev.entity,
				    KMB_ISP_SRC_PAD_VID,
				    &kmb_isp->capture.video->entity, 0,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret < 0) {
		dev_err(kmb_isp->dev, "Fail to link %s->%s entities",
			kmb_isp->subdev.entity.name,
			kmb_isp->capture.video->entity.name);
		goto error_unregister_video;
	}

	return 0;

error_unregister_video:
	kmb_video_unregister(&kmb_isp->capture);
error_unregister_stats:
	kmb_metadata_unregister(&kmb_isp->stats);
error_unregister_params:
	kmb_metadata_unregister(&kmb_isp->params);
error_unregister_subdev:
	v4l2_device_unregister_subdev(&kmb_isp->subdev);

	return ret;
}

/**
 * kmb_isp_unregister_entities - Unregister this media's entities
 * @kmb_isp: pointer to kmb isp device
 */
void kmb_isp_unregister_entities(struct kmb_isp *kmb_isp)
{
	dma_free_coherent(kmb_isp->dev, KMB_ISP_CH_DATA_SIZE,
			  kmb_isp->msg_vaddr, kmb_isp->msg_phy_addr);

	kmb_video_unregister(&kmb_isp->capture);
	kmb_metadata_unregister(&kmb_isp->stats);
	kmb_metadata_unregister(&kmb_isp->params);

	v4l2_device_unregister_subdev(&kmb_isp->subdev);
}
