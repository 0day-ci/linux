/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, Collabora
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@collabora.com>
 */

#ifndef HANTRO_G2_REGS_H_
#define HANTRO_G2_REGS_H_

#include "hantro.h"

#define G2_SWREG(nr)	((nr) * 4)

#define HEVC_DEC_REG(name, base, shift, mask) \
	static const struct hantro_reg _hevc_##name[] = { \
		{ G2_SWREG(base), (shift), (mask) } \
	}; \
	static const struct hantro_reg __maybe_unused *hevc_##name = &_hevc_##name[0];

#define HEVC_REG_VERSION		G2_SWREG(0)

#define HEVC_REG_INTERRUPT		G2_SWREG(1)
#define HEVC_REG_INTERRUPT_DEC_RDY_INT	BIT(12)
#define HEVC_REG_INTERRUPT_DEC_ABORT_E	BIT(5)
#define HEVC_REG_INTERRUPT_DEC_IRQ_DIS	BIT(4)
#define HEVC_REG_INTERRUPT_DEC_E	BIT(0)

HEVC_DEC_REG(strm_swap,		2, 28,	0xf)
HEVC_DEC_REG(dirmv_swap,	2, 20,	0xf)

HEVC_DEC_REG(mode,		  3, 27, 0x1f)
HEVC_DEC_REG(compress_swap,	  3, 20, 0xf)
HEVC_DEC_REG(ref_compress_bypass, 3, 17, 0x1)
HEVC_DEC_REG(out_rs_e,		  3, 16, 0x1)
HEVC_DEC_REG(out_dis,		  3, 15, 0x1)
HEVC_DEC_REG(out_filtering_dis,   3, 14, 0x1)
HEVC_DEC_REG(write_mvs_e,	  3, 12, 0x1)

HEVC_DEC_REG(pic_width_in_cbs,	4, 19,	0x1ff)
HEVC_DEC_REG(pic_height_in_cbs,	4, 6,	0x1ff)
HEVC_DEC_REG(num_ref_frames,	4, 0,	0x1f)

HEVC_DEC_REG(scaling_list_e,	5, 24,	0x1)
HEVC_DEC_REG(cb_qp_offset,	5, 19,	0x1f)
HEVC_DEC_REG(cr_qp_offset,	5, 14,	0x1f)
HEVC_DEC_REG(sign_data_hide,	5, 12,	0x1)
HEVC_DEC_REG(tempor_mvp_e,	5, 11,	0x1)
HEVC_DEC_REG(max_cu_qpd_depth,	5, 5,	0x3f)
HEVC_DEC_REG(cu_qpd_e,		5, 4,	0x1)

HEVC_DEC_REG(stream_len,	6, 0,	0xffffffff)

HEVC_DEC_REG(cabac_init_present, 7, 31, 0x1)
HEVC_DEC_REG(weight_pred_e,	 7, 28, 0x1)
HEVC_DEC_REG(weight_bipr_idc,	 7, 26, 0x3)
HEVC_DEC_REG(filter_over_slices, 7, 25, 0x1)
HEVC_DEC_REG(filter_over_tiles,  7, 24, 0x1)
HEVC_DEC_REG(asym_pred_e,	 7, 23, 0x1)
HEVC_DEC_REG(sao_e,		 7, 22, 0x1)
HEVC_DEC_REG(pcm_filt_d,	 7, 21, 0x1)
HEVC_DEC_REG(slice_chqp_present, 7, 20, 0x1)
HEVC_DEC_REG(dependent_slice,	 7, 19, 0x1)
HEVC_DEC_REG(filter_override,	 7, 18, 0x1)
HEVC_DEC_REG(strong_smooth_e,	 7, 17, 0x1)
HEVC_DEC_REG(filt_offset_beta,	 7, 12, 0x1f)
HEVC_DEC_REG(filt_offset_tc,	 7, 7,  0x1f)
HEVC_DEC_REG(slice_hdr_ext_e,	 7, 6,	0x1)
HEVC_DEC_REG(slice_hdr_ext_bits, 7, 3,	0x7)

HEVC_DEC_REG(const_intra_e,	 8, 31, 0x1)
HEVC_DEC_REG(filt_ctrl_pres,	 8, 30, 0x1)
HEVC_DEC_REG(idr_pic_e,		 8, 16, 0x1)
HEVC_DEC_REG(bit_depth_pcm_y,	 8, 12, 0xf)
HEVC_DEC_REG(bit_depth_pcm_c,	 8, 8,  0xf)
HEVC_DEC_REG(bit_depth_y_minus8, 8, 6,  0x3)
HEVC_DEC_REG(bit_depth_c_minus8, 8, 4,  0x3)
HEVC_DEC_REG(output_8_bits,	 8, 3,  0x1)

HEVC_DEC_REG(refidx1_active,	9, 19,	0x1f)
HEVC_DEC_REG(refidx0_active,	9, 14,	0x1f)
HEVC_DEC_REG(hdr_skip_length,	9, 0,	0x3fff)

HEVC_DEC_REG(start_code_e,	10, 31, 0x1)
HEVC_DEC_REG(init_qp,		10, 24, 0x3f)
HEVC_DEC_REG(num_tile_cols,	10, 19, 0x1f)
HEVC_DEC_REG(num_tile_rows,	10, 14, 0x1f)
HEVC_DEC_REG(tile_e,		10, 1,	0x1)
HEVC_DEC_REG(entropy_sync_e,	10, 0,	0x1)

HEVC_DEC_REG(refer_lterm_e,	12, 16, 0xffff)
HEVC_DEC_REG(min_cb_size,	12, 13, 0x7)
HEVC_DEC_REG(max_cb_size,	12, 10, 0x7)
HEVC_DEC_REG(min_pcm_size,	12, 7,  0x7)
HEVC_DEC_REG(max_pcm_size,	12, 4,  0x7)
HEVC_DEC_REG(pcm_e,		12, 3,  0x1)
HEVC_DEC_REG(transform_skip,	12, 2,	0x1)
HEVC_DEC_REG(transq_bypass,	12, 1,	0x1)
HEVC_DEC_REG(list_mod_e,	12, 0,	0x1)

HEVC_DEC_REG(min_trb_size,	  13, 13, 0x7)
HEVC_DEC_REG(max_trb_size,	  13, 10, 0x7)
HEVC_DEC_REG(max_intra_hierdepth, 13, 7,  0x7)
HEVC_DEC_REG(max_inter_hierdepth, 13, 4,  0x7)
HEVC_DEC_REG(parallel_merge,	  13, 0,  0xf)

HEVC_DEC_REG(rlist_f0,		14, 0,	0x1f)
HEVC_DEC_REG(rlist_f1,		14, 10,	0x1f)
HEVC_DEC_REG(rlist_f2,		14, 20,	0x1f)
HEVC_DEC_REG(rlist_b0,		14, 5,	0x1f)
HEVC_DEC_REG(rlist_b1,		14, 15, 0x1f)
HEVC_DEC_REG(rlist_b2,		14, 25, 0x1f)

HEVC_DEC_REG(rlist_f3,		15, 0,	0x1f)
HEVC_DEC_REG(rlist_f4,		15, 10, 0x1f)
HEVC_DEC_REG(rlist_f5,		15, 20, 0x1f)
HEVC_DEC_REG(rlist_b3,		15, 5,	0x1f)
HEVC_DEC_REG(rlist_b4,		15, 15, 0x1f)
HEVC_DEC_REG(rlist_b5,		15, 25, 0x1f)

HEVC_DEC_REG(rlist_f6,		16, 0,	0x1f)
HEVC_DEC_REG(rlist_f7,		16, 10, 0x1f)
HEVC_DEC_REG(rlist_f8,		16, 20, 0x1f)
HEVC_DEC_REG(rlist_b6,		16, 5,	0x1f)
HEVC_DEC_REG(rlist_b7,		16, 15, 0x1f)
HEVC_DEC_REG(rlist_b8,		16, 25, 0x1f)

HEVC_DEC_REG(rlist_f9,		17, 0,	0x1f)
HEVC_DEC_REG(rlist_f10,		17, 10, 0x1f)
HEVC_DEC_REG(rlist_f11,		17, 20, 0x1f)
HEVC_DEC_REG(rlist_b9,		17, 5,	0x1f)
HEVC_DEC_REG(rlist_b10,		17, 15, 0x1f)
HEVC_DEC_REG(rlist_b11,		17, 25, 0x1f)

HEVC_DEC_REG(rlist_f12,		18, 0,	0x1f)
HEVC_DEC_REG(rlist_f13,		18, 10, 0x1f)
HEVC_DEC_REG(rlist_f14,		18, 20, 0x1f)
HEVC_DEC_REG(rlist_b12,		18, 5,	0x1f)
HEVC_DEC_REG(rlist_b13,		18, 15, 0x1f)
HEVC_DEC_REG(rlist_b14,		18, 25, 0x1f)

HEVC_DEC_REG(rlist_f15,		19, 0,	0x1f)
HEVC_DEC_REG(rlist_b15,		19, 5,	0x1f)

HEVC_DEC_REG(partial_ctb_x,	20, 31, 0x1)
HEVC_DEC_REG(partial_ctb_y,	20, 30, 0x1)
HEVC_DEC_REG(pic_width_4x4,	20, 16, 0xfff)
HEVC_DEC_REG(pic_height_4x4,	20, 0,  0xfff)

HEVC_DEC_REG(cur_poc_00,	46, 24,	0xff)
HEVC_DEC_REG(cur_poc_01,	46, 16,	0xff)
HEVC_DEC_REG(cur_poc_02,	46, 8,	0xff)
HEVC_DEC_REG(cur_poc_03,	46, 0,	0xff)

HEVC_DEC_REG(cur_poc_04,	47, 24,	0xff)
HEVC_DEC_REG(cur_poc_05,	47, 16,	0xff)
HEVC_DEC_REG(cur_poc_06,	47, 8,	0xff)
HEVC_DEC_REG(cur_poc_07,	47, 0,	0xff)

HEVC_DEC_REG(cur_poc_08,	48, 24,	0xff)
HEVC_DEC_REG(cur_poc_09,	48, 16,	0xff)
HEVC_DEC_REG(cur_poc_10,	48, 8,	0xff)
HEVC_DEC_REG(cur_poc_11,	48, 0,	0xff)

HEVC_DEC_REG(cur_poc_12,	49, 24, 0xff)
HEVC_DEC_REG(cur_poc_13,	49, 16, 0xff)
HEVC_DEC_REG(cur_poc_14,	49, 8,	0xff)
HEVC_DEC_REG(cur_poc_15,	49, 0,	0xff)

HEVC_DEC_REG(apf_threshold,	55, 0,	0xffff)

HEVC_DEC_REG(clk_gate_e,	58, 16,	0x1)
HEVC_DEC_REG(buswidth,		58, 8,	0x7)
HEVC_DEC_REG(max_burst,		58, 0,	0xff)

#define HEVC_REG_CONFIG				G2_SWREG(58)
#define HEVC_REG_CONFIG_DEC_CLK_GATE_E		BIT(16)
#define HEVC_REG_CONFIG_DEC_CLK_GATE_IDLE_E	BIT(17)

#define HEVC_ADDR_DST		(G2_SWREG(65))
#define HEVC_REG_ADDR_REF(i)	(G2_SWREG(67)  + ((i) * 0x8))
#define HEVC_ADDR_DST_CHR	(G2_SWREG(99))
#define HEVC_REG_CHR_REF(i)	(G2_SWREG(101) + ((i) * 0x8))
#define HEVC_ADDR_DST_MV	(G2_SWREG(133))
#define HEVC_REG_DMV_REF(i)	(G2_SWREG(135) + ((i) * 0x8))
#define HEVC_ADDR_TILE_SIZE	(G2_SWREG(167))
#define HEVC_ADDR_STR		(G2_SWREG(169))
#define HEVC_SCALING_LIST	(G2_SWREG(171))
#define HEVC_RASTER_SCAN	(G2_SWREG(175))
#define HEVC_RASTER_SCAN_CHR	(G2_SWREG(177))
#define HEVC_TILE_FILTER	(G2_SWREG(179))
#define HEVC_TILE_SAO		(G2_SWREG(181))
#define HEVC_TILE_BSD		(G2_SWREG(183))

HEVC_DEC_REG(strm_buffer_len,	258, 0,	0xffffffff)
HEVC_DEC_REG(strm_start_offset,	259, 0,	0xffffffff)

#endif
