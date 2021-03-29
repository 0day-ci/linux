// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2017-2020 NXP
 */

#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_simple_kms_helper.h>

#include "dpu.h"
#include "dpu-crtc.h"
#include "dpu-drv.h"
#include "dpu-kms.h"
#include "dpu-plane.h"

static int zpos_cmp(const void *a, const void *b)
{
	const struct drm_plane_state *sa = *(struct drm_plane_state **)a;
	const struct drm_plane_state *sb = *(struct drm_plane_state **)b;

	return sa->normalized_zpos - sb->normalized_zpos;
}

static int dpu_atomic_sort_planes_per_crtc(struct drm_crtc_state *crtc_state,
					   struct drm_plane_state **plane_states)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_plane *plane;
	int n = 0;

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state) {
		struct drm_plane_state *plane_state =
			drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);
		plane_states[n++] = plane_state;
	}

	sort(plane_states, n, sizeof(*plane_states), zpos_cmp, NULL);

	return n;
}

static void
dpu_atomic_set_top_plane_per_crtc(struct drm_plane_state **plane_states, int n)
{
	struct dpu_plane_state *dpstate;
	int i;

	for (i = 0; i < n; i++) {
		dpstate = to_dpu_plane_state(plane_states[i]);
		dpstate->is_top = (i == (n - 1)) ? true : false;
	}
}

static int
dpu_atomic_assign_plane_source_per_crtc(struct dpu_crtc *dpu_crtc,
					struct drm_plane_state **plane_states,
					int n, bool use_current_source)
{
	struct drm_plane_state *plane_state;
	struct dpu_plane_state *dpstate;
	struct dpu_plane *dplane;
	struct dpu_plane_grp *grp;
	struct dpu_plane_res *res;
	struct drm_framebuffer *fb;
	struct dpu_fetchunit *fu;
	struct list_head *node;
	const struct dpu_fetchunit_ops *fu_ops;
	union dpu_plane_stage stage;
	struct dpu_layerblend *blend;
	unsigned int sid = dpu_crtc->stream_id;
	int i, j;
	u32 src_w, src_h, dst_w, dst_h;
	u32 cap_mask;
	bool fb_is_packed_yuv422, fb_is_interlaced;
	bool need_fetcheco, need_hscaler, need_vscaler;
	bool found_fu;

	/* for active planes only */
	for (i = 0; i < n; i++) {
		plane_state = plane_states[i];
		dpstate = to_dpu_plane_state(plane_state);

		/*
		 * If modeset is not allowed, use the current source for
		 * the prone-to-put planes so that unnecessary updates and
		 * spurious EBUSY can be avoided.
		 */
		if (use_current_source) {
			fu_ops = dpu_fu_get_ops(dpstate->source);
			fu_ops->set_inavailable(dpstate->source);
			continue;
		}

		dplane = to_dpu_plane(plane_state->plane);
		fb = plane_state->fb;
		grp = dplane->grp;
		res = &grp->res;

		src_w = plane_state->src_w >> 16;
		src_h = plane_state->src_h >> 16;
		dst_w = plane_state->crtc_w;
		dst_h = plane_state->crtc_h;

		fb_is_packed_yuv422 =
				drm_format_info_is_yuv_packed(fb->format) &&
				drm_format_info_is_yuv_sampling_422(fb->format);
		fb_is_interlaced = !!(fb->flags & DRM_MODE_FB_INTERLACED);
		need_fetcheco = fb->format->num_planes > 1;
		need_hscaler = src_w != dst_w;
		need_vscaler = (src_h != dst_h) || fb_is_interlaced;

		cap_mask = 0;
		if (need_fetcheco)
			cap_mask |= DPU_FETCHUNIT_CAP_USE_FETCHECO;
		if (need_hscaler || need_vscaler)
			cap_mask |= DPU_FETCHUNIT_CAP_USE_SCALER;
		if (fb_is_packed_yuv422)
			cap_mask |= DPU_FETCHUNIT_CAP_PACKED_YUV422;

		/* assign source */
		found_fu = false;
		list_for_each(node, &grp->fu_list) {
			fu = dpu_fu_get_from_list(node);

			fu_ops = dpu_fu_get_ops(fu);

			/* available? */
			if (!fu_ops->is_available(fu))
				continue;

			/* enough capability? */
			if ((cap_mask & fu_ops->get_cap_mask(fu)) != cap_mask)
				continue;

			/* avoid fetchunit hot migration */
			if (fu_ops->has_stream_id(fu) &&
			    fu_ops->get_stream_id(fu) != sid)
				continue;

			fu_ops->set_inavailable(fu);
			found_fu = true;
			break;
		}

		if (!found_fu)
			return -EINVAL;

		dpstate->source = fu;

		/* assign stage and blend */
		if (sid) {
			j = grp->hw_plane_cnt - (n - i);
			blend = res->lb[j];
			if (i == 0)
				stage.cf = grp->cf[sid];
			else
				stage.lb = res->lb[j - 1];
		} else {
			blend = res->lb[i];
			if (i == 0)
				stage.cf = grp->cf[sid];
			else
				stage.lb = res->lb[i - 1];
		}

		dpstate->stage = stage;
		dpstate->blend = blend;
	}

	return 0;
}

static int dpu_atomic_assign_plane_source(struct drm_atomic_state *state,
					  u32 crtc_mask_prone_to_put,
					  bool prone_to_put)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct dpu_crtc *dpu_crtc;
	struct drm_plane_state **plane_states;
	bool use_current_source;
	int i, n;
	int ret = 0;

	use_current_source = !state->allow_modeset && prone_to_put;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		/* Skip if no active plane. */
		if (crtc_state->plane_mask == 0)
			continue;

		if (prone_to_put !=
		    !!(drm_crtc_mask(crtc) & crtc_mask_prone_to_put))
			continue;

		dpu_crtc = to_dpu_crtc(crtc);

		plane_states = kmalloc_array(dpu_crtc->hw_plane_cnt,
					     sizeof(*plane_states), GFP_KERNEL);
		if (!plane_states) {
			ret = -ENOMEM;
			dpu_crtc_dbg(crtc,
				     "failed to alloc plane state ptrs: %d\n",
									ret);
			return ret;
		}

		n = dpu_atomic_sort_planes_per_crtc(crtc_state, plane_states);
		if (n < 0) {
			dpu_crtc_dbg(crtc, "failed to sort planes: %d\n", n);
			kfree(plane_states);
			return n;
		}

		dpu_atomic_set_top_plane_per_crtc(plane_states, n);

		ret = dpu_atomic_assign_plane_source_per_crtc(dpu_crtc,
					plane_states, n, use_current_source);
		if (ret) {
			dpu_crtc_dbg(crtc,
				     "failed to assign resource to plane: %d\n",
									ret);
			kfree(plane_states);
			return ret;
		}

		kfree(plane_states);
	}

	return ret;
}

static void dpu_atomic_put_plane_state(struct drm_atomic_state *state,
				       struct drm_plane *plane)
{
	int index = drm_plane_index(plane);

	plane->funcs->atomic_destroy_state(plane, state->planes[index].state);
	state->planes[index].ptr = NULL;
	state->planes[index].state = NULL;
	state->planes[index].old_state = NULL;
	state->planes[index].new_state = NULL;

	drm_modeset_unlock(&plane->mutex);

	dpu_plane_dbg(plane, "put state\n");
}

static void dpu_atomic_put_crtc_state(struct drm_atomic_state *state,
				      struct drm_crtc *crtc)
{
	int index = drm_crtc_index(crtc);

	crtc->funcs->atomic_destroy_state(crtc, state->crtcs[index].state);
	state->crtcs[index].ptr = NULL;
	state->crtcs[index].state = NULL;
	state->crtcs[index].old_state = NULL;
	state->crtcs[index].new_state = NULL;

	drm_modeset_unlock(&crtc->mutex);

	dpu_crtc_dbg(crtc, "put state\n");
}

static void
dpu_atomic_put_possible_states_per_crtc(struct drm_crtc_state *crtc_state)
{
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_crtc *crtc = crtc_state->crtc;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct dpu_plane_state *old_dpstate, *new_dpstate;

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state) {
		old_plane_state = drm_atomic_get_old_plane_state(state, plane);
		new_plane_state = drm_atomic_get_new_plane_state(state, plane);

		old_dpstate = to_dpu_plane_state(old_plane_state);
		new_dpstate = to_dpu_plane_state(new_plane_state);

		/* Should be enough to check the below HW plane resources. */
		if (old_dpstate->stage.ptr != new_dpstate->stage.ptr ||
		    old_dpstate->source != new_dpstate->source ||
		    old_dpstate->blend != new_dpstate->blend)
			return;
	}

	drm_atomic_crtc_state_for_each_plane(plane, crtc_state)
		dpu_atomic_put_plane_state(state, plane);

	dpu_atomic_put_crtc_state(state, crtc);
}

static int dpu_drm_atomic_check(struct drm_device *dev,
				struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct dpu_crtc *dpu_crtc;
	struct dpu_plane_grp *plane_grp;
	struct dpu_fetchunit *fu;
	struct list_head *node;
	const struct dpu_fetchunit_ops *fu_ops;
	u32 crtc_mask_in_state = 0;
	u32 crtc_mask_in_grps = 0;
	u32 crtc_mask_prone_to_put;
	int ret, i;

	ret = drm_atomic_helper_check_modeset(dev, state);
	if (ret)
		return ret;

	/* Set crtc_mask_in_state and crtc_mask_in_grps. */
	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		dpu_crtc = to_dpu_crtc(crtc);

		crtc_mask_in_state |= drm_crtc_mask(crtc);
		crtc_mask_in_grps |= dpu_crtc->grp->crtc_mask;
	}

	/*
	 * Those CRTCs in groups but not in the state for check
	 * are prone to put, because HW resources of their active
	 * planes are likely unchanged.
	 */
	crtc_mask_prone_to_put = crtc_mask_in_grps ^ crtc_mask_in_state;

	/*
	 * For those CRTCs prone to put, get their CRTC states as well,
	 * so that all relevant active plane states can be got when
	 * assigning HW resources to them later on.
	 */
	drm_for_each_crtc(crtc, dev) {
		if ((drm_crtc_mask(crtc) & crtc_mask_prone_to_put) == 0)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	/*
	 * Set all the fetchunits in the plane groups in question
	 * to be available, so that they can be assigned to planes.
	 */
	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		dpu_crtc = to_dpu_crtc(crtc);

		/* Skip the CRTC with stream ID1 in a CRTC group. */
		if (dpu_crtc->stream_id == 1)
			continue;

		plane_grp = dpu_crtc->grp->plane_grp;

		list_for_each(node, &plane_grp->fu_list) {
			fu = dpu_fu_get_from_list(node);
			fu_ops = dpu_fu_get_ops(fu);
			fu_ops->set_available(fu);
		}
	}

	ret = drm_atomic_normalize_zpos(dev, state);
	if (ret) {
		drm_dbg_kms(dev, "failed to normalize zpos: %d\n", ret);
		return ret;
	}

	/*
	 * Assign HW resources to planes in question.
	 * It is likely to fail due to some reasons, e.g., no enough
	 * fetchunits, users ask for more features than the HW resources
	 * can provide, HW resource hot-migration bewteen CRTCs is needed.
	 *
	 * Do the assignment for the prone-to-put CRTCs first, as we want
	 * the planes of them to use the current sources if modeset is not
	 * allowed.
	 */
	ret = dpu_atomic_assign_plane_source(state,
					     crtc_mask_prone_to_put, true);
	if (ret) {
		drm_dbg_kms(dev,
			"failed to assign source to prone-to-put plane: %d\n",
									ret);
		return ret;
	}
	ret = dpu_atomic_assign_plane_source(state,
					     crtc_mask_prone_to_put, false);
	if (ret) {
		drm_dbg_kms(dev, "failed to assign source to plane: %d\n", ret);
		return ret;
	}

	/*
	 * To gain some performance, put those CRTC and plane states
	 * which can be put.
	 */
	drm_for_each_crtc(crtc, dev) {
		if (crtc_mask_prone_to_put & drm_crtc_mask(crtc)) {
			crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
			if (WARN_ON(!crtc_state))
				return -EINVAL;

			dpu_atomic_put_possible_states_per_crtc(crtc_state);
		}
	}

	return drm_atomic_helper_check_planes(dev, state);
}

static const struct drm_mode_config_funcs dpu_drm_mode_config_funcs = {
	.fb_create	= drm_gem_fb_create,
	.atomic_check	= dpu_drm_atomic_check,
	.atomic_commit	= drm_atomic_helper_commit,
};

static int dpu_kms_init_encoder_per_crtc(struct drm_device *drm,
					 struct dpu_crtc *dpu_crtc)
{
	struct drm_encoder *encoder = dpu_crtc->encoder;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	struct device_node *ep, *remote;
	int ret = 0;

	ep = of_get_next_child(dpu_crtc->np, NULL);
	if (!ep) {
		drm_err(drm, "failed to find CRTC port's endpoint\n");
		return -ENODEV;
	}

	remote = of_graph_get_remote_port_parent(ep);
	if (!remote || !of_device_is_available(remote))
		goto out;
	else if (!of_device_is_available(remote->parent))
		goto out;

	bridge = of_drm_find_bridge(remote);
	if (!bridge) {
		ret = -EPROBE_DEFER;
		drm_dbg_kms(drm, "CRTC(%pOF) failed to find bridge: %d\n",
							dpu_crtc->np, ret);
		goto out;
	}

	ret = drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_NONE);
	if (ret) {
		drm_err(drm, "failed to initialize encoder: %d\n", ret);
		goto out;
	}

	ret = drm_bridge_attach(encoder, bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		drm_err(drm, "failed to attach bridge to encoder: %d\n", ret);
		goto out;
	}

	connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(connector)) {
		ret = PTR_ERR(connector);
		drm_err(drm,
			"failed to initialize bridge connector: %d\n", ret);
		goto out;
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		drm_err(drm,
			"failed to attach encoder to connector: %d\n", ret);

out:
	of_node_put(remote);
	of_node_put(ep);
	return ret;
}

int dpu_kms_prepare(struct dpu_drm_device *dpu_drm,
		    struct list_head *crtc_np_list)
{
	struct drm_device *drm = &dpu_drm->base;
	struct dpu_crtc_of_node *crtc_of_node;
	struct dpu_crtc *crtc;
	int ret, n_crtc = 0;

	INIT_LIST_HEAD(&dpu_drm->crtc_list);

	list_for_each_entry(crtc_of_node, crtc_np_list, list) {
		crtc = drmm_kzalloc(drm, sizeof(*crtc), GFP_KERNEL);
		if (!crtc)
			return -ENOMEM;

		crtc->np = crtc_of_node->np;

		crtc->encoder = drmm_kzalloc(drm, sizeof(*crtc->encoder),
								GFP_KERNEL);
		if (!crtc->encoder)
			return -ENOMEM;

		list_add(&crtc->node, &dpu_drm->crtc_list);

		n_crtc++;
	}

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 60;
	drm->mode_config.min_height = 60;
	drm->mode_config.max_width = 8192;
	drm->mode_config.max_height = 8192;
	drm->mode_config.funcs = &dpu_drm_mode_config_funcs;
	drm->mode_config.normalize_zpos = true;
	drm->max_vblank_count = DPU_FRAMEGEN_MAX_FRAME_INDEX;

	list_for_each_entry(crtc, &dpu_drm->crtc_list, node) {
		ret = dpu_kms_init_encoder_per_crtc(drm, crtc);
		if (ret)
			return ret;
	}

	ret = drm_vblank_init(drm, n_crtc);
	if (ret)
		drm_err(drm, "failed to initialize vblank support: %d\n", ret);

	return ret;
}
