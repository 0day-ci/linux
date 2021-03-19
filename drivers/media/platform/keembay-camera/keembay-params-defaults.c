// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera ISP parameter defaults.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/stddef.h>
#include <linux/types.h>

#include "keembay-params-defaults.h"

static const struct kmb_vpu_blc_params blc_default[KMB_VPU_MAX_EXPOSURES] = {
		{
			.coeff1 = 800,
			.coeff2 = 800,
			.coeff3 = 800,
			.coeff4 = 800,
		},
		{
			.coeff1 = 800,
			.coeff2 = 800,
			.coeff3 = 800,
			.coeff4 = 800,
		},
		{
			.coeff1 = 800,
			.coeff2 = 800,
			.coeff3 = 800,
			.coeff4 = 800,
		}

};

static const struct kmb_vpu_sigma_dns_params
	sigma_dns_default[KMB_VPU_MAX_EXPOSURES] = { 0 };

static const struct kmb_vpu_lsc_params lsc_default = {
	.threshold = 2048,
	.width = 64,
	.height = 44,
	.reserved = { 0 },
};

static const struct kmb_vpu_raw_params raw_default = {
	.awb_stats_en = 0,
	.awb_rgb_hist_en = 0,
	.af_stats_en = 0,
	.luma_hist_en = 0,
	.flicker_accum_en = 0,
	.bad_pixel_fix_en = 0,
	.grgb_imb_en = 1,
	.mono_imbalance_en = 0,
	.gain1 = 269,
	.gain2 = 452,
	.gain3 = 634,
	.gain4 = 269,
	.stop1 = 400,
	.stop2 = 450,
	.stop3 = 700,
	.stop4 = 800,
	.threshold1 = 128,
	.alpha1 = 12,
	.alpha2 = 12,
	.alpha3 = 12,
	.alpha4 = 12,
	.threshold2 = 53,
	.static_defect_size = 1,
	.reserved = { 0 },
	.flicker_first_row_acc = 0,
	.flicker_last_row_acc = 0,
};

static const struct kmb_vpu_ae_awb_params ae_awb_default = {
	.start_x = 0,
	.start_y = 0,
	.width = 100,
	.height = 98,
	.skip_x = 100,
	.skip_y = 98,
	.patches_x = 38,
	.patches_y = 22,
	.threshold1 = 0,
	.threshold2 = 4095,
};

static const struct kmb_vpu_af_params af_default = {
	.start_x = 0,
	.start_y = 0,
	.width = 192,
	.height = 144,
	.patches_x = 20,
	.patches_y = 15,
	.coeff = 0,
	.threshold1 = 0,
	.threshold2 = 0,
	.coeffs1 = {31, 19, -32, 31, 63, 31, -50, -35, 35, -70, 35},
	.coeffs2 = {35, 11, -29, 8, 17, 8, 78, -39, 119, -238, 119},
};

static const struct kmb_vpu_hist_params histogram_default = {
	.start_x = 0,
	.start_y = 0,
	.end_x = 3839,
	.end_y = 2156,
	.matrix = {1719, 0, 0, 0, 1024, 0, 0, 0, 2414},
	.weight = {64, 128, 64},
};

// only address - nothing to init...
static const struct kmb_vpu_lca_params lca_default = { 0 };

static const struct kmb_vpu_debayer_params debayer_default = {
	.coeff1 = 51,
	.multiplier1 = 13107,
	.multiplier2 = 13107,
	.coeff2 = 77,
	.coeff3 = 150,
	.coeff4 = 29,
};

static const struct kmb_vpu_dog_dns_params dog_dns_default = {
	.threshold = 0,
	.strength = 0,
	.coeffs11 = {0, 0, 0, 0, 0, 255},
	.coeffs15 = {0, 0, 0, 0, 0, 0, 0, 255},
	.reserved = { 0 },
};

static const struct kmb_vpu_luma_dns_params luma_dns_default = {
	.threshold = 13094,
	.slope = 967,
	.shift = 7,
	.alpha = 50,
	.weight = 0,
	.per_pixel_alpha_en = 0,
	.gain_bypass_en = 0,
	.reserved = { 0 },
};

static const struct kmb_vpu_sharpen_params sharpen_default =  {
	.coeffs1 = {0, 0, 0, 4, 182, 396},
	.coeffs2 = {0, 0, 0, 1, 141, 740},
	.coeffs3 = {0, 0, 2, 42, 246, 444},
	.shift = 15,
	.gain1 = 3396,
	.gain2 = 3378,
	.gain3 = 3270,
	.gain4 = 3400,
	.gain5 = 207,
	.stops1 = {20, 40, 605},
	.gains = {10, 120, 60},
	.stops2 = {11, 100, 2500, 4000},
	.overshoot = 359,
	.undershoot = 146,
	.alpha = 36,
	.gain6 = 128,
	.offset = 637,
};

static const struct kmb_vpu_chroma_gen_params chroma_gen_default  = {
	.epsilon = 2,
	.coeff1 = 426,
	.coeff2 = 767,
	.coeff3 = 597,
	.coeff4 = 77,
	.coeff5 = 150,
	.coeff6 = 29,
	.strength1 = 0,
	.strength2 = 32,
	.coeffs = {33, 59, 71},
	.offset1 = 2,
	.slope1 = 230,
	.slope2 = 256,
	.offset2 = 0,
	.limit = 767,
};

static const struct kmb_vpu_median_params median_default = {
	.size = 7,
	.slope = 32,
	.offset = -19,
};

static const struct kmb_vpu_chroma_dns_params chroma_dns_default = {
	.limit = 255,
	.enable = 0,
	.threshold1 = 30,
	.threshold2 = 30,
	.threshold3 = 30,
	.threshold4 = 30,
	.threshold5 = 45,
	.threshold6 = 45,
	.threshold7 = 45,
	.threshold8 = 45,
	.slope1 = 77,
	.offset1 = -15,
	.slope2 = 255,
	.offset2 = 127,
	.grey1 = 421,
	.grey2 = 758,
	.grey3 = 590,
	.coeff1 = 52,
	.coeff2 = 32,
	.coeff3 = 19,
};

static const struct kmb_vpu_color_comb_params color_comb_default = {
	.matrix = {1303, 65427, 65367, 65172, 1463, 65462, 55, 65034, 1459},
	.offsets = { 0 },
	.coeff1 = 615,
	.coeff2 = 342,
	.coeff3 = 439,
	.reserved = { 0 },
	.enable = 0,
	.weight1 = 85,
	.weight2 = 86,
	.weight3 = 85,
	.limit1 = 512,
	.limit2 = -8192,
	.offset1 = 0,
	.offset2 = 0,
};

static const struct kmb_vpu_hdr_params hdr_default = {
	.ratio = {256, 256},
	.scale = {262143, 262143, 262143},
	.offset1 = -3275,
	.slope1 = 320,
	.offset2 = -3685,
	.slope2 = 641,
	.offset3 = -4054,
	.slope3 = 4095,
	.offset4 = 3686,
	.gain1 = 16,
	.blur1 = {0, 0, 255},
	.blur2 = {0, 0, 0, 0, 255},
	.contrast1 = 20,
	.contrast2 = 16,
	.enable1 = 0,
	.enable2 = 0,
	.offset5 = 0,
	.offset6 = 0,
	.strength = 0,
	.reserved1 = { 0 },
	.offset7 = 15,
	.shift = 1702133760,
	.field1 = 16,
	.field2 = 123,
	.gain3 = 0,
	.min = 0,
	.reserved2 = { 0 },
};

static const struct kmb_vpu_lut_params lut_default = {
	.size = 512,
	.reserved = { 0 },
	.matrix = {262, 516, 100, 3945, 3799, 449, 449, 3720, 4023},
	.offsets = {256, 2048, 2048},
};

static const struct kmb_vpu_tnf_params tnf_default = {
	.factor = 179,
	.gain = 0,
	.offset1 = 217,
	.slope1 = 162,
	.offset2 = 299,
	.slope2 = 121,
	.min1 = 0,
	.min2 = 40,
	.value = 128,
	.enable = 0,
};

static const struct kmb_vpu_dehaze_params dehaze_default = {
	.gain1 = 512,
	.min = 70,
	.strength1 = 0,
	.strength2 = 0,
	.gain2 = 128,
	.saturation = 127,
	.value1 = 2048,
	.value2 = 2048,
	.value3 = 2048,
	.filter = {0, 0, 255},
};

static const struct kmb_vpu_warp_params warp_default = {
	.type = 0,
	.relative = 0,
	.format = 0,
	.position = 0,
	.reserved = { 0 },
	.width = 8,
	.height = 4,
	.stride = 128,
	.enable = 0,
	.matrix = {1, 0, 0, 0, 1, 0, 0, 0, 1},
	.mode = 1,
	.values = {0, 128, 128},
};

void kmb_params_get_defaults(struct kmb_vpu_isp_params_defaults *defaults)
{
	defaults->blc = blc_default;
	defaults->sigma_dns = sigma_dns_default;
	defaults->lsc = &lsc_default;
	defaults->raw = &raw_default;
	defaults->ae_awb = &ae_awb_default;
	defaults->af = &af_default;
	defaults->histogram = &histogram_default;
	defaults->lca = &lca_default;
	defaults->debayer = &debayer_default;
	defaults->dog_dns = &dog_dns_default;
	defaults->luma_dns = &luma_dns_default;
	defaults->sharpen = &sharpen_default;
	defaults->chroma_gen = &chroma_gen_default;
	defaults->median = &median_default;
	defaults->chroma_dns = &chroma_dns_default;
	defaults->color_comb = &color_comb_default;
	defaults->hdr = &hdr_default;
	defaults->lut = &lut_default;
	defaults->tnf = &tnf_default;
	defaults->dehaze = &dehaze_default;
	defaults->warp = &warp_default;
}

