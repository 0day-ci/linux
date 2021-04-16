/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __DRAMC_H__
#define __DRAMC_H__

int mtk_dramc_get_steps_freq(unsigned int step);
unsigned int mtk_dramc_get_ddr_type(void);
unsigned int mtk_dramc_get_data_rate(void);
unsigned int mtk_dramc_get_mr4(unsigned int ch);
unsigned int mtk_dramc_get_channel_count(void);
unsigned int mtk_dramc_get_rank_count(void);
unsigned int mtk_dramc_get_rank_size(unsigned int rk);

#endif /* __DRAMC_H__ */

