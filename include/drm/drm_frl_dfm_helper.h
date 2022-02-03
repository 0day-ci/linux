/* SPDX-License-Identifier: MIT
 * Copyright © 2022 Intel Corp
 */

#ifndef DRM_FRL_DFM_H_
#define DRM_FRL_DFM_H_

/* DFM constraints and tolerance values from HDMI2.1 spec */
#define TB_BORROWED_MAX			400
#define FRL_CHAR_PER_CHAR_BLK		510
/* Tolerance pixel clock unit is in  mHz */
#define TOLERANCE_PIXEL_CLOCK		5
#define TOLERANCE_FRL_BIT_RATE		300
#define TOLERANCE_AUDIO_CLOCK		1000
#define ACR_RATE_MAX			1500
#define EFFICIENCY_MULTIPLIER		1000
#define OVERHEAD_M			(3 * EFFICIENCY_MULTIPLIER / 1000)
#define BPP_MULTIPLIER			16
#define FRL_TIMING_NS_MULTIPLIER	1000000000

/* ALl the input config needed to compute DFM requirements */
struct drm_frl_dfm_input_config {

	/*
	 * Pixel clock rate kHz, when FVA is
	 * enabled this rate is the rate after adjustment
	 */
	u32 pixel_clock_nominal_khz;

	/* active pixels per line */
	u32 hactive;

	/* Blanking pixels per line */
	u32 hblank;

	/* Bits per component */
	u32 bpc;

	/* Pixel encoding */
	u32 color_format;

	/* FRL bit rate in kbps */
	u32 bit_rate_kbps;

	/* FRL lanes */
	u32 lanes;

	/* Number of audio channels */
	u32 audio_channels;

	/* Audio rate in Hz */
	u32 audio_hz;

	/* Selected bpp target value */
	u32 target_bpp_16;

	/*
	 * Number of horizontal pixels in a slice.
	 * Equivalent to PPS parameter slice_width
	 */
	u32 slice_width;
};

/* Computed dfm parameters as per the HDMI2.1 spec */
struct drm_frl_dfm_params {

	/*
	 * Link overhead in percentage
	 * multiplied by 1000 (efficiency multiplier)
	 */
	u32 overhead_max;

	/* Maximum pixel rate in kHz */
	u32 pixel_clock_max_khz;

	/* Minimum video line period in nano sec */
	u32 line_time_ns;

	/* worst case slow frl character rate in kbps */
	u32 char_rate_min_kbps;

	/* minimum total frl charecters per line perios */
	u32 cfrl_line;

	/* Average tribyte rate in khz */
	u32 ftb_avg_k;

	/* Audio characteristics */

	/*  number of audio packets needed during hblank */
	u32 num_audio_pkts_line;

	/*
	 *  Minimum required hblank assuming no control preiod
	 *  RC compression
	 */
	u32 hblank_audio_min;

	/* Number of tribytes required to carry active video */
	u32 tb_active;

	/* Total available tribytes during the blanking period */
	u32 tb_blank;

	/*
	 * Number of tribytes required to be transmitted during
	 * the hblank period
	 */
	u32 tb_borrowed;

	/* DSC frl characteristics */

	/* Tribytes required to carry the target bpp */
	u32 hcactive_target;

	/* tribytes available during blanking with target bpp */
	u32 hcblank_target;
};

/* FRL DFM structure to hold involved in DFM computation */
struct drm_hdmi_frl_dfm {
	struct drm_frl_dfm_input_config config;
	struct drm_frl_dfm_params params;
};

bool drm_frl_dfm_nondsc_requirement_met(struct drm_hdmi_frl_dfm *frl_dfm);

bool
drm_frl_dfm_dsc_requirement_met(struct drm_hdmi_frl_dfm *frl_dfm);

#endif
