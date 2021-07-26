/* SPDX-License-Identifier: GPL-2.0 */

/*
 * linux/sound/cs35l41.h -- Platform data for CS35L41
 *
 * Copyright (c) 2017-2021 Cirrus Logic Inc.
 *
 * Author: David Rhodes	<david.rhodes@cirrus.com>
 */

#ifndef __CS35L41_H
#define __CS35L41_H

enum cs35l41_clk_ids {
	CS35L41_CLKID_SCLK = 0,
	CS35L41_CLKID_LRCLK = 1,
	CS35L41_CLKID_MCLK = 4,
};

struct cs35l41_classh_cfg {
	bool classh_bst_override;
	bool classh_algo_enable;
	int classh_bst_max_limit;
	int classh_mem_depth;
	int classh_release_rate;
	int classh_headroom;
	int classh_wk_fet_delay;
	int classh_wk_fet_thld;
};

struct cs35l41_irq_cfg {
	bool irq_pol_inv;
	bool irq_out_en;
	int irq_src_sel;
};

struct cs35l41_platform_data {
	bool sclk_frc;
	bool lrclk_frc;
	bool right_channel;
	bool amp_gain_zc;
	bool dsp_ng_enable;
	bool invert_pcm;
	int bst_ind;
	int bst_vctrl;
	int bst_ipk;
	int bst_cap;
	int temp_warn_thld;
	int dsp_ng_pcm_thld;
	int dsp_ng_delay;
	unsigned int hw_ng_sel;
	unsigned int hw_ng_delay;
	unsigned int hw_ng_thld;
	int dout_hiz;
	struct cs35l41_irq_cfg irq_config1;
	struct cs35l41_irq_cfg irq_config2;
	struct cs35l41_classh_cfg classh_config;
};

#endif /* __CS35L41_H */
