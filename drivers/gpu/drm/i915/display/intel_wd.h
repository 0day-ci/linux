/* SPDX-License-Identifier: GPL-2.0
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author :
 *	Suraj Kandpal<suraj.kandpal@intel.com>
 *	Arun Murthy<arun.r.murthy@intel.com>
 */

#ifndef _INTEL_WD_H
#define _INTEL_WD_H

#include <drm/drm_crtc.h>

#include "intel_display_types.h"

#define I915_MAX_WD_TANSCODERS 2

struct intel_wd {
	struct intel_encoder base;
	struct intel_crtc *wd_crtc;
	intel_wakeref_t io_wakeref[I915_MAX_WD_TANSCODERS];
	struct intel_connector *attached_connector;
	enum transcoder trans;
	struct i915_vma *vma;
	unsigned long flags;
	struct drm_writeback_job *job;
	int triggered_cap_mode;
	int frame_num;
	bool stream_cap;
	bool start_capture;
	int slicing_strategy;
};

struct intel_wd_clk_vals {
	u32 cdclk;
	u16 link_m;
	u16 link_n;
};

static inline struct intel_wd *enc_to_intel_wd(struct intel_encoder *encoder)
{
	return container_of(&encoder->base, struct intel_wd, base.base);
}
void intel_wd_init(struct drm_i915_private *dev_priv, enum transcoder trans);
void intel_wd_enable_capture(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config,
				struct drm_connector_state *conn_state);
void intel_wd_handle_isr(struct drm_i915_private *dev_priv);
void intel_wd_set_vblank_event(struct intel_crtc *crtc,
				struct intel_crtc_state *state);
#endif/* _INTEL_WD_H */
