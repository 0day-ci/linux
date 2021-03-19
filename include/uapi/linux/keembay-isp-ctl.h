/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Intel Keem Bay camera control parameters and statistics
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_ISP_CTL_H
#define KEEMBAY_ISP_CTL_H

#include <linux/types.h>
#include <linux/videodev2.h>

#define KMB_CAM_MAX_EXPOSURES		3

/* Table max sizes */
#define KMB_CAM_LCA_MESH_SIZE		(32 * 24)
#define KMB_CAM_SHARPEN_RADIAL_SIZE	256
#define KMB_CAM_LUT3D_SIZE		(16 * 16 * 16 * 4)
#define KMB_CAM_LSC_SIZE		((64 + 4) * 64 * 4)
#define KMB_CAM_GAMMA_SIZE		(512 * 4)
#define KMB_CAM_STATIC_DEFECT_SIZE	1000
#define KMB_CAM_CHROMA_LUT_SIZE		1024
#define KMB_CAM_WARP_MESH_SIZE		(480 * 270)
#define KMB_CAM_HDR_TM_LUTS_SIZE	(((67 * 16) + 63) & ~63)

/* Statistics max sizes */
#define KMB_CAM_AE_AWB_STATS_SIZE	(64 * 64)
#define KMB_CAM_AF_STATS_SIZE		(64 * 64)
#define KMB_CAM_HIST_LUMA_SIZE		256
#define KMB_CAM_HIST_RGB_SIZE		(3 * 128)
#define KMB_CAM_FLICKER_ROWS_SIZE	(65535 + 1) /* align to 64 */
#define MAX_DHZ_AIRLIGHT_STATS_SIZE	(4 * 128)

/**
 * enum kmb_vpu_isp_bayer_order - KMB sensor Bayer arrangement format types
 *
 * @KMB_ISP_BAYER_ORDER_GRBG: Gr R B Gr
 * @KMB_ISP_BAYER_ORDER_RGGB: R Gr Gr B
 * @KMB_ISP_BAYER_ORDER_GBRG: Gr B R Gr
 * @KMB_ISP_BAYER_ORDER_BGGR: B Gr Gr R
 */
enum kmb_isp_bayer_order {
	KMB_ISP_BAYER_ORDER_GRBG = 0,
	KMB_ISP_BAYER_ORDER_RGGB = 1,
	KMB_ISP_BAYER_ORDER_GBRG = 2,
	KMB_ISP_BAYER_ORDER_BGGR = 3,
} __packed;

/**
 * struct kmb_blc_params - KMB Black Level Correction parameters
 *
 * @coeff1: Black level correction coefficient parameter. Range [0 - 4096]
 * @coeff2: Black level correction coefficient parameter. Range [0 - 4096]
 * @coeff3: Black level correction coefficient parameter. Range [0 - 4096]
 * @coeff4: Black level correction coefficient parameter. Range [0 - 4096]
 */
struct kmb_blc_params {
	__u32 coeff1;
	__u32 coeff2;
	__u32 coeff3;
	__u32 coeff4;
} __packed;

/**
 * struct kmb_sigma_dns_params - KMB Sigma Denoise parameters
 *
 * @noise: Sigma denoise noise parameter. Range [0 - 65535]
 * @threshold1: Sigma denoise min threshold1 parameter. Range [0 - 255]
 * @threshold2: Sigma denoise max threshold2 parameter. Range [0 - 255]
 * @threshold3: Sigma denoise min threshold3 parameter. Range [0 - 255]
 * @threshold4: Sigma denoise max threshold4 parameter. Range [0 - 255]
 * @threshold5: Sigma denoise min threshold5 parameter. Range [0 - 255]
 * @threshold6: Sigma denoise max threshold6 parameter. Range [0 - 255]
 * @threshold7: Sigma denoise min threshold7 parameter. Range [0 - 255]
 * @threshold8: Sigma denoise max threshold8 parameter. Range [0 - 255]
 */
struct kmb_sigma_dns_params {
	__u32 noise;
	__u32 threshold1;
	__u32 threshold2;
	__u32 threshold3;
	__u32 threshold4;
	__u32 threshold5;
	__u32 threshold6;
	__u32 threshold7;
	__u32 threshold8;
} __packed;

/**
 * struct kmb_lsc_params - KMB Lens Shading Correction parameters
 *
 * @threshold: Lens shading correction threshold parameter
 * @width: Lens shading correction gain1 parameter. Range [1 - 64].
 *         Must be a multiple of 4
 * @height: Lens shading correction gain2 parameter. Range [1 - 64].
 *          Must be an even number
 * @gain_mesh: Lens shading correction gain mesh table
 */
struct kmb_lsc_params {
	__u32 threshold;
	__u32 width;
	__u32 height;
	__u8 gain_mesh[KMB_CAM_LSC_SIZE];
} __packed;

/**
 * struct kmb_raw_params - KMB Raw parameters
 *
 * @awb_stats_en: Enable AE/AWB stats output
 * @awb_rgb_hist_en: Enable RGB histogram output
 * @af_stats_en: Enable AF stats output
 * @luma_hist_en: Enable Luma histogram output
 * @flicker_accum_en: Enable flicker detection row accumulation output
 * @bad_pixel_fix_en: Enable Hot/Cold pixel suppression
 * @grgb_imb_en: Enable Gr/Gb imbalance correction
 * @mono_imbalance_en: Enable mono imbalance correction
 * @gain1: Raw gain1 parameter
 * @gain2: Raw gain2 parameter
 * @gain3: Raw gain3 parameter
 * @gain4: Raw gain4 parameter
 * @stop1: Raw stop1 parameter
 * @stop2: Raw stop2 parameter
 * @stop3: Raw stop3 parameter
 * @stop4: Raw stop4 parameter
 * @threshold1: Raw threshold1 parameter
 * @alpha1: Raw alpha1 parameter. Range [0 - 15]
 * @alpha2: Raw alpha2 parameter. Range [0 - 15]
 * @alpha3: Raw alpha3 parameter. Range [0 - 15]
 * @alpha4: Raw alpha4 parameter. Range [0 - 15]
 * @threshold2: Raw threshold2 parameter. Range [0 - 2047]
 * @static_defect_size: static_defect_map size parameter. Range [0 - 65536]
 * @static_defect_map: Static defect map
 * @start_row: Raw start_row parameter
 * @end_row: Raw end_row parameter
 */
struct kmb_raw_params {
	__u32 awb_stats_en;
	__u32 awb_rgb_hist_en;
	__u32 af_stats_en;
	__u32 luma_hist_en;
	__u32 flicker_accum_en;
	__u32 bad_pixel_fix_en;
	__u32 grgb_imb_en;
	__u32 mono_imbalance_en;
	__u32 gain1;
	__u32 gain2;
	__u32 gain3;
	__u32 gain4;
	__u32 stop1;
	__u32 stop2;
	__u32 stop3;
	__u32 stop4;
	__u32 threshold1;
	__u32 alpha1;
	__u32 alpha2;
	__u32 alpha3;
	__u32 alpha4;
	__u32 threshold2;
	__u32 static_defect_size;
	__u8 static_defect_map[KMB_CAM_STATIC_DEFECT_SIZE];
	__u32 start_row;
	__u32 end_row;
} __packed;

/**
 * struct kmb_ae_awb_params - KMB AE/AWB statistics parameters
 *
 * @start_x: AE/AWB start_x parameter
 * @start_y: AE/AWB start_y parameter
 * @width: AE/AWB width parameter
 * @height: AE/AWB height parameter
 * @skip_x: AE/AWB skip_x parameter
 * @skip_y: AE/AWB skip_y parameter
 * @patches_x: AE/AWB patches_x parameter
 * @patches_y: AE/AWB patches_y parameter
 * @threshold1: AE/AWB threshold1 parameter
 * @threshold2: AE/AWB threshold2 parameter
 */
struct kmb_ae_awb_params {
	__u32 start_x;
	__u32 start_y;
	__u32 width;
	__u32 height;
	__u32 skip_x;
	__u32 skip_y;
	__u32 patches_x;
	__u32 patches_y;
	__u16 threshold1;
	__u16 threshold2;
} __packed;

/**
 * struct kmb_af_params - KMB Auto Focus parameters
 *
 * @start_x: AF start_x parameter
 * @start_y: AF start_y parameter
 * @width: AF width parameter
 * @height: AF height parameter
 * @patches_x: AF patches_x parameter
 * @patches_y: AF patches_y parameter
 * @coeff: AF filter coeff parameter
 * @threshold1: AF filer threshold1 parameter
 * @threshold2: AF filer threshold2 parameter
 * @coeffs1: AF filter coeffs1 parameter
 * @coeffs2: AF filter coeffs2 parameter
 */
struct kmb_af_params {
	__u32 start_x;
	__u32 start_y;
	__u32 width;
	__u32 height;
	__u32 patches_x;
	__u32 patches_y;
	__s32 coeff;
	__s32 threshold1;
	__s32 threshold2;
	__s32 coeffs1[11];
	__s32 coeffs2[11];
} __packed;

/**
 * struct kmb_hist_params - KMB Hist parameters
 *
 * @start_x: Hist start_x parameter. Range [0 - 1]
 * @start_y: Hist start_y parameter. Range [0 - 1]
 * @end_x: Hist end_x parameter. Range [0 - 1]
 * @end_y: Hist end_y parameter. Range [0 - 1]
 * @matrix: Hist matrix parameter. Range [0.0 - 8.0]
 * @weight: Hist weight parameter. Range [0.0 - 1.0]
 */
struct kmb_hist_params {
	__u32 start_x;
	__u32 start_y;
	__u32 end_x;
	__u32 end_y;
	__u16 matrix[9];
	__u16 weight[3];
} __packed;

/**
 * struct kmb_lca_params - KMB Lateral Chromatic Aberration parameters
 *
 * @coeff: LCA coeff parameter
 */
struct kmb_lca_params {
	__u8 coeff[KMB_CAM_LCA_MESH_SIZE];
} __packed;

/**
 * struct kmb_debayer_params - KMB Debayer parameters
 *
 * @coeff1: Filter coeff1 parameter
 * @multiplier1: Filter multiplier1 parameter
 * @multiplier2: Filter multiplier2 parameter
 * @coeff2: Filter coeff2 parameter
 * @coeff3: Filter coeff3 parameter
 * @coeff4: Filter coeff4 parameter
 */
struct kmb_debayer_params {
	__s32 coeff1;
	__u32 multiplier1;
	__u32 multiplier2;
	__s32 coeff2;
	__s32 coeff3;
	__s32 coeff4;
} __packed;

/**
 * struct kmb_hdr_params - KMB HDR parameters
 *
 * @ratio: HDR ratio parameter. Range [0 - 65536]
 * @scale: HDR scale parameter
 * @offset1: HDR offset1 parameter. Range [-4095 - 0]
 * @slope1: HDR slope1 parameter. Range [0 - 4095]
 * @offset2: HDR offset2 parameter. Range [-4095 - 0]
 * @slope2: HDR slope2 parameter. Range [0 - 4095]
 * @offset3: HDR offset3 parameter. Range [0 - 4095]
 * @slope3: HDR slope3 parameter. Range [0 - 4095]
 * @offset4: HDR offset4 parameter. Range [0 - 4095]
 * @gain1: HDR gain1 parameter. Range [0 - 4095]
 * @blur1: HDR blur1 parameter. Range [0.0 - 255.0]
 * @blur2: HDR blur2 parameter. Range [0 - 255]
 * @contrast1: HDR contrast1 parameter
 * @contrast2: HDR contrast2 parameter
 * @enable1: HDR enable1 parameter
 * @enable2: HDR enable2 parameter
 * @offset5: HDR offset5 parameter
 * @gain2: HDR gain2 parameter
 * @offset6: HDR offset6 parameter. Range [0 - 1024]
 * @strength: HDR strength parameter. Range [0 - 65536]
 * @tm_lut: HDR tm lut parameter
 * @offset7: HDR offset7 parameter. Range [0 - 65536]
 * @shift: HDR shift parameter
 * @field1: HDR filed1 parameter
 * @field2: HDR field2 parameter
 * @gain3: HDR gain3 parameter. Range [0 - 255]
 * @min: HDR min parameter. Range [0 - 4095]
 */
struct kmb_hdr_params {
	__u32 ratio[2];
	__u32 scale[3];
	__s32 offset1;
	__u32 slope1;
	__s32 offset2;
	__u32 slope2;
	__s32 offset3;
	__u32 slope3;
	__s32 offset4;
	__u32 gain1;
	__u32 blur1[3];
	__u32 blur2[5];
	__u32 contrast1;
	__u32 contrast2;
	__u32 enable1;
	__u32 enable2;
	__s32 offset5;
	__u32 gain2;
	__s32 offset6;
	__u32 strength;
	__u8 tm_lut[KMB_CAM_HDR_TM_LUTS_SIZE];
	__u16 offset7;
	__u32 shift;
	__u16 field1;
	__u16 field2;
	__u8 gain3;
	__u16 min;
} __packed;

/**
 * struct kmb_dog_dns_params - KMB Difference-of-Gaussians DNS parameters
 *
 * @threshold: Filter threshold parameter. Range [0 - 255]
 * @strength: Filter strength parameter. Range [0 - 255]
 * @coeffs11: Filter coeffs11 parameter. Range [0 - 1023]
 * @coeffs15: Filter coeffs15 parameter. Range [0 - 1023]
 */
struct kmb_dog_dns_params {
	__u32 threshold;
	__u32 strength;
	__u8 coeffs11[6];
	__u8 coeffs15[8];
} __packed;

/**
 * struct kmb_luma_dns_params - KMB Luma DNS parameters
 *
 * @threshold: Luma DNS threshold parameter. Range [0 - 32768]
 * @slope: Luma DNS slope parameter. Range [0 - 2048]
 * @shift: Luma DNS shift parameter. Range [0 - 255]
 * @alpha: Luma DNS alpha parameter. Range [0 - 127]
 * @weight: Luma DNS weight parameter. Range [0 - 4294967295]
 * @per_pixel_alpha_en: Enable adapt alpha
 * @gain_bypass_en: Enable gain bypass
 */
struct kmb_luma_dns_params {
	__u32 threshold;
	__u32 slope;
	__u32 shift;
	__u32 alpha;
	__u32 weight;
	__u32 per_pixel_alpha_en;
	__u32 gain_bypass_en;
} __packed;

/**
 * struct kmb_sharpen_params - KMB Sharpen parameters
 *
 * @coeffs1: Filter coeffs1 parameter. Range [0 - 255]
 * @coeffs2: Filter coeffs2 parameter. Range [0 - 255]
 * @coeffs3: Filter coeffs3 parameter. Range [0 - 255]
 * @shift: Filter shift parameter. Range [0 - 255]
 * @gain1: Filter gain1 parameter. Range [0 - 2047]
 * @gain2: Filter gain2 parameter. Range [0 - 2047]
 * @gain3: Filter gain3 parameter. Range [0 - 2047]
 * @gain4: Filter gain4 parameter. Range [0 - 2047]
 * @gain5: Filter gain5 parameter. Range [0 - 255]
 * @stops1: Filter stops1 parameter. Range [0 - 4095]
 * @gains: Filter gains parameter. Range [0 - 256]
 * @stops2: Filter stops2 parameter. Range [0 - 4095]
 * @overshoot: Filter overshoot parameter. Range [0 - 256]
 * @undershoot: Filter undershoot parameter. Range [0 - 256]
 * @alpha: Filter alpha parameter. Range [0 - 256]
 * @gain6: Filter gain6 parameter. Range [0 - 255]
 * @offset: Filter offset parameter. Range [0 - 1023]
 * @radial_lut: Sharpen radial LUT parameter. Range [0 - 255]
 */
struct kmb_sharpen_params {
	__u16 coeffs1[6];
	__u16 coeffs2[6];
	__u16 coeffs3[6];
	__u32 shift;
	__u32 gain1;
	__u32 gain2;
	__u32 gain3;
	__u32 gain4;
	__u32 gain5;
	__u32 stops1[3];
	__u32 gains[3];
	__u32 stops2[4];
	__u32 overshoot;
	__u32 undershoot;
	__u32 alpha;
	__u32 gain6;
	__u32 offset;
	__u8 radial_lut[KMB_CAM_SHARPEN_RADIAL_SIZE];
} __packed;

/**
 * struct kmb_chroma_gen_params - KMB Chroma GEN parameters
 *
 * @epsilon: Chroma GEN epsilon parameter. Range [0 - 255]
 * @coeff1: Chroma GEN coeff1 parameter. Range [0 - 1024]
 * @coeff2: Chroma GEN coeff2 parameter. Range [0 - 1024]
 * @coeff3: Chroma GEN coeff3 parameter. Range [0 - 1024]
 * @coeff4: Chroma GEN coeff4 parameter. Range [0 - 255]
 * @coeff5: Chroma GEN coeff5 parameter. Range [0 - 255]
 * @coeff6: Chroma GEN coeff6 parameter. Range [0 - 255]
 * @strength1: Chroma GEN strength1 parameter. Range [0 - 255]
 * @strength2: Chroma GEN strength2 parameter. Range [0 - 255]
 * @coeffs: Chroma GEN coeffs parameter . Range [0 - 255]
 * @offset1: Chroma GEN offset1 parameter. Range [0 - 255]
 * @slope1: Chroma GEN slope1 parameter. Range [0 - 255]
 * @slope2: Chroma GEN slope2 parameter. Range [0 - 255]
 * @offset2: Chroma GEN offset2 parameter. Range [0 - 255]
 * @limit: Chroma GEN limit parameter. Range [0 - 767]
 */
struct kmb_chroma_gen_params {
	__u32 epsilon;
	__u32 coeff1;
	__u32 coeff2;
	__u32 coeff3;
	__u32 coeff4;
	__u32 coeff5;
	__u32 coeff6;
	__u32 strength1;
	__u32 strength2;
	__u32 coeffs[3];
	__s32 offset1;
	__u32 slope1;
	__u32 slope2;
	__s32 offset2;
	__u32 limit;
} __packed;

/**
 * struct kmb_median_params - KMB Median parameters
 *
 * @size: Filter size parameter. Range [1;3;5;7]
 * @slope: Filter slope parameter. Range [0 - 128]
 * @offset: Filter offset parameter. Range [-32 - 32]
 */
struct kmb_median_params {
	__u32 size;
	__u32 slope;
	__s32 offset;
} __packed;

/**
 * struct kmb_chroma_dns_params - KMB Chroma Denoise parameters
 *
 * @limit: Filter limit parameter
 * @enable: Filter enable parameter
 * @threshold1: Filter threshold1 parameter
 * @threshold2: Filter threshold2 parameter
 * @threshold3: Filter threshold3 parameter. Range [0 - 255]
 * @threshold4: Filter threshold4 parameter. Range [0 - 255]
 * @threshold5: Filter threshold5 parameter. Range [0 - 255]
 * @threshold6: Filter threshold6 parameter. Range [0 - 255]
 * @threshold7: Filter threshold7 parameter. Range [0 - 255]
 * @threshold8: Filter threshold8 parameter. Range [0 - 255]
 * @slope1: Filter slope1 parameter. Range [0 - 255]
 * @offset1: Filter offset1 parameter. Range [0 - 255]
 * @slope2: Filter slope2 parameter. Range [0 - 255]
 * @offset2: Filter offset2 parameter. Range [0 - 255]
 * @grey1: Filter grey1 parameter. Range [0 - 255]
 * @grey2: Filter grey2 parameter. Range [0 - 255]
 * @grey3: Filter grey3 parameter. Range [0 - 255]
 * @coeff1: Filter coeff1 parameter. Range [0 - 255]
 * @coeff2: Filter coeff2 parameter. Range [0 - 255]
 * @coeff3: Filter coeff3 parameter. Range [0 - 255]
 */
struct kmb_chroma_dns_params {
	__u32 limit;
	__u32 enable;
	__u32 threshold1;
	__u32 threshold2;
	__u32 threshold3;
	__u32 threshold4;
	__u32 threshold5;
	__u32 threshold6;
	__u32 threshold7;
	__u32 threshold8;
	__u32 slope1;
	__s32 offset1;
	__u32 slope2;
	__s32 offset2;
	__u32 grey1;
	__u32 grey2;
	__u32 grey3;
	__u32 coeff1;
	__u32 coeff2;
	__u32 coeff3;
} __packed;

/**
 * struct kmb_color_comb_params - KMB Color Combine parameters
 *
 * @matrix: Color combine matrix parameter. Range [-8.0 - 8.0]
 * @offsets:Color combine offsets parameter. Range [-1.0 - 1.0]
 * @coeff1: Color combine coeff1 parameter. Range [0 - 1023]
 * @coeff2: Color combine coeff2 parameter. Range [0 - 1023]
 * @coeff3: Color combine coeff3 parameter. Range [0 - 1023]
 * @lut_3d: Color combine LUT 3D parameter. Range [0 - 4095]
 * @enable: Color combine enable parameter
 * @weight1: Color combine weight1 parameter. Range [0 - 255]
 * @weight2: Color combine weight2 parameter. Range [0 - 255]
 * @weight3: Color combine weight3 parameter. Range [0 - 255]
 * @limit1: Color combine limit1 parameter. Range [0 - 32766]
 * @limit2: Color combine limit2 parameter. Range [-32766 - 0]
 * @offset1: Color combine offset1 parameter. Range [0 - 8192]
 * @offset2: Color combine offset2 parameter. Range [0 - 8192]
 */
struct kmb_color_comb_params {
	__u16 matrix[9];
	__u16 offsets[3];
	__u32 coeff1;
	__u32 coeff2;
	__u32 coeff3;
	__u8 lut_3d[KMB_CAM_LUT3D_SIZE];
	__u32 enable;
	__u32 weight1;
	__u32 weight2;
	__u32 weight3;
	__u32 limit1;
	__s32 limit2;
	__s32 offset1;
	__s32 offset2;
} __packed;

/**
 * struct kmb_lut_params - KMB lut parameters
 *
 * @size: Lut size parameter. Range [0 - 128]
 * @table: Lut table parameter
 * @matriix: Lut matrix parameter
 * @offsets: Lut offsets pparameter
 */
struct kmb_lut_params {
	__u32 size;
	__u8 table[KMB_CAM_GAMMA_SIZE];
	__u16 matrix[3 * 3];
	__u16 offsets[3];
} __packed;

/**
 * struct kmb_tnf_params - KMB Temporal Noise Filter parameters
 *
 * @factor: Filter factor parameter. Range [0 - 255]
 * @gain: Filter gain parameter. Range [0 - 4095]
 * @offset1: Filter offset1 parameter. Range [0 - 8192]
 * @slope1: Filter slope1 parameter. Range [0 - 512]
 * @offset2: Filter offset2 parameter. Range [0 - 8192]
 * @slope2: Filter slope2 parameter. Range [0 - 512]
 * @min1: Filter min1 parameter. Range [0 - 65535]
 * @min2: Filter min2 parameter. Range [0 - 65535]
 * @value: Filter value parameter. Range [0 - 255]
 * @enable: Filter enable parameter
 * @chroma_lut0: First chroma LUT. Range [0 - 4095]
 * @chroma_lut1: Second chroma LUT. Range [0 - 4095]
 */
struct kmb_tnf_params {
	__u32 factor;
	__u32 gain;
	__u32 offset1;
	__u32 slope1;
	__u32 offset2;
	__u32 slope2;
	__u32 min1;
	__u32 min2;
	__u32 value;
	__u32 enable;
	__u8 chroma_lut0[KMB_CAM_CHROMA_LUT_SIZE];
	__u8 chroma_lut1[KMB_CAM_CHROMA_LUT_SIZE];
} __packed;

/**
 * struct kmb_dehaze_params - KMB dehaze parameters
 *
 * @gain1: Dehaze gain1 parameter. Range [0 - 1023]
 * @min: Dehaze min parameter. Range [0 - 255]
 * @strength1: Dehaze strength1 parameter. Range [0 - 255]
 * @strength2: Dehaze strength2 parameter. Range [0 - 65535]
 * @gain2: Dehaze gain2 parameter. Range [0 - 255]
 * @saturation: Dehaze saturation parameter. Range [0 - 127]
 * @value1: Dehaze value1 parameter. Range [0 - 4095]
 * @value2: Dehaze value2 parameter. Range [0 - 4095]
 * @value3: Dehaze value3 parameter. Range [0 - 4095]
 * @filter: Dehaze filter parameter. Range [0 - 255]
 */
struct kmb_dehaze_params {
	__u32 gain1;
	__u32 min;
	__u32 strength1;
	__u32 strength2;
	__u32 gain2;
	__u32 saturation;
	__u32 value1;
	__u32 value2;
	__u32 value3;
	__u32 filter[3];
} __packed;

/**
 * struct kmb_warp_params - KMB Warp filter parameters
 *
 * @type: Warp filter type parameter. Range [0 - 1]
 * @relative: Warp filter relative parameter. Range [0 - 1]
 * @format: Warp filter format parameter. Range [0 - 1]
 * @position: Warp filter position parameter
 * @mesh_grid: Warp mesh grid
 * @width: Warp filter width parameter
 * @height: Warp filter height parameter
 * @stride: Warp filter stride parameter
 * @enable: Warp filter enable parameter
 * @matrix: Warp matrix parameter
 * @mode: Warp filter mode parameter. Range [0 - 1]
 * @values: Warp filter values parameter. Range [0 - 128]
 */
struct kmb_warp_params {
	__u8 type;
	__u8 relative;
	__u8 format;
	__u8 position;
	__u8 mesh_grid[KMB_CAM_WARP_MESH_SIZE];
	__u16 width;
	__u16 height;
	__u32 stride;
	__u8 enable;
	__u32 matrix[9];
	__u8 mode;
	__u16 values[3];
} __packed;

/**
 * struct kmb_isp_params_flags - Bits to indicate which params
 *                               need to be updated
 *
 * @blc: 1 = update, 0 = do not update.
 * @sigma_dns: 1 = update, 0 = do not update.
 * @lsc: 1 = update, 0 = do not update.
 * @raw: 1 = update, 0 = do not update.
 * @ae_awb: 1 = update, 0 = do not update.
 * @af: 1 = update, 0 = do not update.
 * @histogram: 1 = update, 0 = do not update.
 * @lca: 1 = update, 0 = do not update.
 * @debayer: 1 = update, 0 = do not update.
 * @dog_dns: 1 = update, 0 = do not update.
 * @luma_dns: 1 = update, 0 = do not update.
 * @sharpen: 1 = update, 0 = do not update.
 * @chroma_gen: 1 = update, 0 = do not update.
 * @median: 1 = update, 0 = do not update.
 * @chroma_dns: 1 = update, 0 = do not update.
 * @color_comb: 1 = update, 0 = do not update.
 * @hdr: 1 = update, 0 = do not update.
 * @lut: 1 = update, 0 = do not update.
 * @tnf: 1 = update, 0 = do not update.
 * @dehaze: 1 = update, 0 = do not update.
 * @warp: 1 = update, 0 = do not update.
 * @reserved: reserved for future use and for alignment
 */
struct kmb_isp_params_flags {
	__u32 blc:1;
	__u32 sigma_dns:1;
	__u32 lsc:1;
	__u32 raw:1;
	__u32 ae_awb:1;
	__u32 af:1;
	__u32 histogram:1;
	__u32 lca:1;
	__u32 debayer:1;
	__u32 dog_dns:1;
	__u32 luma_dns:1;
	__u32 sharpen:1;
	__u32 chroma_gen:1;
	__u32 median:1;
	__u32 chroma_dns:1;
	__u32 color_comb:1;
	__u32 hdr:1;
	__u32 lut:1;
	__u32 tnf:1;
	__u32 dehaze:1;
	__u32 warp:1;
	__u32 reserved:11;
} __packed;

/**
 * struct kmb_isp_params - KMB ISP parameters structure
 *
 * @update: Select which parameters to apply, see kmb_vpu_isp_params_flags
 * @blc: Black Level correction parameters
 * @sigma_dns: Sigma denoise parameters
 * @lsc: Lens Shading Correction parameters
 * @raw: Raw parameters
 * @ae_awb: Auto exposure/Auto white balance parameters
 * @af: Auto focus parameters
 * @histogram: Histogram parameters
 * @lca: Lateral Chromatic Aberration filter parameters
 * @debayer: SIPP Bayer demosaicing filter parameters
 * @dog_dns: Difference-of-Gaussians filter parameters
 * @luma_dns: Luma denoise parameters
 * @sharpen: Sharpen filter parameters
 * @chroma_gen: Chroma GEN parameters
 * @median: Median hardware filter parameters
 * @chroma_dns: Chroma Denoise hardware filter parameters
 * @color_comb: Color Combine parameters
 * @hdr: HDR parameters applied only in HDR mode
 * @lut: LUT parameters
 * @tnf: Temporal Noise Filter parameters
 * @dehaze: Dehaze parameters
 * @warp: Warp filter parameters
 *
 * Each struct represents a filter and its settings which are applied on the raw
 * image.
 */
struct kmb_isp_params {
	struct kmb_isp_params_flags update;
	struct kmb_blc_params blc[KMB_CAM_MAX_EXPOSURES];
	struct kmb_sigma_dns_params sigma_dns[KMB_CAM_MAX_EXPOSURES];
	struct kmb_lsc_params lsc;
	struct kmb_raw_params raw;
	struct kmb_ae_awb_params ae_awb;
	struct kmb_af_params af;
	struct kmb_hist_params histogram;
	struct kmb_lca_params lca;
	struct kmb_debayer_params debayer;
	struct kmb_dog_dns_params dog_dns;
	struct kmb_luma_dns_params luma_dns;
	struct kmb_sharpen_params sharpen;
	struct kmb_chroma_gen_params chroma_gen;
	struct kmb_median_params median;
	struct kmb_chroma_dns_params chroma_dns;
	struct kmb_color_comb_params color_comb;
	struct kmb_hdr_params hdr;
	struct kmb_lut_params lut;
	struct kmb_tnf_params tnf;
	struct kmb_dehaze_params dehaze;
	struct kmb_warp_params warp;
} __packed;

/**
 * struct kmb_isp_stats_flags - Bits to indicate which stats need to be updated
 *
 * @ae_awb: 1 = updated, 0 = not updated.
 * @af: 1 = updated, 0 = not updated.
 * @luma_hist: 1 = updated, 0 = not updated.
 * @rgb_hist: 1 = updated, 0 = not updated.
 * @flicker_rows: 1 = updated, 0 = not updated.
 * @dehaze: 1 = updated, 0 = not updated.
 * @reserved: reserved for future use and for alignment
 */
struct kmb_isp_stats_flags {
	__u32 ae_awb:1;
	__u32 af:1;
	__u32 luma_hist:1;
	__u32 rgb_hist:1;
	__u32 flicker_rows:1;
	__u32 dehaze:1;
	__u32 reserved:26;
} __packed;

/**
 * struct kmb_isp_stats - KMB ISP raw statistics
 *
 * @exposure: Array with exposure statistics blocks. When HDR mode is not
 *            enable only statistics with index 0 are valid. Included stats:
 * @exposure.ae_awb_stats: Raw AE/AWB statistics.
 * @exposure.af_stats: Raw AF statistics.
 * @exposure.hist_luma: Luma histogram.
 * @exposure.hist_rgb: RGB histogram.
 * @exposure.flicker_rows: Flicker detection rows.
 * @dehaze: Dehaze statistics it is collected after HDR Fusion in HDR case.
 * @update: Select which stats to update, see kmb_vpu_isp_stats_flags
 */
struct kmb_isp_stats {
	struct {
		__u8 ae_awb_stats[KMB_CAM_AE_AWB_STATS_SIZE];
		__u8 af_stats[KMB_CAM_AF_STATS_SIZE];
		__u8 hist_luma[KMB_CAM_HIST_LUMA_SIZE];
		__u8 hist_rgb[KMB_CAM_HIST_RGB_SIZE];
		__u8 flicker_rows[KMB_CAM_FLICKER_ROWS_SIZE];
	} exposure[KMB_CAM_MAX_EXPOSURES];
	__u8 dehaze[MAX_DHZ_AIRLIGHT_STATS_SIZE];
	struct kmb_isp_stats_flags update;
} __packed;

#endif /* KEEMBAY_ISP_CTL_H */
