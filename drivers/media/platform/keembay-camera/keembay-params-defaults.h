/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera ISP parameter defaults.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#ifndef KEEMBAY_DEFAULTS_H
#define KEEMBAY_DEFAULTS_H

#include "keembay-vpu-isp.h"

struct kmb_vpu_isp_params_defaults {
	const struct kmb_vpu_blc_params *blc;
	const struct kmb_vpu_sigma_dns_params *sigma_dns;
	const struct kmb_vpu_lsc_params *lsc;
	const struct kmb_vpu_raw_params *raw;
	const struct kmb_vpu_ae_awb_params *ae_awb;
	const struct kmb_vpu_af_params *af;
	const struct kmb_vpu_hist_params *histogram;
	const struct kmb_vpu_lca_params *lca;
	const struct kmb_vpu_debayer_params *debayer;
	const struct kmb_vpu_dog_dns_params *dog_dns;
	const struct kmb_vpu_luma_dns_params *luma_dns;
	const struct kmb_vpu_sharpen_params *sharpen;
	const struct kmb_vpu_chroma_gen_params *chroma_gen;
	const struct kmb_vpu_median_params *median;
	const struct kmb_vpu_chroma_dns_params *chroma_dns;
	const struct kmb_vpu_color_comb_params *color_comb;
	const struct kmb_vpu_hdr_params *hdr;
	const struct kmb_vpu_lut_params *lut;
	const struct kmb_vpu_tnf_params *tnf;
	const struct kmb_vpu_dehaze_params *dehaze;
	const struct kmb_vpu_warp_params *warp;
};

void kmb_params_get_defaults(struct kmb_vpu_isp_params_defaults *defaults);

#endif /* KEEMBAY_DEFAULTS_H */
