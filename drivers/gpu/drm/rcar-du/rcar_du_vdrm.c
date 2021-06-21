// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_du_vdrm.c -- R-Car Display Unit Virtual DRMs
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */

#include <linux/of_device.h>

#include <drm/drm_print.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <media/vsp1.h>

#include "rcar_du_vdrm.h"
#include "rcar_du_kms.h"
#include "rcar_du_crtc.h"
#include "rcar_du_vsp.h"

static int rcar_du_vdrm_dumb_create(struct drm_file *file,
				    struct drm_device *dev,
				    struct drm_mode_create_dumb *args)
{
	/*
	 * TODO:
	 *   This is Warkarround.
	 *   In the future, this function will be removed.
	 *   The vdrm will be modified to directly call the dumb_create
	 *   callback of the du driver.
	 */
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	unsigned int align;

	/*
	 * The R8A7779 DU requires a 16 pixels pitch alignment as documented.
	 */
	align = 16 * args->bpp / 8;

	args->pitch = roundup(min_pitch, align);

	return drm_gem_cma_dumb_create_internal(file, dev, args);
}

static void rcar_du_vdrm_crtc_flush(struct drm_crtc *crtc)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_du_vsp_atomic_flush(rcrtc);
}

static struct vdrm_funcs vdrm_funcs = {
	.dumb_create = rcar_du_vdrm_dumb_create,
	.crtc_flush = rcar_du_vdrm_crtc_flush,
};

void rcar_du_vdrm_crtc_complete(struct rcar_du_crtc *crtc, unsigned int status)
{
	struct rcar_du_vdrm_display *disp;

	list_for_each_entry(disp, &crtc->vdrm_displays, head) {
		vdrm_drv_handle_vblank(disp->display);
		if (status & VSP1_DU_STATUS_COMPLETE)
			vdrm_drv_finish_page_flip(disp->display);
	}
}

int rcar_du_vdrm_count(struct rcar_du_device *rcdu)
{
	const struct device_node *np = rcdu->dev->of_node;
	int num;

	num = of_property_count_u32_elems(np, "vdrms");
	if (num < 0)
		return 0;

	return num;
}

int rcar_du_vdrms_init(struct rcar_du_device *rcdu)
{
	struct vdrm_device *vdrm;
	int num_vdrms;
	int i, ret;

	num_vdrms = rcar_du_vdrm_count(rcdu);
	if (num_vdrms == 0)
		return 0;

	rcdu->vdrms = kcalloc(num_vdrms, sizeof(vdrm), GFP_KERNEL);
	if (!rcdu->vdrms)
		return -1;

	DRM_INFO("VDRM: num vdrm = %d\n", num_vdrms);

	for (i = 0; i < num_vdrms; i++) {
		struct of_phandle_args args;
		const struct device_node *np = rcdu->dev->of_node;

		ret = of_parse_phandle_with_fixed_args(np, "vdrms", 0, i,
						       &args);
		if (ret < 0) {
			DRM_WARN("VDRM: failed get vdrm%d.\n", i);
			goto err;
		}

		vdrm = vdrm_drv_init(&rcdu->ddev, args.np, 0, NULL,
				     &vdrm_funcs);
		of_node_put(args.np);
		if (IS_ERR(vdrm)) {
			ret = PTR_ERR(vdrm);
			goto err;
		}

		rcdu->vdrms[i] = vdrm;
		rcdu->num_vdrms++;
	}

	return 0;

err:
	rcar_du_vdrms_fini(rcdu);
	rcdu->num_vdrms = 0;
	return ret;
}

int rcar_du_vdrm_plane_init(struct vdrm_device *vdrm,
			    struct rcar_du_vsp_plane *plane,
			    const struct drm_plane_funcs *funcs,
			    const struct drm_plane_helper_funcs *helper_funcs,
			    const u32 *formats, unsigned int num_formats,
			    int max_zpos)
{
	return vdrm_drv_plane_init(vdrm, &plane->plane, funcs,
				   helper_funcs, formats, num_formats,
				   max_zpos);
}

int rcar_du_vdrm_crtc_init(struct rcar_du_crtc *crtc, int index)
{
	struct rcar_du_device *rcdu;
	int i;

	rcdu = crtc->dev;
	for (i = 0; i < rcdu->num_vdrms; i++) {
		struct vdrm_display *vdisplay;
		int plane_index = crtc->vsp->num_planes + i;
		struct drm_plane *plane =
			&crtc->vsp->planes[plane_index].plane;

		vdisplay = vdrm_drv_display_init(rcdu->vdrms[i], &crtc->crtc,
						 plane);
		if (IS_ERR(vdisplay))
			return PTR_ERR(vdisplay);

		rcar_du_crtc_add_vdrm_display(crtc, vdisplay);
	}

	return 0;
}

int rcar_du_vdrms_register(struct rcar_du_device *rcdu)
{
	int i, ret;

	for (i = 0; i < rcdu->num_vdrms; i++) {
		ret = vdrm_drv_register(rcdu->vdrms[i]);
		if (ret)
			return ret;
	}

	return 0;
}

void rcar_du_vdrms_fini(struct rcar_du_device *rcdu)
{
	int i;

	for (i = 0; i < rcdu->num_vdrms; i++) {
		if (rcdu->vdrms[i])
			vdrm_drv_fini(rcdu->vdrms[i]);
	}

	for (i = 0; i < RCAR_DU_MAX_CRTCS; i++)
		rcar_du_crtc_remove_vdrm_displays(&rcdu->crtcs[i]);

	kfree(rcdu->vdrms);
}
