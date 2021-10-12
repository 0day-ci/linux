/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_FB_PIN_H__
#define __INTEL_FB_PIN_H__

#include <linux/types.h>

struct drm_i915_private;
struct drm_framebuffer;
struct intel_fbdev;
struct i915_vma;
struct intel_plane_state;
struct i915_ggtt_view;

int intel_plane_pin_fb(struct intel_plane_state *plane_state);
void intel_plane_unpin_fb(struct intel_plane_state *old_plane_state);

int intel_fbdev_pin_and_fence(struct drm_i915_private *dev_priv,
			      struct intel_fbdev *ifbdev,
			      void **vaddr);
void intel_fbdev_unpin(struct intel_fbdev *ifbdev);
#endif
