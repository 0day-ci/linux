// SPDX-License-Identifier: GPL-2.0+
/*
 * vdrm_drv.c -- Virtual DRM driver
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 *
 * This driver is based on drivers/gpu/drm/drm_simple_kms_helper.c.
 */

#include <linux/of_device.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <video/videomode.h>

#include "vdrm_drv.h"

static inline struct vdrm_display *
to_vdrm_display(struct drm_connector *connector)
{
	return container_of(connector, struct vdrm_display, connector);
}

static inline struct vdrm_display *
crtc_to_vdrm_display(struct drm_crtc *crtc)
{
	return container_of(crtc, struct vdrm_display, crtc);
}

static int vdrm_dumb_create(struct drm_file *file, struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct vdrm_device *vdrm = to_vdrm_device(dev);

	return vdrm->funcs->dumb_create(file, dev, args);
}

struct vdrm_framebuffer {
	struct drm_framebuffer fb;
	struct drm_framebuffer *parent_fb;
};

static inline struct vdrm_framebuffer *
to_vdrm_framebuffer(struct drm_framebuffer *fb)
{
	return container_of(fb, struct vdrm_framebuffer, fb);
}

static void vdrm_fb_destroy(struct drm_framebuffer *fb)
{
	struct vdrm_framebuffer *vfb = to_vdrm_framebuffer(fb);

	vfb->parent_fb->funcs->destroy(vfb->parent_fb);
	drm_framebuffer_cleanup(fb);
	kfree(vfb);
}

static const struct drm_framebuffer_funcs vdrm_fb_funcs = {
	.destroy = vdrm_fb_destroy,
};

static int vdrm_fb_init(struct drm_device *dev, struct vdrm_framebuffer *vfb)
{
	vfb->fb = *vfb->parent_fb;
	vfb->fb.dev = dev;

	return drm_framebuffer_init(dev, &vfb->fb, &vdrm_fb_funcs);
}

static struct drm_framebuffer *
vdrm_fb_create(struct drm_device *dev, struct drm_file *file_priv,
	       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct vdrm_device *vdrm = to_vdrm_device(dev);
	const struct drm_mode_config_funcs *mode_config_funcs =
		vdrm->parent->mode_config.funcs;
	struct vdrm_framebuffer *vfb;
	struct drm_framebuffer *fb;
	int ret;

	vfb = kzalloc(sizeof(*vfb), GFP_KERNEL);
	if (!vfb)
		return ERR_PTR(-ENOMEM);

	fb = mode_config_funcs->fb_create(vdrm->parent, file_priv, mode_cmd);
	if (IS_ERR(fb)) {
		kfree(vfb);
		return fb;
	}

	vfb->parent_fb = fb;
	ret = vdrm_fb_init(dev, vfb);
	if (ret) {
		fb->funcs->destroy(fb);
		kfree(vfb);
		return ERR_PTR(ret);
	}

	return &vfb->fb;
}

static const struct drm_mode_config_funcs vdrm_mode_config_funcs = {
	.fb_create = vdrm_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static struct drm_display_mode *vdrm_create_mode(struct vdrm_display *disp)
{
	struct drm_display_mode *mode;
	struct videomode videomode;

	mode = drm_mode_create(&disp->dev->ddev);
	if (!mode)
		return NULL;

	memset(&videomode, 0, sizeof(videomode));
	videomode.hactive = disp->plane_info.width;
	videomode.vactive = disp->plane_info.height;
	videomode.pixelclock =
		disp->parent_crtc->state->adjusted_mode.crtc_clock * 1000;
	mode->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
	drm_display_mode_from_videomode(&videomode, mode);

	return mode;
}

static int vdrm_connector_get_mode(struct drm_connector *connector)
{
	struct vdrm_display *disp = to_vdrm_display(connector);
	struct drm_display_mode *mode = vdrm_create_mode(disp);

	if (!mode)
		return 0;

	drm_mode_probed_add(connector, mode);
	return 1;
}

static const struct drm_connector_helper_funcs vdrm_conn_helper_funcs = {
	.get_modes = vdrm_connector_get_mode,
};

/*
 * TODO:
 *  At the time this callback is called, the parent CRTC must be connected.
 *  Since this callback will not be called when detect() callback of the
 *  parent connector is called, vDRM driver desn't support hotplug.
 *  In the future, it is necessary that hotplug is supported.
 */
static enum drm_connector_status
vdrm_connector_detect(struct drm_connector *connector, bool force)
{
	struct vdrm_display *disp = to_vdrm_display(connector);
	struct vdrm_device *vdrm = to_vdrm_device(connector->dev);
	struct drm_connector *conn;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(vdrm->parent, &conn_iter);
	drm_for_each_connector_iter(conn, &conn_iter) {
		if (!conn->state)
			continue;

		if (conn->state->crtc == disp->parent_crtc) {
			drm_connector_list_iter_end(&conn_iter);
			return connector_status_connected;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	return connector_status_disconnected;
}

static const struct drm_connector_funcs vdrm_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = vdrm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void vdrm_drv_finish_page_flip_internal(struct vdrm_display *disp)
{
	struct drm_device *dev = &disp->dev->ddev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = disp->event;
	disp->event = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (event == NULL)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_crtc_send_vblank_event(&disp->crtc, event);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (disp->vblank_count) {
		drm_crtc_vblank_put(&disp->crtc);
		disp->vblank_count--;
	}
}

static void vdrm_plane_update(struct drm_plane *plane,
			      struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc *vcrtc_old_state = old_state->crtc;
	struct drm_crtc *vcrtc_plane_state = new_state->crtc;
	struct drm_crtc *crtc;
	struct vdrm_display *vdisplay;

	crtc = (old_state->crtc ? old_state->crtc : new_state->crtc);
	if (WARN_ON(!crtc))
		return;

	vdisplay = crtc_to_vdrm_display(crtc);

	old_state->crtc = vdisplay->parent_crtc;
	new_state->crtc = vdisplay->parent_crtc;

	new_state->dst.x1 += vdisplay->plane_info.x;
	new_state->dst.y1 += vdisplay->plane_info.y;
	vdisplay->parent_plane_helper_funcs->atomic_update(plane, state);

	old_state->crtc = vcrtc_old_state;
	new_state->crtc = vcrtc_plane_state;
}

static struct vdrm_display *
vdrm_plane_find_display(struct vdrm_device *vdrm, struct drm_plane *plane)
{
	struct vdrm_display *disp;

	list_for_each_entry(disp, &vdrm->disps, head) {
		if (disp->plane == plane)
			return disp;
	}

	return NULL;
}

static void vdrm_plane_reset(struct drm_plane *plane)
{
	struct vdrm_device *vdrm = to_vdrm_device(plane->dev);
	struct vdrm_display *disp;

	disp = vdrm_plane_find_display(vdrm, plane);
	if (WARN_ON(!disp))
		return;

	disp->parent_plane_funcs->reset(plane);
	plane->state->zpos = disp->plane_info.z;
}

static struct drm_property *
vdrm_find_parent_property(struct vdrm_device *vdrm, struct drm_property *prop)
{
	int i;

	for (i = 0; i < vdrm->num_props; i++) {
		if (vdrm->props[i].prop == prop)
			return vdrm->props[i].parent_prop;
	}

	return NULL;
}

static int vdrm_plane_set_property(struct drm_plane *plane,
				   struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t val)
{
	struct vdrm_device *vdrm = to_vdrm_device(plane->dev);
	struct vdrm_display *disp;
	struct drm_property *parent_prop;

	disp = vdrm_plane_find_display(vdrm, plane);
	if (WARN_ON(!disp))
		return -EINVAL;

	parent_prop = vdrm_find_parent_property(vdrm, property);
	if (parent_prop && disp->parent_plane_funcs->atomic_set_property)
		return disp->parent_plane_funcs->atomic_set_property(plane,
								state,
								parent_prop,
								val);

	if (vdrm->plane_props.offset_x == property) {
		if (val > disp->parent_crtc->mode.hdisplay)
			return -EINVAL;
		disp->plane_info.x = val;
	} else if (vdrm->plane_props.offset_y == property) {
		if (val > disp->parent_crtc->mode.vdisplay)
			return -EINVAL;
		disp->plane_info.y = val;
	} else if (vdrm->plane_props.width == property) {
		if (val > disp->parent_crtc->mode.hdisplay)
			return -EINVAL;
		disp->plane_info.width = val;
	} else if (vdrm->plane_props.height == property) {
		if (val > disp->parent_crtc->mode.vdisplay)
			return -EINVAL;
		disp->plane_info.height = val;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int vdrm_plane_get_property(struct drm_plane *plane,
				   const struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t *val)
{
	struct vdrm_device *vdrm = to_vdrm_device(plane->dev);
	struct vdrm_display *disp;
	struct drm_property *parent_prop;

	disp = vdrm_plane_find_display(vdrm, plane);
	if (WARN_ON(!disp))
		return -EINVAL;

	parent_prop = vdrm_find_parent_property(vdrm, property);
	if (parent_prop && disp->parent_plane_funcs->atomic_get_property)
		return disp->parent_plane_funcs->atomic_get_property(plane,
								state,
								parent_prop,
								val);

	if (vdrm->plane_props.offset_x == property)
		*val = disp->plane_info.x;
	else if (vdrm->plane_props.offset_y == property)
		*val = disp->plane_info.y;
	else if (vdrm->plane_props.width == property)
		*val = disp->plane_info.width;
	else if (vdrm->plane_props.height == property)
		*val = disp->plane_info.height;
	else
		return -EINVAL;

	return 0;
}

static int vdrm_crtc_check(struct drm_crtc *crtc,
			   struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	bool has_primary = crtc_state->plane_mask &
				drm_plane_mask(crtc->primary);

	/* We always want to have an active plane with an active CRTC */
	if (has_primary != crtc_state->enable)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state, crtc);
}

static void vdrm_crtc_flush(struct drm_crtc *crtc,
			    struct drm_atomic_state *state)
{
	struct vdrm_display *disp = crtc_to_vdrm_display(crtc);
	struct vdrm_device *vdrm = disp->dev;

	if (crtc->state->event) {
		struct drm_device *dev = crtc->dev;
		unsigned long flags;

		if (disp->crtc_enabled) {
			WARN_ON(drm_crtc_vblank_get(crtc) != 0);
			disp->vblank_count++;
		}

		spin_lock_irqsave(&dev->event_lock, flags);
		disp->event = crtc->state->event;
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	if (vdrm->funcs->crtc_flush)
		vdrm->funcs->crtc_flush(disp->parent_crtc);
}

static void vdrm_crtc_enable(struct drm_crtc *crtc,
			     struct drm_atomic_state *state)
{
	struct vdrm_display *disp = crtc_to_vdrm_display(crtc);

	drm_crtc_vblank_on(crtc);
	disp->crtc_enabled = true;
}

static void vdrm_crtc_disable(struct drm_crtc *crtc,
			      struct drm_atomic_state *state)
{
	struct vdrm_display *disp = crtc_to_vdrm_display(crtc);
	unsigned long flags;
	bool pending;

	disp->crtc_enabled = false;
	drm_crtc_vblank_off(crtc);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	pending = disp->event != NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	if (!wait_event_timeout(disp->flip_wait, !pending,
				msecs_to_jiffies(50))) {
		DRM_WARN("VDRM: page flip timeout\n");
		vdrm_drv_finish_page_flip_internal(disp);
	}

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static const struct drm_crtc_helper_funcs vdrm_crtc_helper_funcs = {
	.atomic_check = vdrm_crtc_check,
	.atomic_flush = vdrm_crtc_flush,
	.atomic_enable = vdrm_crtc_enable,
	.atomic_disable = vdrm_crtc_disable,
};

static int vdrm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct vdrm_display *disp;

	disp = crtc_to_vdrm_display(crtc);
	disp->vblank_enabled = true;

	return 0;
}

static void vdrm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct vdrm_display *disp;

	disp = crtc_to_vdrm_display(crtc);
	disp->vblank_enabled = false;
}

static const struct drm_crtc_funcs vdrm_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = vdrm_crtc_enable_vblank,
	.disable_vblank = vdrm_crtc_disable_vblank,
};
static const struct drm_encoder_funcs vdrm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int vdrm_properties_init(struct vdrm_device *vdrm, int num_props,
				struct vdrm_property_info *props)
{
	int i;
	unsigned int w = vdrm->ddev.mode_config.max_width;
	unsigned int h = vdrm->ddev.mode_config.max_height;

	vdrm->plane_props.offset_x =
		drm_property_create_range(&vdrm->ddev, 0, "vdrm_offset_x", 0, w);
	if (!vdrm->plane_props.offset_x)
		return -1;
	vdrm->plane_props.offset_y =
		drm_property_create_range(&vdrm->ddev, 0, "vdrm_offset_y", 0, h);
	if (!vdrm->plane_props.offset_y)
		return -1;
	vdrm->plane_props.width =
		drm_property_create_range(&vdrm->ddev, 0, "vdrm_width", 1, w);
	if (!vdrm->plane_props.width)
		return -1;
	vdrm->plane_props.height =
		drm_property_create_range(&vdrm->ddev, 0, "vdrm_height", 1, h);
	if (!vdrm->plane_props.height)
		return -1;

	if (num_props == 0)
		return 0;

	vdrm->props = devm_kzalloc(vdrm->parent->dev,
				    sizeof(*vdrm->props) * num_props,
				    GFP_KERNEL);
	if (!vdrm->props)
		return -ENOMEM;

	for (i = 0; i < num_props; i++) {
		struct drm_property *p = props[i].prop;

		vdrm->props[i].prop =
			drm_property_create_range(&vdrm->ddev, p->flags,
						  p->name, p->values[0],
						  p->values[1]);
		if (!vdrm->props[i].prop)
			goto err;

		vdrm->props[i].parent_prop = p;
		vdrm->props[i].default_val = props[i].default_val;
	}
	vdrm->num_props = num_props;

	return 0;

err:
	for (i--; i >= 0; i--)
		drm_property_destroy(&vdrm->ddev, vdrm->props[i].prop);
	devm_kfree(vdrm->parent->dev, vdrm->props);
	return -1;
}

static int vdrm_of_get_plane(struct device_node *np,
			     int *x, int *y, int *width, int *height, int *z)
{
	struct device_node *child;
	int ret;

	child = of_get_next_child(np, NULL);
	if (!child)
		return -ENODEV;

	ret = of_property_read_u32(child, "x", x);
	ret |= of_property_read_u32(child, "y", y);
	ret |= of_property_read_u32(child, "width", width);
	ret |= of_property_read_u32(child, "height", height);
	ret |= of_property_read_u32(child, "zpos", z);

	of_node_put(child);
	return ret;
}

static void vdrm_dump(struct vdrm_device *vdrm)
{
	struct vdrm_display *disp;

	DRM_INFO("Virtual DRM Info:\n");
	list_for_each_entry(disp, &vdrm->disps, head) {
		DRM_INFO("\tCONNECTOR: %d\n",
			 disp->connector.base.id);
		DRM_INFO("\tCRTC: %d\n",
			 disp->crtc.base.id);
		DRM_INFO("\tENCODER: %d\n",
			 disp->encoder.base.id);
		DRM_INFO("\tPLANE: %d\n",
			 disp->plane->base.id);
		DRM_INFO("\tParent CRTC: %d\n",
			 disp->parent_crtc->base.id);
	}
}

/**
 * vdrm_drv_handle_vblank - handle a vblank event for vDRM
 * @vdisplay: vDRM display object
 */
void vdrm_drv_handle_vblank(struct vdrm_display *vdisplay)
{
	if (vdisplay->vblank_enabled)
		drm_crtc_handle_vblank(&vdisplay->crtc);
}

/**
 * vdrm_drv_finish_page_flip - handle a page flip event for vDRM
 * @vdisplay: vDRM display object
 */
void vdrm_drv_finish_page_flip(struct vdrm_display *vdisplay)
{
	vdrm_drv_finish_page_flip_internal(vdisplay);
}

DEFINE_DRM_GEM_CMA_FOPS(vdrm_fops);

static struct drm_driver vdrm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.dumb_create = vdrm_dumb_create,
	.fops = &vdrm_fops,
	.name = "virt-drm",
	.desc = "Virtual DRM driver",
	.date = "20201104",
	.major = 1,
	.minor = 0,
};

/**
 * vdrm_drv_init - Initialize vDRM device
 * @dev: parent DRM device
 * @np: vDRM device node in DTB
 * @num_props: number of parent property objects
 * @props: parent plane properties used in vDRM
 * @funcs: callbacks for vDRM
 *
 * Allocates a new vDRM device, initializes mode_config of the vDRM device
 * and allocates property objects. Not initialize plane, crtc, encoder and
 * connector. Initialization of plane must be done in vdrm_drv_plane_init()
 * and initialization of crtc, encoder and connector must be done by
 * vdrm_drv_display_init(). Also, after initializing the plane, crtc,
 * connector, and encoder, register vDRM device must be done by
 * vdrm_drv_register().
 *
 * Returns:
 * vDRM object (&vdrm_device) on success, error code encoded into the pointer
 * on failure.
 */
struct vdrm_device *vdrm_drv_init(struct drm_device *dev,
				  struct device_node *np, int num_props,
				  struct vdrm_property_info *props,
				  const struct vdrm_funcs *funcs)
{
	struct vdrm_device *vdrm;
	struct vdrm_plane_info plane_info;
	int ret;

	if (!of_device_is_compatible(np, "virt-drm"))
		return ERR_PTR(-ENODEV);

	/* get plane information from device tree */
	ret = vdrm_of_get_plane(np, &plane_info.x, &plane_info.y,
				&plane_info.width, &plane_info.height,
				&plane_info.z);
	if (ret < 0) {
		DRM_WARN("VDRM: failed get plane node of %s\n",
			 of_node_full_name(np));
		return ERR_PTR(ret);
	}

	vdrm_driver.prime_handle_to_fd = dev->driver->prime_handle_to_fd;
	vdrm_driver.prime_fd_to_handle = dev->driver->prime_fd_to_handle;
	vdrm_driver.gem_prime_import_sg_table =
		dev->driver->gem_prime_import_sg_table;
	vdrm_driver.gem_prime_mmap = dev->driver->gem_prime_mmap;

	vdrm = devm_drm_dev_alloc(dev->dev, &vdrm_driver, struct vdrm_device,
				  ddev);
	if (IS_ERR(vdrm))
		return vdrm;

	vdrm->parent = dev;
	vdrm->funcs = funcs;
	vdrm->of_plane_info = plane_info;

	INIT_LIST_HEAD(&vdrm->disps);

	ret = drmm_mode_config_init(&vdrm->ddev);
	if (ret)
		goto failed;

	vdrm->ddev.mode_config.min_width = 0;
	vdrm->ddev.mode_config.min_height = 0;
	vdrm->ddev.mode_config.max_width = 8190;
	vdrm->ddev.mode_config.max_height = 8190;
	vdrm->ddev.mode_config.normalize_zpos = true;
	vdrm->ddev.mode_config.funcs = &vdrm_mode_config_funcs;

	ret = vdrm_properties_init(vdrm, num_props, props);
	if (ret < 0)
		goto failed;

	drm_dev_set_unique(&vdrm->ddev, of_node_full_name(np));
	return vdrm;

failed:
	kfree(vdrm);
	return ERR_PTR(ret);
}

/**
 * vdrm_drv_plane_init - Initialize the plane used by vDRM
 * @vdrm: vDRM object
 * @plane: plane to assign to vDRM
 * @funcs: callbacks for the plane
 * @helper_funcs: helper vtable to set for plane
 * @formats: color formats
 * @num_formats: number of color formats
 * @max_zpos: max value for zpos property of plane
 *
 * Initializes a plane object of PRIMARY type by drm_universal_plane_init()
 * and initializes @plane's properties. The property passed by vdrm_drv_init()
 * is set to @plane.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int vdrm_drv_plane_init(struct vdrm_device *vdrm, struct drm_plane *plane,
			const struct drm_plane_funcs *funcs,
			const struct drm_plane_helper_funcs *helper_funcs,
			const u32 *formats, unsigned int num_formats,
			int max_zpos)
{
	struct vdrm_display *disp;
	int i, ret;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	disp->dev = vdrm;
	disp->plane = plane;
	disp->plane_info = vdrm->of_plane_info;

	disp->parent_plane_funcs = funcs;
	disp->parent_plane_helper_funcs = helper_funcs;
	disp->plane_funcs = *funcs;
	disp->plane_helper_funcs = *helper_funcs;

	disp->plane_funcs.reset = vdrm_plane_reset;
	disp->plane_funcs.atomic_set_property = vdrm_plane_set_property;
	disp->plane_funcs.atomic_get_property = vdrm_plane_get_property;
	disp->plane_helper_funcs.atomic_update = vdrm_plane_update;

	drm_plane_helper_add(disp->plane, &disp->plane_helper_funcs);
	ret = drm_universal_plane_init(&vdrm->ddev, plane, 0,
				       &disp->plane_funcs, formats,
				       num_formats, NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		kfree(disp);
		return ret;
	}

	drm_plane_create_alpha_property(plane);
	drm_plane_create_zpos_property(plane, disp->plane_info.z, 0, max_zpos);
	drm_object_attach_property(&plane->base,
				   vdrm->plane_props.offset_x,
				   disp->plane_info.x);
	drm_object_attach_property(&plane->base,
				   vdrm->plane_props.offset_y,
				   disp->plane_info.y);
	drm_object_attach_property(&plane->base,
				   vdrm->plane_props.width,
				   disp->plane_info.width);
	drm_object_attach_property(&plane->base,
				   vdrm->plane_props.height,
				   disp->plane_info.height);
	for (i = 0; i < vdrm->num_props; i++) {
		drm_object_attach_property(&plane->base,
					   vdrm->props[i].prop,
					   vdrm->props[i].default_val);
	}

	INIT_LIST_HEAD(&disp->head);
	list_add_tail(&disp->head, &vdrm->disps);
	vdrm->num_crtcs++;
	return 0;
}

/**
 * vdrm_drv_display_init - Initialize the vDRM display object
 * @vdrm: vDRM object
 * @crtc: parent crtc to be linked with the vDRM crtc
 * @plane: plane assigned to vDRM
 *
 * Initializes crtc, connector and encorder, and links @crtc to crtc of vDRM.
 *
 * Returns:
 * vDRM display object on success, error code encoded into the pointer on
 * failure.
 */
struct vdrm_display *vdrm_drv_display_init(struct vdrm_device *vdrm,
					   struct drm_crtc *crtc,
					   struct drm_plane *plane)
{
	struct vdrm_display *disp;
	int ret;

	disp = vdrm_plane_find_display(vdrm, plane);
	if (!disp)
		return ERR_PTR(-EINVAL);

	drm_crtc_helper_add(&disp->crtc, &vdrm_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(&vdrm->ddev, &disp->crtc, plane, NULL,
					&vdrm_crtc_funcs, NULL);
	if (ret)
		return ERR_PTR(ret);

	drm_connector_helper_add(&disp->connector, &vdrm_conn_helper_funcs);
	ret = drm_connector_init(&vdrm->ddev, &disp->connector,
				 &vdrm_conn_funcs, DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ERR_PTR(ret);

	disp->encoder.possible_crtcs = drm_crtc_mask(&disp->crtc);
	ret = drm_encoder_init(&vdrm->ddev, &disp->encoder, &vdrm_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_connector_attach_encoder(&disp->connector, &disp->encoder);
	if (ret)
		return ERR_PTR(ret);

	init_waitqueue_head(&disp->flip_wait);
	disp->parent_crtc = crtc;

	return disp;
}

/**
 * vdrm_drv_register - Register vDRM device
 * @vdrm: vDRM object
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int vdrm_drv_register(struct vdrm_device *vdrm)
{
	int ret;
	struct drm_device *dev = &vdrm->ddev;

	ret = drm_vblank_init(dev, vdrm->num_crtcs);
	if (ret)
		return ret;

	drm_mode_config_reset(dev);

	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	dev->irq_enabled = true;

	DRM_INFO("Virtual Device is initialized.\n");

	vdrm_dump(vdrm);

	return 0;
}

/**
 * vdrm_drv_fini - release vDRM resources
 * @vdrm: vDRM object
 */
void vdrm_drv_fini(struct vdrm_device *vdrm)
{
	struct vdrm_display *disp;

	if (vdrm->ddev.registered)
		drm_dev_unregister(&vdrm->ddev);
	drm_mode_config_cleanup(&vdrm->ddev);

	list_for_each_entry(disp, &vdrm->disps, head)
		kfree(disp);
}
