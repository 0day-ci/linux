// SPDX-License-Identifier: GPL-2.0+
/*
 * A virtual stateless VPU example device for uAPI development purposes.
 *
 * A userspace implementation can use vivpu to run a decoding loop even
 * when no hardware is available or when the kernel uAPI for the codec
 * has not been upstreamed yet. This can reveal bugs at an early stage.
 *
 * Copyright (c) Collabora, Ltd.
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * Based on the vicodec driver, that is:
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * Based on the Cedrus VPU driver, that is:
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#include "vivpu.h"
#include "vivpu-dec.h"

#include <linux/delay.h>
#include <linux/workqueue.h>
#include <media/v4l2-mem2mem.h>
#include <media/tpg/v4l2-tpg.h>

static void
vivpu_av1_check_reference_frames(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	u32 i;
	int idx;
	const struct v4l2_ctrl_av1_frame_header *f;
	struct vb2_queue *capture_queue;

	f = run->av1.frame_header;
	capture_queue = &ctx->fh.m2m_ctx->cap_q_ctx.q;

	/*
	 * For every reference frame timestamp, make sure we can actually find
	 * the buffer in the CAPTURE queue.
	 */
	for (i = 0; i < V4L2_AV1_NUM_REF_FRAMES; i++) {
		idx = vb2_find_timestamp(capture_queue, f->reference_frame_ts[i], 0);
		if (idx < 0)
			v4l2_err(&ctx->dev->v4l2_dev,
				 "no capture buffer found for reference_frame_ts[%d] %llu",
				 i, f->reference_frame_ts[i]);
		else
			dprintk(ctx->dev,
				"found capture buffer %d for reference_frame_ts[%d] %llu\n",
				idx, i, f->reference_frame_ts[i]);
	}
}

static void vivpu_dump_av1_seq(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_ctrl_av1_sequence *seq = run->av1.sequence;

	dprintk(ctx->dev, "AV1 Sequence\n");
	dprintk(ctx->dev, "flags %d\n", seq->flags);
	dprintk(ctx->dev, "profile %d\n", seq->seq_profile);
	dprintk(ctx->dev, "order_hint_bits %d\n", seq->order_hint_bits);
	dprintk(ctx->dev, "bit_depth %d\n", seq->bit_depth);
	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_tile_group(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_ctrl_av1_tile_group *tg;
	u32 n;
	u32 i;

	n = vivpu_control_num_elems(ctx, V4L2_CID_STATELESS_AV1_TILE_GROUP);
	for (i = 0; i < n; i++) {
		tg = &run->av1.tile_group[i];
		dprintk(ctx->dev, "AV1 Tile Group\n");
		dprintk(ctx->dev, "flags %d\n", tg->flags);
		dprintk(ctx->dev, "tg_start %d\n", tg->tg_start);
		dprintk(ctx->dev, "tg_end %d\n", tg->tg_end);
		dprintk(ctx->dev, "\n");
	}

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_tile_group_entry(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_ctrl_av1_tile_group_entry *tge;
	u32 n;
	u32 i;

	n = vivpu_control_num_elems(ctx, V4L2_CID_STATELESS_AV1_TILE_GROUP_ENTRY);
	for (i = 0; i < n; i++) {
		tge = &run->av1.tg_entries[i];
		dprintk(ctx->dev, "AV1 Tile Group Entry\n");
		dprintk(ctx->dev, "tile_offset %d\n", tge->tile_offset);
		dprintk(ctx->dev, "tile_size %d\n", tge->tile_size);
		dprintk(ctx->dev, "tile_row %d\n", tge->tile_row);
		dprintk(ctx->dev, "tile_col %d\n", tge->tile_col);

		dprintk(ctx->dev, "\n");
	}

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_tile_list(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_ctrl_av1_tile_list *tl;
	u32 n;
	u32 i;

	n = vivpu_control_num_elems(ctx, V4L2_CID_STATELESS_AV1_TILE_LIST);
	for (i = 0; i < n; i++) {
		tl = &run->av1.tile_list[i];
		dprintk(ctx->dev, "AV1 Tile List\n");
		dprintk(ctx->dev, "output_frame_width_in_tiles_minus_1 %d\n",
			tl->output_frame_width_in_tiles_minus_1);
		dprintk(ctx->dev, "output_frame_height_in_tiles_minus_1 %d\n",
			tl->output_frame_height_in_tiles_minus_1);
		dprintk(ctx->dev, "tile_count_minus_1 %d\n",
			tl->tile_count_minus_1);
		dprintk(ctx->dev, "\n");
	}

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_tile_list_entry(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_ctrl_av1_tile_list_entry *tle;
	u32 n;
	u32 i;

	n = vivpu_control_num_elems(ctx, V4L2_CID_STATELESS_AV1_TILE_LIST_ENTRY);
	for (i = 0; i < n; i++) {
		tle = &run->av1.tl_entries[i];
		dprintk(ctx->dev, "AV1 Tile List Entry\n");
		dprintk(ctx->dev, "anchor_frame_idx %d\n", tle->anchor_frame_idx);
		dprintk(ctx->dev, "anchor_tile_row %d\n", tle->anchor_tile_row);
		dprintk(ctx->dev, "anchor_tile_col %d\n", tle->anchor_tile_col);
		dprintk(ctx->dev, "tile_data_size_minus_1 %d\n",
			tle->tile_data_size_minus_1);
		dprintk(ctx->dev, "\n");
	}

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_quantization(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_quantization *q = &run->av1.frame_header->quantization;

	dprintk(ctx->dev, "AV1 Quantization\n");
	dprintk(ctx->dev, "flags %d\n", q->flags);
	dprintk(ctx->dev, "base_q_idx %d\n", q->base_q_idx);
	dprintk(ctx->dev, "delta_q_y_dc %d\n", q->delta_q_y_dc);
	dprintk(ctx->dev, "delta_q_u_dc %d\n", q->delta_q_u_dc);
	dprintk(ctx->dev, "delta_q_u_ac %d\n", q->delta_q_u_ac);
	dprintk(ctx->dev, "delta_q_v_dc %d\n", q->delta_q_v_dc);
	dprintk(ctx->dev, "delta_q_v_ac %d\n", q->delta_q_v_ac);
	dprintk(ctx->dev, "qm_y %d\n", q->qm_y);
	dprintk(ctx->dev, "qm_u %d\n", q->qm_u);
	dprintk(ctx->dev, "qm_v %d\n", q->qm_v);
	dprintk(ctx->dev, "delta_q_res %d\n", q->delta_q_res);
	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_segmentation(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_segmentation *s = &run->av1.frame_header->segmentation;
	u32 i;
	u32 j;

	dprintk(ctx->dev, "AV1 Segmentation\n");
	dprintk(ctx->dev, "flags %d\n", s->flags);

	for (i = 0; i < ARRAY_SIZE(s->feature_enabled); i++)
		dprintk(ctx->dev,
			"feature_enabled[%d] %d\n",
			i, s->feature_enabled[i]);

	for (i = 0; i < V4L2_AV1_MAX_SEGMENTS; i++)
		for (j = 0; j < V4L2_AV1_SEG_LVL_MAX; j++)
			dprintk(ctx->dev,
				"feature_data[%d][%d] %d\n",
				i, j, s->feature_data[i][j]);

	dprintk(ctx->dev, "last_active_seg_id %d\n", s->last_active_seg_id);
	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_loop_filter(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_loop_filter *lf = &run->av1.frame_header->loop_filter;
	u32 i;

	dprintk(ctx->dev, "AV1 Loop Filter\n");
	dprintk(ctx->dev, "flags %d\n", lf->flags);

	for (i = 0; i < ARRAY_SIZE(lf->level); i++)
		dprintk(ctx->dev, "level[%d] %d\n", i, lf->level[i]);

	dprintk(ctx->dev, "sharpness %d\n", lf->sharpness);

	for (i = 0; i < ARRAY_SIZE(lf->ref_deltas); i++)
		dprintk(ctx->dev, "ref_deltas[%d] %d\n", i, lf->ref_deltas[i]);

	for (i = 0; i < ARRAY_SIZE(lf->mode_deltas); i++)
		dprintk(ctx->dev, "mode_deltas[%d], %d\n", i, lf->mode_deltas[i]);

	dprintk(ctx->dev, "delta_lf_res %d\n", lf->delta_lf_res);
	dprintk(ctx->dev, "delta_lf_multi %d\n", lf->delta_lf_multi);
	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_loop_restoration(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_loop_restoration *lr;
	u32 i;

	lr = &run->av1.frame_header->loop_restoration;
	dprintk(ctx->dev, "AV1 Loop Restoration\n");

	for (i = 0; i < ARRAY_SIZE(lr->frame_restoration_type); i++)
		dprintk(ctx->dev, "frame_restoration_type[%d] %d\n", i,
			lr->frame_restoration_type[i]);

	dprintk(ctx->dev, "lr_unit_shift %d\n", lr->lr_unit_shift);
	dprintk(ctx->dev, "lr_uv_shift %d\n", lr->lr_uv_shift);

	for (i = 0; i < ARRAY_SIZE(lr->loop_restoration_size); i++)
		dprintk(ctx->dev, "loop_restoration_size[%d] %d\n",
			i, lr->loop_restoration_size[i]);

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_cdef(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_cdef *cdef = &run->av1.frame_header->cdef;
	u32 i;

	dprintk(ctx->dev, "AV1 CDEF\n");
	dprintk(ctx->dev, "damping_minus_3 %d\n", cdef->damping_minus_3);
	dprintk(ctx->dev, "bits %d\n", cdef->bits);

	for (i = 0; i < ARRAY_SIZE(cdef->y_pri_strength); i++)
		dprintk(ctx->dev,
			"y_pri_strength[%d] %d\n", i, cdef->y_pri_strength[i]);
	for (i = 0; i < ARRAY_SIZE(cdef->y_sec_strength); i++)
		dprintk(ctx->dev,
			"y_sec_strength[%d] %d\n", i, cdef->y_sec_strength[i]);
	for (i = 0; i < ARRAY_SIZE(cdef->uv_pri_strength); i++)
		dprintk(ctx->dev,
			"uv_pri_strength[%d] %d\n", i, cdef->uv_pri_strength[i]);
	for (i = 0; i < ARRAY_SIZE(cdef->uv_sec_strength); i++)
		dprintk(ctx->dev,
			"uv_sec_strength[%d] %d\n", i, cdef->uv_sec_strength[i]);

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_global_motion(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_global_motion *gm;
	u32 i;
	u32 j;

	gm = &run->av1.frame_header->global_motion;

	dprintk(ctx->dev, "AV1 Global Motion\n");

	for (i = 0; i < ARRAY_SIZE(gm->flags); i++)
		dprintk(ctx->dev, "flags[%d] %d\n", i, gm->flags[i]);
	for (i = 0; i < ARRAY_SIZE(gm->type); i++)
		dprintk(ctx->dev, "type[%d] %d\n", i, gm->type[i]);

	for (i = 0; i < V4L2_AV1_TOTAL_REFS_PER_FRAME; i++)
		for (j = 0; j < 6; j++)
			dprintk(ctx->dev, "params[%d][%d] %d\n",
				i, j, gm->type[i]);

	dprintk(ctx->dev, "invalid %d\n", gm->invalid);

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_film_grain(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_film_grain *fg;
	u32 i;

	fg = &run->av1.frame_header->film_grain;

	dprintk(ctx->dev, "AV1 Film Grain\n");
	dprintk(ctx->dev, "flags %d\n", fg->flags);
	dprintk(ctx->dev, "grain_seed %d\n", fg->grain_seed);
	dprintk(ctx->dev, "film_grain_params_ref_idx %d\n",
		fg->film_grain_params_ref_idx);
	dprintk(ctx->dev, "num_y_points %d\n", fg->num_y_points);

	for (i = 0; i < ARRAY_SIZE(fg->point_y_value); i++)
		dprintk(ctx->dev, "point_y_value[%d] %d\n",
			i, fg->point_y_value[i]);

	for (i = 0; i < ARRAY_SIZE(fg->point_y_scaling); i++)
		dprintk(ctx->dev, "point_y_scaling[%d] %d\n",
			i, fg->point_y_scaling[i]);

	dprintk(ctx->dev, "\n");
}

static void
vivpu_dump_av1_tile_info(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_av1_tile_info *ti;
	u32 i;

	ti = &run->av1.frame_header->tile_info;

	dprintk(ctx->dev, "AV1 Tile Info\n");

	dprintk(ctx->dev, "flags %d\n", ti->flags);

	for (i = 0; i < ARRAY_SIZE(ti->mi_col_starts); i++)
		dprintk(ctx->dev, "mi_col_starts[%d] %d\n",
			i, ti->mi_col_starts[i]);

	for (i = 0; i < ARRAY_SIZE(ti->mi_row_starts); i++)
		dprintk(ctx->dev, "mi_row_starts[%d] %d\n",
			i, ti->mi_row_starts[i]);

	for (i = 0; i < ARRAY_SIZE(ti->width_in_sbs_minus_1); i++)
		dprintk(ctx->dev, "width_in_sbs_minus_1[%d] %d\n",
			i, ti->width_in_sbs_minus_1[i]);

	for (i = 0; i < ARRAY_SIZE(ti->height_in_sbs_minus_1); i++)
		dprintk(ctx->dev, "height_in_sbs_minus_1[%d] %d\n",
			i, ti->height_in_sbs_minus_1[i]);

	dprintk(ctx->dev, "tile_size_bytes %d\n", ti->tile_size_bytes);
	dprintk(ctx->dev, "context_update_tile_id %d\n", ti->context_update_tile_id);
	dprintk(ctx->dev, "tile_cols %d\n", ti->tile_cols);
	dprintk(ctx->dev, "tile_rows %d\n", ti->tile_rows);

	dprintk(ctx->dev, "\n");
}

static void vivpu_dump_av1_frame(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	const struct v4l2_ctrl_av1_frame_header *f = run->av1.frame_header;
	u32 i;

	dprintk(ctx->dev, "AV1 Frame Header\n");
	dprintk(ctx->dev, "flags %d\n", f->flags);
	dprintk(ctx->dev, "frame_type %d\n", f->frame_type);
	dprintk(ctx->dev, "order_hint %d\n", f->order_hint);
	dprintk(ctx->dev, "superres_denom %d\n", f->superres_denom);
	dprintk(ctx->dev, "upscaled_width %d\n", f->upscaled_width);
	dprintk(ctx->dev, "interpolation_filter %d\n", f->interpolation_filter);
	dprintk(ctx->dev, "tx_mode %d\n", f->tx_mode);
	dprintk(ctx->dev, "frame_width_minus_1 %d\n", f->frame_width_minus_1);
	dprintk(ctx->dev, "frame_height_minus_1 %d\n", f->frame_height_minus_1);
	dprintk(ctx->dev, "render_width_minus_1 %d\n", f->render_width_minus_1);
	dprintk(ctx->dev, "render_height_minus_1 %d\n", f->render_height_minus_1);
	dprintk(ctx->dev, "current_frame_id %d\n", f->current_frame_id);
	dprintk(ctx->dev, "primary_ref_frame %d\n", f->primary_ref_frame);

	for (i = 0; i < V4L2_AV1_MAX_OPERATING_POINTS; i++) {
		dprintk(ctx->dev, "buffer_removal_time[%d] %d\n",
			i, f->buffer_removal_time[i]);
	}

	dprintk(ctx->dev, "refresh_frame_flags %d\n", f->refresh_frame_flags);
	dprintk(ctx->dev, "last_frame_idx %d\n", f->last_frame_idx);
	dprintk(ctx->dev, "gold_frame_idx %d\n", f->gold_frame_idx);

	for (i = 0; i < ARRAY_SIZE(f->reference_frame_ts); i++)
		dprintk(ctx->dev, "reference_frame_ts[%d] %llu\n", i,
			f->reference_frame_ts[i]);

	vivpu_dump_av1_tile_info(ctx, run);
	vivpu_dump_av1_quantization(ctx, run);
	vivpu_dump_av1_segmentation(ctx, run);
	vivpu_dump_av1_loop_filter(ctx, run);
	vivpu_dump_av1_cdef(ctx, run);
	vivpu_dump_av1_loop_restoration(ctx, run);
	vivpu_dump_av1_global_motion(ctx, run);
	vivpu_dump_av1_film_grain(ctx, run);

	for (i = 0; i < ARRAY_SIZE(f->skip_mode_frame); i++)
		dprintk(ctx->dev, "skip_mode_frame[%d] %d\n",
			i, f->skip_mode_frame[i]);

	dprintk(ctx->dev, "\n");
}

static void vivpu_dump_av1_ctrls(struct vivpu_ctx *ctx, struct vivpu_run *run)
{
	vivpu_dump_av1_seq(ctx, run);
	vivpu_dump_av1_frame(ctx, run);
	vivpu_dump_av1_tile_group(ctx, run);
	vivpu_dump_av1_tile_group_entry(ctx, run);
	vivpu_dump_av1_tile_list(ctx, run);
	vivpu_dump_av1_tile_list_entry(ctx, run);
}

void vivpu_device_run(void *priv)
{
	struct vivpu_ctx *ctx = priv;
	struct vivpu_run run = {};
	struct media_request *src_req;

	run.src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	run.dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request(s) controls if needed. */
	src_req = run.src->vb2_buf.req_obj.req;

	if (src_req)
		v4l2_ctrl_request_setup(src_req, &ctx->hdl);

	switch (ctx->current_codec) {
	case VIVPU_CODEC_AV1:
		run.av1.sequence =
			vivpu_find_control_data(ctx, V4L2_CID_STATELESS_AV1_SEQUENCE);
		run.av1.frame_header =
			vivpu_find_control_data(ctx, V4L2_CID_STATELESS_AV1_FRAME_HEADER);
		run.av1.tile_group =
			vivpu_find_control_data(ctx, V4L2_CID_STATELESS_AV1_TILE_GROUP);
		run.av1.tg_entries =
			vivpu_find_control_data(ctx, V4L2_CID_STATELESS_AV1_TILE_GROUP_ENTRY);
		run.av1.tile_list =
			vivpu_find_control_data(ctx, V4L2_CID_STATELESS_AV1_TILE_LIST);
		run.av1.tl_entries =
			vivpu_find_control_data(ctx, V4L2_CID_STATELESS_AV1_TILE_LIST_ENTRY);

		vivpu_dump_av1_ctrls(ctx, &run);
		vivpu_av1_check_reference_frames(ctx, &run);
		break;
	default:
		break;
	}

	v4l2_m2m_buf_copy_metadata(run.src, run.dst, true);
	run.dst->sequence = ctx->q_data[V4L2_M2M_DST].sequence++;
	run.src->sequence = ctx->q_data[V4L2_M2M_SRC].sequence++;
	run.dst->field = ctx->dst_fmt.fmt.pix.field;

	dprintk(ctx->dev, "Got src buffer %p, sequence %d, timestamp %llu\n",
		run.src, run.src->sequence, run.src->vb2_buf.timestamp);

	dprintk(ctx->dev, "Got dst buffer %p, sequence %d, timestamp %llu\n",
		run.dst, run.dst->sequence, run.dst->vb2_buf.timestamp);

	/* Complete request(s) controls if needed. */
	if (src_req)
		v4l2_ctrl_request_complete(src_req, &ctx->hdl);

	if (vivpu_transtime)
		usleep_range(vivpu_transtime, vivpu_transtime * 2);

	v4l2_m2m_buf_done_and_job_finish(ctx->dev->m2m_dev,
					 ctx->fh.m2m_ctx, VB2_BUF_STATE_DONE);
}
