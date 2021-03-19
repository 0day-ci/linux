/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay VPU ISP params
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_VPU_ISP_H
#define KEEMBAY_VPU_ISP_H

/* Keembay VPU ISP Tables sizes and limits */
#define KMB_VPU_MAX_EXPOSURES 3

/**
 * struct kmb_vpu_raw_stats - KMB Raw statisticsKMB
 *
 * @ae_awb_stats_addr: AE/AWB statistics addr
 * @af_stats_addr: Base start offset for AF statistics addr
 * @hist_luma_addr: Luma histogram addr
 * @hist_rgb_addr: RGB histogram addr
 * @flicker_rows_addr: Flicker detection raw addr
 */
struct kmb_vpu_raw_stats {
	u64 ae_awb_stats_addr;
	u64 af_stats_addr;
	u64 hist_luma_addr;
	u64 hist_rgb_addr;
	u64 flicker_rows_addr;
} __packed;

/**
 * struct kmb_vpu_blc_params - KMB Black Level Correction parameters
 *
 * @coeff1: Black level correction coefficient 1 parameter
 * @coeff2: Black level correction coefficient 2 parameter
 * @coeff3: Black level correction coefficient 3 parameter
 * @coeff4: Black level correction coefficient 4 parameter
 */
struct kmb_vpu_blc_params {
	u32 coeff1;
	u32 coeff2;
	u32 coeff3;
	u32 coeff4;
} __packed;

/**
 * struct kmb_vpu_sigma_dns_params - KMB Sigma Denoise parameters
 *
 * @noise: Sigma denoise noise parameter
 * @threshold1: Sigma denoise min threshold1 parameter
 * @threshold2: Sigma denoise max threshold2 parameter
 * @threshold3: Sigma denoise min threshold3 parameter
 * @threshold4: Sigma denoise max threshold4 parameter
 * @threshold5: Sigma denoise min threshold5 parameter
 * @threshold6: Sigma denoise max threshold6 parameter
 * @threshold7: Sigma denoise min threshold7 parameter
 * @threshold8: Sigma denoise max threshold8 parameter
 */
struct kmb_vpu_sigma_dns_params {
	u32 noise;
	u32 threshold1;
	u32 threshold2;
	u32 threshold3;
	u32 threshold4;
	u32 threshold5;
	u32 threshold6;
	u32 threshold7;
	u32 threshold8;
} __packed;

/**
 * struct kmb_vpu_lsc_params - KMB Lens Shading Correction parameters
 *
 * @threshold: Lens shading correction threshold parameter
 * @width: Lens shading correction width parameter
 * @height: Lens shading correction height parameter
 * @reserved: Reserved for alignment purpose
 * @addr: Lens shading correction table address
 */
struct kmb_vpu_lsc_params {
	u32 threshold;
	u32 width;
	u32 height;
	u8 reserved[4];
	u64 addr;
} __packed;

/**
 * struct kmb_vpu_raw_params - KMB Raw parameters
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
 * @alpha1: Raw alpha1 parameter
 * @alpha2: Raw alpha2 parameter
 * @alpha3: Raw alpha3 parameter
 * @alpha4: Raw alpha4 parameter
 * @threshold2: Raw threshold2 parameter
 * @static_defect_size: Static defect data size
 * @reserved: Reserved for alignment purpose
 * @static_defect_addr: Static defect data address
 * @flicker_first_row_acc: First row of flicker detection row accumulation
 * @flicker_last_row_acc: First row of flicker detection row accumulation
 * @stats: raw statistics buffers
 */
struct kmb_vpu_raw_params {
	u32 awb_stats_en;
	u32 awb_rgb_hist_en;
	u32 af_stats_en;
	u32 luma_hist_en;
	u32 flicker_accum_en;
	u32 bad_pixel_fix_en;
	u32 grgb_imb_en;
	u32 mono_imbalance_en;
	u32 gain1;
	u32 gain2;
	u32 gain3;
	u32 gain4;
	u32 stop1;
	u32 stop2;
	u32 stop3;
	u32 stop4;
	u32 threshold1;
	u32 alpha1;
	u32 alpha2;
	u32 alpha3;
	u32 alpha4;
	u32 threshold2;
	u32 static_defect_size;
	u8 reserved[4];
	u64 static_defect_addr;
	u32 flicker_first_row_acc;
	u32 flicker_last_row_acc;
	struct kmb_vpu_raw_stats stats[KMB_VPU_MAX_EXPOSURES];
} __packed;

/**
 * struct kmb_vpu_ae_awb_params - KMB AE/AWB statistics parameters
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
struct kmb_vpu_ae_awb_params {
	u32 start_x;
	u32 start_y;
	u32 width;
	u32 height;
	u32 skip_x;
	u32 skip_y;
	u32 patches_x;
	u32 patches_y;
	u16 threshold1;
	u16 threshold2;
} __packed;

/**
 * struct kmb_vpu_af_params - KMB Auto Focus parameters
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
struct kmb_vpu_af_params {
	u32 start_x;
	u32 start_y;
	u32 width;
	u32 height;
	u32 patches_x;
	u32 patches_y;
	s32 coeff;
	s32 threshold1;
	s32 threshold2;
	s32 coeffs1[11];
	s32 coeffs2[11];
} __packed;

/**
 * struct kmb_vpu_hist_params - KMB Hist parameters
 *
 * @start_x: Hist start_x parameter
 * @start_y: Hist start_y parameter
 * @end_x: Hist end_x parameter
 * @end_y: Hist end_y parameter
 * @matrix: Hist matrix parameter
 * @weight: Hist weight parameter
 */
struct kmb_vpu_hist_params {
	u32 start_x;
	u32 start_y;
	u32 end_x;
	u32 end_y;
	u16 matrix[9];
	u16 weight[3];
} __packed;

/**
 * struct kmb_vpu_lca_params - KMB Lateral Chromatic Aberration parameters
 *
 * @addr: LCA table address
 */
struct kmb_vpu_lca_params {
	u64 addr;
} __packed;

/**
 * struct kmb_vpu_debayer_params - KMB Debayer parameters
 *
 * @coeff1: Filter coeff1 parameter
 * @multiplier1: Filter multiplier1 parameter
 * @multiplier2: Filter multiplier2 parameter
 * @coeff2: Filter coeff2 parameter
 * @coeff3: Filter coeff3 parameter
 * @coeff4: Filter coeff4 parameter
 */
struct kmb_vpu_debayer_params {
	s32 coeff1;
	u32 multiplier1;
	u32 multiplier2;
	s32 coeff2;
	s32 coeff3;
	s32 coeff4;
} __packed;

/**
 * struct kmb_vpu_hdr_params - KMB HDR parameters
 *
 * @ratio: HDR ratio parameter
 * @scale: HDR scale parameter
 * @offset1: HDR offset1 parameter
 * @slope1: HDR slope1 parameter
 * @offset2: HDR offset2 parameter
 * @slope2: HDR slope2 parameter
 * @offset3: HDR offset3 parameter
 * @slope3: HDR slope3 parameter
 * @offset4: HDR offset4 parameter
 * @gain1: HDR gain1 parameter
 * @blur1: HDR blur1 parameter
 * @blur2: HDR blur2 parameter
 * @contrast1: HDR contrast1 parameter
 * @contrast2: HDR contrast2 parameter
 * @enable1: HDR enable1 parameter
 * @enable2: HDR enable2 parameter
 * @offset5: HDR offset5 parameter
 * @gain2: HDR gain2 parameter
 * @offset6: HDR offset6 parameter
 * @strength: HDR strength parameter
 * @reserved1: Reserved for alignment purpose
 * @luts_addr: HDR LUT address
 * @offset7: HDR offset7 parameter
 * @shift: HDR shift parameter
 * @field1: HDR filed1 parameter
 * @field2: HDR field2 parameter
 * @gain3: HDR gain3 parameter
 * @min: HDR min parameter
 * @reserved2: Reserved for alignment purpose
 */
struct kmb_vpu_hdr_params {
	u32 ratio[2];
	u32 scale[3];
	s32 offset1;
	u32 slope1;
	s32 offset2;
	u32 slope2;
	s32 offset3;
	u32 slope3;
	s32 offset4;
	u32 gain1;
	u32 blur1[3];
	u32 blur2[5];
	u32 contrast1;
	u32 contrast2;
	u32 enable1;
	u32 enable2;
	s32 offset5;
	u32 gain2;
	s32 offset6;
	u32 strength;
	u8 reserved1[4];
	u64 luts_addr;
	u16 offset7;
	u32 shift;
	u16 field1;
	u16 field2;
	u8 gain3;
	u16 min;
	u8 reserved2[3];
} __packed;

/**
 * struct kmb_vpu_dog_dns_params - KMB Difference-of-Gaussians DNS parameters
 *
 * @threshold: Filter threshold parameter
 * @strength: Filter strength parameter
 * @coeffs11: Filter coeffs11 parameter
 * @coeffs15: Filter coeffs15 parameter
 * @reserved: Reserved for alignment purpose
 */
struct kmb_vpu_dog_dns_params {
	u32 threshold;
	u32 strength;
	u8 coeffs11[6];
	u8 coeffs15[8];
	u8 reserved[2];
} __packed;

/**
 * struct kmb_vpu_luma_dns_params - KMB Luma DNS parameters
 *
 * @threshold: Luma DNS threshold parameter
 * @slope: Luma DNS slope parameter
 * @shift: Luma DNS shift parameter
 * @alpha: Luma DNS alpha parameter
 * @weight: Luma DNS weight parameter
 * @per_pixel_alpha_en: Enable adapt alpha
 * @gain_bypass_en: Enable gain bypass
 * @reserved: for alignment purpose
 */
struct kmb_vpu_luma_dns_params {
	u32 threshold;
	u32 slope;
	u32 shift;
	u32 alpha;
	u32 weight;
	u32 per_pixel_alpha_en;
	u32 gain_bypass_en;
	u8 reserved[4];
} __packed;

/**
 * struct kmb_vpu_sharpen_params - KMB Sharpen parameters
 *
 * @coeffs1: Filter coeffs1 parameter
 * @coeffs2: Filter coeffs2 parameter
 * @coeffs3: Filter coeffs3 parameter
 * @shift: Filter shift parameter
 * @gain1: Filter gain1 parameter
 * @gain2: Filter gain2 parameter
 * @gain3: Filter gain3 parameter
 * @gain4: Filter gain4 parameter
 * @gain5: Filter gain5 parameter
 * @stops1: Filter stops1 parameter
 * @gains: Filter gains parameter
 * @stops2: Filter stops2 parameter
 * @overshoot: Filter overshoot parameter
 * @undershoot: Filter undershoot parameter
 * @alpha: Filter alpha parameter
 * @gain6: Filter gain6 parameter
 * @offset: Filter offset parameter
 * @addr: Filter data address
 */
struct kmb_vpu_sharpen_params {
	u16 coeffs1[6];
	u16 coeffs2[6];
	u16 coeffs3[6];
	u32 shift;
	u32 gain1;
	u32 gain2;
	u32 gain3;
	u32 gain4;
	u32 gain5;
	u32 stops1[3];
	u32 gains[3];
	u32 stops2[4];
	u32 overshoot;
	u32 undershoot;
	u32 alpha;
	u32 gain6;
	u32 offset;
	u64 addr;
} __packed;

/**
 * struct kmb_vpu_chroma_gen_params - KMB Chroma GEN parameters
 *
 * @epsilon: Chroma GEN epsilon parameter
 * @coeff1: Chroma GEN coeff1 parameter
 * @coeff2: Chroma GEN coeff2 parameter
 * @coeff3: Chroma GEN coeff3 parameter
 * @coeff4: Chroma GEN coeff4 parameter
 * @coeff5: Chroma GEN coeff5 parameter
 * @coeff6: Chroma GEN coeff6 parameter
 * @strength1: Chroma GEN strength1 parameter
 * @strength2: Chroma GEN strength2 parameter
 * @coeffs: Chroma GEN coeffs parameter
 * @offset1: Chroma GEN offset1 parameter
 * @slope1: Chroma GEN slope1 parameter
 * @slope2: Chroma GEN slope2 parameter
 * @offset2: Chroma GEN offset2 parameter
 * @limit: Chroma GEN limit parameter
 */
struct kmb_vpu_chroma_gen_params {
	u32 epsilon;
	u32 coeff1;
	u32 coeff2;
	u32 coeff3;
	u32 coeff4;
	u32 coeff5;
	u32 coeff6;
	u32 strength1;
	u32 strength2;
	u32 coeffs[3];
	s32 offset1;
	u32 slope1;
	u32 slope2;
	s32 offset2;
	u32 limit;
} __packed;

/**
 * struct kmb_vpu_median_params - KMB Median parameters
 *
 * @size: Filter size parameter
 * @slope: Filter slope parameter
 * @offset: Filter offset parameter
 */
struct kmb_vpu_median_params {
	u32 size;
	u32 slope;
	s32 offset;
} __packed;

/**
 * struct kmb_vpu_chroma_dns_params - KMB Chroma Denoise parameters
 *
 * @limit: Filter limit parameter
 * @enable: Filter enable parameter
 * @threshold1: Filter threshold1 parameter
 * @threshold2: Filter threshold2 parameter
 * @threshold3: Filter threshold3 parameter
 * @threshold4: Filter threshold4 parameter
 * @threshold5: Filter threshold5 parameter
 * @threshold6: Filter threshold6 parameter
 * @threshold7: Filter threshold7 parameter
 * @threshold8: Filter threshold8 parameter
 * @slope1: Filter slope1 parameter
 * @offset1: Filter offset1 parameter
 * @slope2: Filter slope2 parameter
 * @offset2: Filter offset2 parameter
 * @grey1: Filter grey1 parameter
 * @grey2: Filter grey2 parameter
 * @grey3: Filter grey3 parameter
 * @coeff1: Filter coeff1 parameter
 * @coeff2: Filter coeff2 parameter
 * @coeff3: Filter coeff3 parameter
 */
struct kmb_vpu_chroma_dns_params {
	u32 limit;
	u32 enable;
	u32 threshold1;
	u32 threshold2;
	u32 threshold3;
	u32 threshold4;
	u32 threshold5;
	u32 threshold6;
	u32 threshold7;
	u32 threshold8;
	u32 slope1;
	s32 offset1;
	u32 slope2;
	s32 offset2;
	u32 grey1;
	u32 grey2;
	u32 grey3;
	u32 coeff1;
	u32 coeff2;
	u32 coeff3;
} __packed;

/**
 * struct kmb_vpu_color_comb_params - KMB Color Combine parameters
 *
 * @matrix: Color combine matrix parameter
 * @offsets:Color combine offsets parameter
 * @coeff1: Color combine coeff1 parameter
 * @coeff2: Color combine coeff2 parameter
 * @coeff3: Color combine coeff3 parameter
 * @reserved: Reserved for alignment purpose
 * @addr: Color combine table address
 * @enable: Color combine enable parameter
 * @weight1: Color combine weight1 parameter
 * @weight2: Color combine weight2 parameter
 * @weight3: Color combine weight3 parameter
 * @limit1: Color combine limit1 parameter
 * @limit2: Color combine limit2 parameter
 * @offset1: Color combine offset1 parameter
 * @offset2: Color combine offset2 parameter
 */
struct kmb_vpu_color_comb_params {
	u16 matrix[9];
	u16 offsets[3];
	u32 coeff1;
	u32 coeff2;
	u32 coeff3;
	u8 reserved[4];
	u64 addr;
	u32 enable;
	u32 weight1;
	u32 weight2;
	u32 weight3;
	u32 limit1;
	s32 limit2;
	s32 offset1;
	s32 offset2;
} __packed;

/**
 * struct kmb_vpu_lut_params - KMB lut parameters
 *
 * @size: Lut size parameter
 * @reserved: Reserved for alignment purpose
 * @addr: Lut table address
 * @matrix: Lut matrix parameter
 * @offsets: Lut offsets parameter
 */
struct kmb_vpu_lut_params {
	u32 size;
	u8 reserved[4];
	u64 addr;
	u16 matrix[3 * 3];
	u16 offsets[3];
} __packed;

/**
 * struct kmb_vpu_tnf_params - KMB Temporal Noise Filter parameters
 *
 * @factor: Filter factor parameter
 * @gain: Filter gain parameter
 * @offset1: Filter offset1 parameter
 * @slope1: Filter slope1 parameter
 * @offset2: Filter offset2 parameter
 * @slope2: Filter slope2 parameter
 * @min1: Filter min1 parameter
 * @min2: Filter min2 parameter
 * @value: Filter value parameter
 * @enable: Filter enable parameter
 * @lut0_addr: Filter lut0 address
 * @lut1_addr: Filter lut1 address
 */
struct kmb_vpu_tnf_params {
	u32 factor;
	u32 gain;
	u32 offset1;
	u32 slope1;
	u32 offset2;
	u32 slope2;
	u32 min1;
	u32 min2;
	u32 value;
	u32 enable;
	u64 lut0_addr;
	u64 lut1_addr;
} __packed;

/**
 * struct kmb_vpu_dehaze_params - KMB dehaze parameters
 *
 * @gain1: Dehaze gain1 parameter
 * @min: Dehaze min parameter
 * @strength1: Dehaze strength1 parameter
 * @strength2: Dehaze strength2 parameter
 * @gain2: Dehaze gain2 parameter
 * @saturation: Dehaze saturation parameter
 * @value1: Dehaze value1 parameter
 * @value2: Dehaze value2 parameter
 * @value3: Dehaze value3 parameter
 * @filter: Dehaze filter parameter
 * @stats_addr: Dehaze statistics address
 */
struct kmb_vpu_dehaze_params {
	u32 gain1;
	u32 min;
	u32 strength1;
	u32 strength2;
	u32 gain2;
	u32 saturation;
	u32 value1;
	u32 value2;
	u32 value3;
	u32 filter[3];
	u64 stats_addr;
} __packed;

/**
 * struct kmb_vpu_warp_params - KMB Warp filter parameters
 *
 * @type: Warp filter type parameter
 * @relative: Warp filter relative parameter
 * @format: Warp filter format parameter
 * @position: Warp filter position parameter
 * @reserved: Reserved for alignment purposes
 * @addr: Warp filter addr parameter
 * @width: Warp filter width parameter
 * @height: Warp filter height parameter
 * @stride: Warp filter stride parameter
 * @enable: Warp filter enable parameter
 * @matrix: Warp matrix parameter
 * @mode: Warp filter mode parameter
 * @values: Warp filter values parameter
 */
struct kmb_vpu_warp_params {
	u8 type;
	u8 relative;
	u8 format;
	u8 position;
	u8 reserved[4];
	u64 addr;
	u16 width;
	u16 height;
	u32 stride;
	u8 enable;
	u32 matrix[9];
	u8 mode;
	u16 values[3];
} __packed;

/**
 * enum kmb_vpu_bayer_order - KMB sensor Bayer arrangement format types
 *
 * @KMB_ISP_BAYER_ORDER_GRBG: Gr R B Gr
 * @KMB_ISP_BAYER_ORDER_RGGB: R Gr Gr B
 * @KMB_ISP_BAYER_ORDER_GBRG: Gr B R Gr
 * @KMB_ISP_BAYER_ORDER_BGGR: B Gr Gr R
 */
enum kmb_vpu_bayer_order {
	KMB_VPU_ISP_BAYER_ORDER_GRBG = 0,
	KMB_VPU_ISP_BAYER_ORDER_RGGB = 1,
	KMB_VPU_ISP_BAYER_ORDER_GBRG = 2,
	KMB_VPU_ISP_BAYER_ORDER_BGGR = 3,
} __packed;

/* Version of the VPU ISP ABI. It should be passed as
 * first argument in the isp params struct
 */
#define KMB_VPU_ISP_ABI_VERSION 104

/**
 * struct kmb_vpu_isp_params - KMB  VPU ISP parameters structure
 *
 * @header_version: Header Version
 * @image_data_width: Image data width
 * @num_exposures: Number of exposures
 * @bayer_order: enum kmb_isp_bayer_order
 * @user_data_key: Private key used for the client
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
 * @lut: lut parameters
 * @tnf: Temporal Noise Filter parameters
 * @dehaze: Dehaze parameters
 * @warp: Warp filter parameters
 */
struct kmb_vpu_isp_params {
	u32 header_version;
	u32 image_data_width;
	u32 num_exposures;
	u32 bayer_order;
	u32 user_data_key;
	struct kmb_vpu_blc_params blc[KMB_VPU_MAX_EXPOSURES];
	struct kmb_vpu_sigma_dns_params sigma_dns[KMB_VPU_MAX_EXPOSURES];
	struct kmb_vpu_lsc_params lsc;
	struct kmb_vpu_raw_params raw;
	struct kmb_vpu_ae_awb_params ae_awb;
	struct kmb_vpu_af_params af;
	struct kmb_vpu_hist_params histogram;
	struct kmb_vpu_lca_params lca;
	struct kmb_vpu_debayer_params debayer;
	struct kmb_vpu_dog_dns_params dog_dns;
	struct kmb_vpu_luma_dns_params luma_dns;
	struct kmb_vpu_sharpen_params sharpen;
	struct kmb_vpu_chroma_gen_params chroma_gen;
	struct kmb_vpu_median_params median;
	struct kmb_vpu_chroma_dns_params chroma_dns;
	struct kmb_vpu_color_comb_params color_comb;
	struct kmb_vpu_hdr_params hdr;
	struct kmb_vpu_lut_params lut;
	struct kmb_vpu_tnf_params tnf;
	struct kmb_vpu_dehaze_params dehaze;
	struct kmb_vpu_warp_params warp;
} __packed;

#endif /* KEEMBAY_VPU_ISP */
