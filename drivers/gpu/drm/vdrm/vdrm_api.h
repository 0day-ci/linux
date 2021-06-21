/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vdrm_api.h -- Virtual DRM API
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */

#ifndef __VDRM_API__
#define __VDRM_API__

#include <linux/of_device.h>
#include <drm/drm_crtc.h>

/**
 * struct vdrm_property_info - Information about the properties passed from
 *			       the DRM driver to vDRM
 * @prop: Parent property to pass to vDRM
 * @default_val: Default value for the property passed to vDRM
 */
struct vdrm_property_info {
	struct drm_property *prop;
	uint64_t default_val;
};

/**
 * struct vdrm_funcs - Callbacks to parent DRM driver
 */
struct vdrm_funcs {
	/**
	 * @dumb_create:
	 *
	 * Called by &drm_driver.dumb_create. Please read the documentation
	 * for the &drm_driver.dumb_create hook for more details.
	 */
	int (*dumb_create)(struct drm_file *file, struct drm_device *dev,
			   struct drm_mode_create_dumb *args);

	/**
	 * @crtc_flush:
	 *
	 * Called by &drm_crtc_helper_funcs.atomic_flush. Please read the
	 * documentation for the &drm_crtc_helper_funcs.atomic_flush hook for
	 * more details.
	 */
	void (*crtc_flush)(struct drm_crtc *crtc);
};

struct vdrm_device;
struct vdrm_display;

void vdrm_drv_handle_vblank(struct vdrm_display *vdisplay);
void vdrm_drv_finish_page_flip(struct vdrm_display *vdisplay);
struct vdrm_device *vdrm_drv_init(struct drm_device *dev,
				  struct device_node *np, int num_props,
				  struct vdrm_property_info *props,
				  const struct vdrm_funcs *funcs);
int vdrm_drv_plane_init(struct vdrm_device *vdrm, struct drm_plane *plane,
			const struct drm_plane_funcs *funcs,
			const struct drm_plane_helper_funcs *helper_funcs,
			const u32 *formats, unsigned int num_formats,
			int max_zpos);
struct vdrm_display *vdrm_drv_display_init(struct vdrm_device *vdrm,
					   struct drm_crtc *crtc,
					   struct drm_plane *plane);
int vdrm_drv_register(struct vdrm_device *vdrm);
void vdrm_drv_fini(struct vdrm_device *vdrm);

#endif /* __VDRM_API__ */
