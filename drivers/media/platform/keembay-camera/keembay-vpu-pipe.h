/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera VPU pipe definitions
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_VPU_PIPE_H
#define KEEMBAY_VPU_PIPE_H

#include "keembay-vpu-src.h"

#define PIPE_TYPE_ISP_MAX_EXP 3

enum {
	PIPE_TYPE_ISP_ISP_ULL = 1,
	PIPE_TYPE_ISP_ISP_2DOL,
	PIPE_TYPE_ISP_ISP_3DOL,
	PIPE_TYPE_ISP_ISP_MONO,

	PIPE_TYPE_MAX,
};

enum {
	SRC_TYPE_ALLOC_VPU_DATA_MIPI = 0,
	SRC_TYPE_ALLOC_VPU_DATA_DBG,
	SRC_TYPE_ALLOC_ARM_DATA_ARM,
	SRC_TYPE_ALLOC_ARM_DATA_MIPI,
	SRC_TYPE_ALLOC_ARM_DATA_DBG,

	SRC_TYPE_ALLOC_DATA_MAX,
};

enum {
	PIPE_TRANSFORM_HUB_NONE = 0,
	PIPE_TRANSFORM_HUB_BASIC,
	PIPE_TRANSFORM_HUB_FULL,
	PIPE_TRANSFORM_HUB_STITCH,
	PIPE_TRANSFORM_HUB_EPTZ,

	PIPE_TRANSFORM_HUB_MAX,
};

enum {
	PIPE_OUTPUT_ID_RAW = 0,
	PIPE_OUTPUT_ID_ISP_CTRL,
	PIPE_OUTPUT_ID_0,
	PIPE_OUTPUT_ID_1,
	PIPE_OUTPUT_ID_2,
	PIPE_OUTPUT_ID_3,
	PIPE_OUTPUT_ID_4,
	PIPE_OUTPUT_ID_5,
	PIPE_OUTPUT_ID_6,

	PIPE_OUTPUT_ID_MAX,
};

/*
 * struct kmb_channel_cfg - KMB channel configuration
 *
 * @id: Channel id
 * @frm_res: Frame resolution
 */
struct kmb_channel_cfg {
	u32 id;
	struct kmb_ic_img_size frm_res;
};

/*
 * struct kmb_pipe_config_evs - VPU pipeline configuration
 *
 * @pipe_id: Pipe id
 * @pipe_type: Pipe type
 * @src_type: Source type
 * @pipe_trans_hub: Transform hub type
 * @in_isp_res: Input ISP resolution
 * @out_isp_res: Output isp resolution
 * @in_isp_stride: ISP input stride used in DOL interleaved mode
 * @in_exp_offsets: Long and short exp frames offsets used in interleaved mode
 * @out_min_res: Output min resolution
 * @out_max_res: Output max resolution
 * @pipe_xlink_chann: Output channel id from the enum PIPE_OUTPUT_ID
 * @keep_aspect_ratio: If enabled, aspect ratio must be kept when image is
 *                     resized
 * @in_data_width: Input bits per pixel
 * @in_data_packed: Flag to enable packed mode
 * @out_data_width: Output bits per pixel for first plane
 * @internal_memory_addr: Internal memory pool address
 * @internal_memory_size: Internal memory pool size
 */
struct kmb_pipe_config_evs {
	u8 pipe_id;
	u8 pipe_type;
	u8 src_type;
	u8 pipe_trans_hub;
	struct kmb_ic_img_size in_isp_res;
	struct kmb_ic_img_size out_isp_res;
	u16 in_isp_stride;
	u32 in_exp_offsets[PIPE_TYPE_ISP_MAX_EXP];
	struct kmb_ic_img_size out_min_res[PIPE_OUTPUT_ID_MAX];
	struct kmb_ic_img_size out_max_res[PIPE_OUTPUT_ID_MAX];
	struct kmb_channel_cfg pipe_xlink_chann[PIPE_OUTPUT_ID_MAX];
	u8 keep_aspect_ratio;
	u8 in_data_width;
	u8 in_data_packed;
	u8 out_data_width;
	u64 internal_memory_addr;
	u32 internal_memory_size;
} __aligned(64);

#endif /* KEEMBAY_VPU_PIPE_H */
