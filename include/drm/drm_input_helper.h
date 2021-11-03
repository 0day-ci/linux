/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Google, Inc.
 */
#ifndef __DRM_INPUT_HELPER_H__
#define __DRM_INPUT_HELPER_H__

#include <linux/input.h>

struct drm_device;

struct drm_input_handler {
	struct input_handler handler;
	void *priv;
	void (*callback)(void *priv);
};

int drm_input_handle_register(struct drm_device *dev,
			      struct drm_input_handler *handler);
void drm_input_handle_unregister(struct drm_input_handler *handler);

#endif /* __DRM_INPUT_HELPER_H__ */
