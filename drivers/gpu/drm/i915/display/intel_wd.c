// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *	Suraj Kandpal <suraj.kandpal@intel.com>
 *	Arun Murthy <arun.r.murthy@intel.com>
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>

#include "intel_atomic.h"
#include "intel_connector.h"
#include "intel_wd.h"
#include "intel_fb_pin.h"
#include "intel_de.h"

enum {
	WD_CAPTURE_4_PIX,
	WD_CAPTURE_2_PIX,
} wd_capture_format;

static struct drm_writeback_job
*intel_get_writeback_job_from_queue(struct intel_wd *intel_wd)
{
	struct drm_writeback_job *job;
	struct drm_i915_private *dev_priv = to_i915(intel_wd->base.base.dev);
	struct drm_writeback_connector *wb_conn =
		intel_wd->attached_connector->base.wb_connector;
	unsigned long flags;

	spin_lock_irqsave(&wb_conn->job_lock, flags);
	job = list_first_entry_or_null(&wb_conn->job_queue,
			struct drm_writeback_job,
			list_entry);
	spin_unlock_irqrestore(&wb_conn->job_lock, flags);
	if (job == NULL) {
		drm_dbg_kms(&dev_priv->drm, "job queue is empty\n");
		return NULL;
	}

	return job;
}

void print_connectors(struct drm_i915_private *dev_priv)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *intel_connector;

	drm_modeset_lock_all(&dev_priv->drm);
	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		intel_connector = to_intel_connector(connector);
		drm_dbg_kms(&dev_priv->drm, "[CONNECTOR:%d:%s]: status: %s\n",
			connector->base.id, connector->name,
			drm_get_connector_status_name(connector->status));
	}
	drm_connector_list_iter_end(&conn_iter);
	drm_modeset_unlock_all(&dev_priv->drm);
}

/*Check with Spec*/
static const u32 wb_fmts[] = {
		DRM_FORMAT_YUV444,
		DRM_FORMAT_XYUV8888,
		DRM_FORMAT_XBGR8888,
		DRM_FORMAT_XRGB8888,
		DRM_FORMAT_Y410,
		DRM_FORMAT_YUV422,
		DRM_FORMAT_XBGR2101010,
		DRM_FORMAT_RGB565,

};

static int intel_wd_get_format(int pixel_format)
{
	int wd_format = -EINVAL;

	switch (pixel_format) {
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_XYUV8888:
	case DRM_FORMAT_YUV444:
		wd_format = WD_CAPTURE_4_PIX;
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_RGB565:
		wd_format = WD_CAPTURE_2_PIX;
		break;
	default:
		DRM_ERROR("unsupported pixel format %x!\n",
			pixel_format);
	}

	return wd_format;
}

static int intel_wd_verify_pix_format(int format)
{
	const struct drm_format_info *info = drm_format_info(format);
	int pix_format = info->format;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(wb_fmts); i++)
		if (pix_format == wb_fmts[i])
			return 0;

	return 1;
}

static u32 intel_wd_get_stride(const struct intel_crtc_state *crtc_state,
			int format)
{
	const struct drm_format_info *info = drm_format_info(format);
	int wd_format;
	int hactive, pixel_size;

	wd_format = intel_wd_get_format(info->format);

	switch (wd_format) {
	case WD_CAPTURE_4_PIX:
		pixel_size = 4;
		break;
	case WD_CAPTURE_2_PIX:
		pixel_size = 2;
		break;
	default:
		pixel_size = 1;
		break;
	}

	hactive = crtc_state->hw.adjusted_mode.crtc_hdisplay;

	return DIV_ROUND_UP(hactive * pixel_size, 64);
}

static int intel_wd_pin_fb(struct intel_wd *intel_wd,
			struct drm_framebuffer *fb)
{
	const struct i915_ggtt_view view = {
		.type = I915_GGTT_VIEW_NORMAL,
		};
	struct i915_vma *vma;

	vma = intel_pin_and_fence_fb_obj(fb, false, &view, false,
			&intel_wd->flags);

	if (IS_ERR(vma))
		return PTR_ERR(vma);

	intel_wd->vma = vma;
	return 0;
}

static void intel_configure_slicing_strategy(struct drm_i915_private *dev_priv,
		struct intel_wd *intel_wd, u32 *tmp)
{
	*tmp &= ~WD_STRAT_MASK;
	if (intel_wd->slicing_strategy == 1)
		*tmp |= WD_SLICING_STRAT_1_1;
	else if (intel_wd->slicing_strategy == 2)
		*tmp |= WD_SLICING_STRAT_2_1;
	else if (intel_wd->slicing_strategy == 3)
		*tmp |= WD_SLICING_STRAT_4_1;
	else if (intel_wd->slicing_strategy == 4)
		*tmp |= WD_SLICING_STRAT_8_1;

	intel_de_write(dev_priv, WD_STREAMCAP_CTL(intel_wd->trans),
			*tmp);

}

static enum drm_mode_status
intel_wd_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	drm_dbg_kms(connector->dev, ":");
	return MODE_OK;
}

static int intel_wd_get_modes(struct drm_connector *connector)
{
	return 0;
}

static void intel_wd_get_config(struct intel_encoder *encoder,
		struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *intel_crtc =
		to_intel_crtc(pipe_config->uapi.crtc);

	drm_dbg_kms(&dev_priv->drm, "\n");
	if (intel_crtc) {
		memcpy(pipe_config, intel_crtc->config,
			sizeof(*pipe_config));
		pipe_config->output_types |= BIT(INTEL_OUTPUT_WD);
		drm_dbg_kms(&dev_priv->drm, "crtc found\n");
	}

}

static int intel_wd_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_state *pipe_config,
			struct drm_connector_state *conn_state)
{
	struct intel_wd *intel_wd = enc_to_intel_wd(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_writeback_job *job;

	drm_dbg_kms(&dev_priv->drm, "\n");
	job = intel_get_writeback_job_from_queue(intel_wd);
	if (job || conn_state->writeback_job) {
		intel_wd->wd_crtc = to_intel_crtc(pipe_config->uapi.crtc);
		return 0;
	}
	drm_dbg_kms(&dev_priv->drm, "No writebackjob in queue\n");

	return 0;
}

static void intel_wd_get_power_domains(struct intel_encoder *encoder,
			struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_wd *intel_wd = enc_to_intel_wd(encoder);
	intel_wakeref_t wakeref;

	wakeref = intel_display_power_get(dev_priv,
				encoder->power_domain);

	intel_wd->io_wakeref[0] = wakeref;
	drm_dbg_kms(encoder->base.dev, "\n");
}

static bool intel_wd_get_hw_state(struct intel_encoder *encoder,
		enum pipe *pipe)
{
	bool ret = false;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_wd *intel_wd = enc_to_intel_wd(encoder);
	struct intel_crtc *wd_crtc = intel_wd->wd_crtc;
	intel_wakeref_t wakeref;
	u32 tmp;

	if (wd_crtc)
		return false;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
				encoder->power_domain);
	drm_dbg_kms(encoder->base.dev, "power enabled : %s\n",
			!wakeref ? "false":"true");

	if (!wakeref)
		return false;

	tmp = intel_de_read(dev_priv, PIPECONF(intel_wd->trans));
	ret = tmp & WD_TRANS_ACTIVE;
	drm_dbg_kms(encoder->base.dev, "trancoder enabled: %s\n",
			ret ? "true":"false");
	if (ret) {
		*pipe = wd_crtc->pipe;
		drm_dbg_kms(encoder->base.dev, "pipe selected is %d\n",
			wd_crtc->pipe);
	}
	return true;
}

static int intel_wd_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_st,
				    struct drm_connector_state *conn_st)
{
	/* Check for the format and buffers and property validity */
	struct drm_framebuffer *fb;
	struct drm_writeback_job *job = conn_st->writeback_job;
	struct drm_i915_private *dev_priv = to_i915(encoder->dev);
	const struct drm_display_mode *mode = &crtc_st->mode;
	int ret;

	drm_dbg_kms(&dev_priv->drm, "\n");

	if (!job) {
		drm_dbg_kms(&dev_priv->drm, "No writeback job created returning\n");
		return -EINVAL;
	}

	fb = job->fb;

	if (!fb) {
		drm_dbg_kms(&dev_priv->drm, "Invalid framebuffer\n");
		return -EINVAL;
	}

	if (fb->width != mode->hdisplay || fb->height != mode->vdisplay) {
		drm_dbg_kms(&dev_priv->drm, "Invalid framebuffer size %ux%u\n",
				fb->width, fb->height);
		return -EINVAL;
	}

	ret = intel_wd_verify_pix_format(fb->format->format);

	if (ret) {
		drm_dbg_kms(&dev_priv->drm, "Unsupported framebuffer format %08x\n",
				fb->format->format);
		return -EINVAL;
	}

	return 0;
}


static const struct drm_encoder_helper_funcs wd_encoder_helper_funcs = {
	.atomic_check = intel_wd_encoder_atomic_check,
};

static void intel_wd_connector_destroy(struct drm_connector *connector)
{
	drm_dbg_kms(connector->dev, "\n");
	drm_connector_cleanup(connector);
	kfree(connector);
}

static enum drm_connector_status
intel_wb_connector_detect(struct drm_connector *connector, bool force)
{
	drm_dbg_kms(connector->dev, "Writeback connector connected\n");
	return connector_status_connected;
}


static const struct drm_connector_funcs wd_connector_funcs = {
	.detect = intel_wb_connector_detect,
	.reset = drm_atomic_helper_connector_reset,
	.destroy = intel_wd_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs wd_connector_helper_funcs = {
	.get_modes = intel_wd_get_modes,
	.mode_valid = intel_wd_mode_valid,
};

static bool intel_fastset_dis(struct intel_encoder *encoder,
		struct intel_crtc_state *pipe_config)
{
	pipe_config->uapi.mode_changed = true;
	drm_dbg_kms(encoder->base.dev, "\n");
	return false;
}

void intel_wd_init(struct drm_i915_private *dev_priv, enum transcoder trans)
{
	struct intel_wd *intel_wd;
	struct intel_encoder *encoder;
	struct intel_connector *intel_connector;
	struct drm_writeback_connector *wb_conn;
	int n_formats = ARRAY_SIZE(wb_fmts);
	int err;

	drm_dbg_kms(&dev_priv->drm, "\n");
	intel_wd = kzalloc(sizeof(*intel_wd), GFP_KERNEL);
	if (!intel_wd)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(intel_wd);
		return;
	}

	wb_conn = &intel_connector->wb_conn;
	wb_conn->base = &intel_connector->base;
	wb_conn->encoder = &intel_wd->base.base;

	encoder = &intel_wd->base;
	intel_wd->attached_connector = intel_connector;
	intel_wd->trans = trans;
	intel_wd->triggered_cap_mode = 1;
	intel_wd->frame_num = 1;
	intel_wd->slicing_strategy = 1;
	encoder->get_config = intel_wd_get_config;
	encoder->compute_config = intel_wd_compute_config;
	encoder->get_hw_state = intel_wd_get_hw_state;
	encoder->type = INTEL_OUTPUT_WD;
	encoder->cloneable = 0;
	encoder->pipe_mask = ~0;
	encoder->power_domain = POWER_DOMAIN_TRANSCODER_B;
	encoder->get_power_domains = intel_wd_get_power_domains;
	encoder->initial_fastset_check = intel_fastset_dis;
	intel_connector->get_hw_state =
		intel_connector_get_hw_state;

	err = drm_writeback_connector_init(&dev_priv->drm, wb_conn,
		&wd_connector_funcs,
		&wd_encoder_helper_funcs,
		wb_fmts, n_formats);

	if (err != 0) {
		drm_dbg_kms(&dev_priv->drm,
		"drm_writeback_connector_init: Failed: %d\n",
			err);
		goto cleanup;
	}

	drm_connector_helper_add(wb_conn->base, &wd_connector_helper_funcs);
	intel_connector_attach_encoder(intel_connector, encoder);
	wb_conn->base->status = connector_status_connected;
	return;

cleanup:
	kfree(intel_wd);
	intel_connector_free(intel_connector);
}

void intel_wd_writeback_complete(struct intel_wd *intel_wd,
	struct drm_writeback_job *job, int status)
{
	struct drm_writeback_connector *wb_conn =
		intel_wd->attached_connector->base.wb_connector;
	drm_writeback_signal_completion(wb_conn, status);
}

int intel_wd_setup_transcoder(struct intel_wd *intel_wd,
		struct intel_crtc_state *pipe_config,
		struct drm_connector_state *conn_state,
		struct drm_writeback_job *job)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->uapi.crtc);
	enum pipe pipe = intel_crtc->pipe;
	struct drm_framebuffer *fb;
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	struct drm_gem_object *wd_fb_obj;
	int ret;
	u32 stride, tmp;
	u16 hactive, vactive;

	fb = job->fb;
	wd_fb_obj = fb->obj[0];
	if (!wd_fb_obj) {
		drm_dbg_kms(&dev_priv->drm, "No framebuffer gem object created\n");
		return -1;
	}
	ret = intel_wd_pin_fb(intel_wd, fb);
	drm_WARN_ON(&dev_priv->drm, ret != 0);

	/*Write stride and surface registers in that particular order*/
	stride = intel_wd_get_stride(pipe_config, fb->format->format);

	tmp = intel_de_read(dev_priv, WD_STRIDE(intel_wd->trans));
	tmp &= ~WD_STRIDE_MASK;
	tmp |= (stride << WD_STRIDE_SHIFT);

	intel_de_write(dev_priv, WD_STRIDE(intel_wd->trans), tmp);

	tmp = intel_de_read(dev_priv, WD_SURF(intel_wd->trans));
	drm_dbg_kms(&dev_priv->drm, "%d is the surface address\n", tmp);

	intel_de_write(dev_priv, WD_SURF(intel_wd->trans),
			i915_ggtt_offset(intel_wd->vma));

	tmp = intel_de_read_fw(dev_priv, WD_IIR(intel_wd->trans));
	intel_de_write_fw(dev_priv, WD_IIR(intel_wd->trans), tmp);

	tmp = ~(WD_GTT_FAULT_INT|WD_WRITE_COMPLETE_INT|WD_FRAME_COMPLETE_INT|
			WD_VBLANK_INT|WD_OVERRUN_INT|WD_CAPTURING_INT);
	intel_de_write_fw(dev_priv, WD_IMR(intel_wd->trans), tmp);

	if (intel_wd->stream_cap) {
		tmp = intel_de_read(dev_priv,
				WD_STREAMCAP_CTL(intel_wd->trans));
		tmp |= WD_STREAM_CAP_MODE_EN;
		intel_configure_slicing_strategy(dev_priv, intel_wd, &tmp);
	}

	hactive = pipe_config->uapi.mode.hdisplay;
	vactive = pipe_config->uapi.mode.vdisplay;

	drm_dbg_kms(&dev_priv->drm, "hactive : %d, vactive: %d\n", hactive, vactive);

	tmp = intel_de_read(dev_priv, HTOTAL(intel_wd->trans));
	drm_dbg_kms(&dev_priv->drm, "hactive_reg : %d\n", tmp);
	tmp = intel_de_read(dev_priv, VTOTAL(intel_wd->trans));
	drm_dbg_kms(&dev_priv->drm, "vactive_reg : %d\n", tmp);
	/* minimum hactive as per bspec: 64 pixels*/
	if (hactive < 64)
		drm_err(&dev_priv->drm, "hactive is less then 64 pixels\n");

	intel_de_write(dev_priv, HTOTAL(intel_wd->trans), hactive - 1);
	intel_de_write(dev_priv, VTOTAL(intel_wd->trans), vactive - 1);

	tmp = intel_de_read(dev_priv, WD_TRANS_FUNC_CTL(intel_wd->trans));
	/* select pixel format */
	tmp &= ~WD_PIX_FMT_MASK;

	switch (fb->format->format) {
	default:
	fallthrough;
	case DRM_FORMAT_YUYV:
		tmp |= WD_PIX_FMT_YUYV;
		break;
	case DRM_FORMAT_XYUV8888:
		tmp |= WD_PIX_FMT_XYUV8888;
		break;
	case DRM_FORMAT_XBGR8888:
		tmp |= WD_PIX_FMT_XBGR8888;
		break;
	case DRM_FORMAT_Y410:
		tmp |= WD_PIX_FMT_Y410;
		break;
	case DRM_FORMAT_YUV422:
		tmp |= WD_PIX_FMT_YUV422;
		break;
	case DRM_FORMAT_XBGR2101010:
		tmp |= WD_PIX_FMT_XBGR2101010;
		break;
	case DRM_FORMAT_RGB565:
		tmp |= WD_PIX_FMT_RGB565;
		break;
	}

	if (intel_wd->triggered_cap_mode)
		tmp |= WD_TRIGGERED_CAP_MODE_ENABLE;

	if (intel_wd->stream_cap)
		tmp |= WD_CTL_POINTER_DTDH;

	/*select input pipe*/
	tmp &= ~WD_INPUT_SELECT_MASK;
	drm_dbg_kms(&dev_priv->drm, "Selected pipe is %d\n", pipe);
	switch (pipe) {
	default:
		fallthrough;
	case PIPE_A:
		tmp |= WD_INPUT_PIPE_A;
		break;
	case PIPE_B:
		tmp |= WD_INPUT_PIPE_B;
		break;
	case PIPE_C:
		tmp |= WD_INPUT_PIPE_C;
		break;
	case PIPE_D:
		tmp |= WD_INPUT_PIPE_D;
		break;
	}

	/* enable DDI buffer */
	if (!(tmp & TRANS_WD_FUNC_ENABLE))
		tmp |= TRANS_WD_FUNC_ENABLE;

	intel_de_write(dev_priv, WD_TRANS_FUNC_CTL(intel_wd->trans), tmp);

	tmp = intel_de_read(dev_priv, PIPECONF(intel_wd->trans));
	ret = tmp & WD_TRANS_ACTIVE;
	drm_dbg_kms(&dev_priv->drm, "Trancoder enabled: %s\n", ret ? "true":"false");

	if (!ret) {
		/*enable the transcoder	*/
		tmp = intel_de_read(dev_priv, PIPECONF(intel_wd->trans));
		tmp |= WD_TRANS_ENABLE;
		intel_de_write(dev_priv, PIPECONF(intel_wd->trans), tmp);

		/* wait for transcoder to be enabled */
		if (intel_de_wait_for_set(dev_priv, PIPECONF(intel_wd->trans),
				WD_TRANS_ACTIVE, 10))
			drm_err(&dev_priv->drm, "WD transcoder not enabled\n");
	}

	return 0;
}

static void intel_wd_disable_capture(struct intel_wd *intel_wd)
{
	struct drm_i915_private *dev_priv = to_i915(intel_wd->base.base.dev);
	u32 tmp;

	intel_de_write_fw(dev_priv, WD_IMR(intel_wd->trans), 0xFF);
	tmp = intel_de_read(dev_priv, PIPECONF(intel_wd->trans));
	tmp &= WD_TRANS_DISABLE;
	intel_de_write(dev_priv, PIPECONF(intel_wd->trans), tmp);

	drm_dbg_kms(&dev_priv->drm, "WD Trans_Conf value after disable = 0x%08x\n",
		intel_de_read(dev_priv, PIPECONF(intel_wd->trans)));
	tmp = intel_de_read(dev_priv, WD_TRANS_FUNC_CTL(intel_wd->trans));
	tmp |= ~TRANS_WD_FUNC_ENABLE;
}

int intel_wd_capture(struct intel_wd *intel_wd,
		struct intel_crtc_state *pipe_config,
		struct drm_connector_state *conn_state,
		struct drm_writeback_job *job)
{
	u32 tmp;
	struct drm_i915_private *dev_priv = to_i915(intel_wd->base.base.dev);
	int ret = 0, status = 0;
	struct intel_crtc *wd_crtc = intel_wd->wd_crtc;
	unsigned long flags;

	drm_dbg_kms(&dev_priv->drm, "\n");

	if (!job->out_fence)
		drm_dbg_kms(&dev_priv->drm, "Not able to get out_fence for job\n");

	ret = intel_wd_setup_transcoder(intel_wd, pipe_config,
		conn_state, job);

	if (ret < 0) {
		drm_dbg_kms(&dev_priv->drm,
		"wd transcoder setup not completed aborting capture\n");
		return -1;
	}

	if (wd_crtc == NULL) {
		DRM_ERROR("CRTC not attached\n");
		return -1;
	}

	tmp = intel_de_read_fw(dev_priv,
			WD_TRANS_FUNC_CTL(intel_wd->trans));
	tmp |= START_TRIGGER_FRAME;
	tmp &= ~WD_FRAME_NUMBER_MASK;
	tmp |= intel_wd->frame_num;
	intel_de_write_fw(dev_priv,
			WD_TRANS_FUNC_CTL(intel_wd->trans), tmp);

	if (!intel_de_wait_for_set(dev_priv, WD_IIR(intel_wd->trans),
				WD_FRAME_COMPLETE_INT, 100)){
		drm_dbg_kms(&dev_priv->drm, "frame captured\n");
		tmp = intel_de_read(dev_priv, WD_IIR(intel_wd->trans));
		drm_dbg_kms(&dev_priv->drm, "iir value : %d\n", tmp);
		status = 0;
	} else {
		drm_dbg_kms(&dev_priv->drm, "frame not captured triggering stop frame\n");
		tmp = intel_de_read(dev_priv,
				WD_TRANS_FUNC_CTL(intel_wd->trans));
		tmp |= STOP_TRIGGER_FRAME;
		intel_de_write(dev_priv,
				WD_TRANS_FUNC_CTL(intel_wd->trans), tmp);
		status = -1;
	}

	intel_de_write(dev_priv, WD_IIR(intel_wd->trans), tmp);
	intel_wd_writeback_complete(intel_wd, job, status);
	if (intel_get_writeback_job_from_queue(intel_wd) == NULL)
		intel_wd_disable_capture(intel_wd);
	if (wd_crtc->wd.e) {
		spin_lock_irqsave(&dev_priv->drm.event_lock, flags);
		drm_dbg_kms(&dev_priv->drm, "send %p\n", wd_crtc->wd.e);
		drm_crtc_send_vblank_event(&wd_crtc->base,
					wd_crtc->wd.e);
		spin_unlock_irqrestore(&dev_priv->drm.event_lock, flags);
		wd_crtc->wd.e = NULL;
	} else {
		DRM_ERROR("Event NULL! %p, %p\n", &dev_priv->drm,
			wd_crtc);
	}
	return 0;

}

void intel_wd_enable_capture(struct intel_encoder *encoder,
		struct intel_crtc_state *pipe_config,
		struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_wd *intel_wd = enc_to_intel_wd(encoder);
	struct drm_writeback_job *job;

	drm_dbg_kms(&dev_priv->drm, "\n");

	job = intel_get_writeback_job_from_queue(intel_wd);
	if (job == NULL) {
		drm_dbg_kms(&dev_priv->drm,
			"job queue is empty not capturing any frame\n");
		return;
	}

	intel_wd_capture(intel_wd, pipe_config,
			conn_state, job);
	intel_wd->frame_num += 1;

}

void intel_wd_set_vblank_event(struct intel_crtc *intel_crtc,
			struct intel_crtc_state *intel_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	struct drm_crtc_state *state = &intel_crtc_state->uapi;
	struct intel_encoder *encoder;
	struct intel_wd *intel_wd;

	for_each_intel_encoder_with_wd(&dev_priv->drm, encoder) {
		intel_wd = enc_to_intel_wd(encoder);

		if (!intel_wd->wd_crtc) {
			drm_dbg_kms(&dev_priv->drm, "wd crtc not found\n");
			return;
		}
	}

	if (intel_crtc == intel_wd->wd_crtc) {
		intel_crtc->wd.e = state->event;
		state->event = NULL;
		if (intel_crtc->wd.e)
			drm_dbg_kms(&dev_priv->drm, "WD event:%p\n",
				intel_crtc->wd.e);
		else
			drm_dbg_kms(&dev_priv->drm, "WD no event\n");
	}
}

void intel_wd_handle_isr(struct drm_i915_private *dev_priv)
{
	u32 iir_value = 0;
	struct intel_encoder *encoder;
	struct intel_wd *intel_wd;

	iir_value = intel_de_read(dev_priv, WD_IIR(TRANSCODER_WD_0));
	drm_dbg_kms(&dev_priv->drm, "\n");

	for_each_intel_encoder_with_wd(&dev_priv->drm, encoder) {
		intel_wd = enc_to_intel_wd(encoder);

		if (!intel_wd->wd_crtc) {
			DRM_ERROR("NO CRTC attached with WD\n");
			goto clear_iir;
		}
	}

	if (iir_value & WD_VBLANK_INT)
		drm_dbg_kms(&dev_priv->drm, "vblank interrupt for wd transcoder\n");
	if (iir_value & WD_WRITE_COMPLETE_INT)
		drm_dbg_kms(&dev_priv->drm,
		"wd write complete interrupt encountered\n");
	else
		DRM_INFO("iir: %x\n", iir_value);
	if (iir_value & WD_FRAME_COMPLETE_INT) {
		drm_dbg_kms(&dev_priv->drm,
			"frame complete interrupt for wd transcoder\n");
		return;
	}
clear_iir:
	intel_de_write(dev_priv, WD_IIR(TRANSCODER_WD_0), iir_value);
}
