// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VCODEC_DEC_HW_H_
#define _MTK_VCODEC_DEC_HW_H_

#include <linux/io.h>
#include <linux/platform_device.h>

#include "mtk_vcodec_drv.h"

#define VDEC_HW_ACTIVE 0x10
#define VDEC_IRQ_CFG 0x11
#define VDEC_IRQ_CLR 0x10
#define VDEC_IRQ_CFG_REG 0xa4

extern const struct of_device_id mtk_vdec_hw_match[MTK_VDEC_HW_MAX];

/**
 * enum mtk_comp_hw_reg_idx - component register base index
 */
enum mtk_comp_hw_reg_idx {
	VDEC_COMP_SYS,
	VDEC_COMP_MISC,
	VDEC_COMP_MAX
};

/**
 * struct mtk_vdec_hw_dev - vdec hardware driver data
 * @plat_dev: platform device
 * @master_dev: master device
 * @reg_base: Mapped address of MTK Vcodec registers.
 *
 * @curr_ctx: The context that is waiting for codec hardware
 *
 * @dec_irq: decoder irq resource
 * @pm: power management control
 * @comp_idx: each hardware index
 */
struct mtk_vdec_hw_dev {
	struct platform_device *plat_dev;
	struct mtk_vcodec_dev *master_dev;
	void __iomem *reg_base[VDEC_COMP_MAX];

	struct mtk_vcodec_ctx *curr_ctx;

	int dec_irq;
	struct mtk_vcodec_pm pm;
	int comp_idx;
};

#endif /* _MTK_VCODEC_DEC_HW_H_ */
