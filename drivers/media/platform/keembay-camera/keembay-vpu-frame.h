/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera VPU frame data
 *
 * Copyright (C) 2021 Intel Corporation
 */

#ifndef KEEMBAY_VPU_FRAME_H_
#define KEEMBAY_VPU_FRAME_H_

/**
 * enum kmb_frame_types - Frame types
 *
 * @KMB_FRAME_TYPE_YUV422I: Interleaved 8 bit
 * @KMB_FRAME_TYPE_YUV444P: Planar 4:4:4 format
 * @KMB_FRAME_TYPE_YUV420P: Planar 4:2:0 format
 * @KMB_FRAME_TYPE_YUV422P: Planar 8-bit greyscale
 * @KMB_FRAME_TYPE_YUV400P: 8-bit greyscale
 * @KMB_FRAME_TYPE_RGBA8888: RGBA interleaved stored in 32 bit word
 * @KMB_FRAME_TYPE_RGB888: Planar 8 bit RGB data
 * @KMB_FRAME_TYPE_LUT2: 1 bit per pixel, Lookup table(used for graphics layers)
 * @KMB_FRAME_TYPE_LUT4: 2 bit per pixel, Lookup table(used for graphics layers)
 * @KMB_FRAME_TYPE_LUT16: 4 bit per pixel, Lookup table (used for
 *                        graphics layers)
 * @KMB_FRAME_TYPE_RAW16: Save any raw type (8, 10, 12bit) on 16 bits
 * @KMB_FRAME_TYPE_RAW14: 14-bit value in 16-bit storage
 * @KMB_FRAME_TYPE_RAW12: 12-bit value in 16-bit storage
 * @KMB_FRAME_TYPE_RAW10: 10-bit value in 16-bit storage
 * @KMB_FRAME_TYPE_RAW8: Raw 8 greyscale
 * @KMB_FRAME_TYPE_PACK10: SIPP 10-bit packed format
 * @KMB_FRAME_TYPE_PACK12: SIPP 12-bit packed format
 * @KMB_FRAME_TYPE_YUV444I: Planar 4:4:4 interleaved format
 * @KMB_FRAME_TYPE_NV12: Format NV12
 * @KMB_FRAME_TYPE_NV21: Format NV21
 * @KMB_FRAME_TYPE_BITSTREAM: Used for video encoder bitstream
 * @KMB_FRAME_TYPE_HDR: Format HDR
 * @KMB_FRAME_TYPE_NV12PACK10: NV12 format with pixels encoded in pack 10
 * @KMB_FRAME_TYPE_NONE: Format None
 */
enum kmb_frame_types {
	KMB_FRAME_TYPE_YUV422I,
	KMB_FRAME_TYPE_YUV444P,
	KMB_FRAME_TYPE_YUV420P,
	KMB_FRAME_TYPE_YUV422P,
	KMB_FRAME_TYPE_YUV400P,
	KMB_FRAME_TYPE_RGBA8888,
	KMB_FRAME_TYPE_RGB888,
	KMB_FRAME_TYPE_LUT2,
	KMB_FRAME_TYPE_LUT4,
	KMB_FRAME_TYPE_LUT16,
	KMB_FRAME_TYPE_RAW16,
	KMB_FRAME_TYPE_RAW14,
	KMB_FRAME_TYPE_RAW12,
	KMB_FRAME_TYPE_RAW10,
	KMB_FRAME_TYPE_RAW8,
	KMB_FRAME_TYPE_PACK10,
	KMB_FRAME_TYPE_PACK12,
	KMB_FRAME_TYPE_YUV444I,
	KMB_FRAME_TYPE_NV12,
	KMB_FRAME_TYPE_NV21,
	KMB_FRAME_TYPE_BITSTREAM,
	KMB_FRAME_TYPE_HDR,
	KMB_FRAME_TYPE_NV12PACK10,
	KMB_FRAME_TYPE_NONE,
};

/**
 * struct kmb_frame_spec - KMB frame specifications
 *
 * @type: Values from the enum kmb_frame_type
 * @height: Height in pixels
 * @width: Width in pixels
 * @stride: Defines as distance in bytes from pix(y, x) to pix(y+1, x)
 * @bpp: Bits per pixel (for unpacked types set to 8 or 16, for NV12 set only
 *       luma pixel size)
 */
struct kmb_frame_spec {
	u16 type;
	u16 height;
	u16 width;
	u16 stride;
	u16 bpp;
};

/**
 * struct kmb_vpu_frame_buffer - KMB frame buffer elements
 *
 * @spec: Frame specifications parameters
 * @p1: Address to first image plane
 * @p2: Address to second image plane (if used)
 * @p3: Address to third image plane (if used)
 * @ts: Timestamp in NS
 */
struct kmb_vpu_frame_buffer {
	struct kmb_frame_spec spec;
	u64 p1;
	u64 p2;
	u64 p3;
	s64 ts;
};

#endif /* KEEMBAY_VPU_FRAME_H_ */
