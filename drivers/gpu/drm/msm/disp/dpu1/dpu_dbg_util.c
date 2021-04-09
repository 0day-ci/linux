// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "dpu_dbg.h"

void dpu_dbg_dump_regs(u32 **reg, u32 len, void __iomem *base_addr,
		u32 dump_op, struct drm_printer *p)
{
	u32 len_aligned, len_padded;
	u32 x0, x4, x8, xc;
	void __iomem *addr;
	u32 *dump_addr = NULL;
	void __iomem *end_addr;
	int i;

	len_aligned = (len + REG_DUMP_ALIGN - 1) / REG_DUMP_ALIGN;
	len_padded = len_aligned * REG_DUMP_ALIGN;

	addr = base_addr;
	end_addr = base_addr + len;

	if (dump_op == DPU_DBG_DUMP_IN_COREDUMP && !p) {
		DRM_ERROR("invalid drm printer\n");
		return;
	}

	if (dump_op == DPU_DBG_DUMP_IN_MEM && !(*reg))
		*reg = kzalloc(len_padded, GFP_KERNEL);

	if (*reg)
		dump_addr = *reg;

	for (i = 0; i < len_aligned; i++) {
		if (dump_op == DPU_DBG_DUMP_IN_MEM) {
			x0 = (addr < end_addr) ? readl_relaxed(addr + 0x0) : 0;
			x4 = (addr + 0x4 < end_addr) ? readl_relaxed(addr + 0x4) : 0;
			x8 = (addr + 0x8 < end_addr) ? readl_relaxed(addr + 0x8) : 0;
			xc = (addr + 0xc < end_addr) ? readl_relaxed(addr + 0xc) : 0;
		}

		if (dump_addr) {
			if (dump_op == DPU_DBG_DUMP_IN_MEM) {
				dump_addr[i * 4] = x0;
				dump_addr[i * 4 + 1] = x4;
				dump_addr[i * 4 + 2] = x8;
				dump_addr[i * 4 + 3] = xc;
			} else if (dump_op == DPU_DBG_DUMP_IN_COREDUMP) {
				drm_printf(p, "0x%lx : %08x %08x %08x %08x\n",
						(unsigned long)(addr - base_addr),
						dump_addr[i * 4], dump_addr[i * 4 + 1],
						dump_addr[i * 4 + 2], dump_addr[i * 4 + 3]);
			}
			pr_debug("0x%lx : %08x %08x %08x %08x\n",
				(unsigned long)(addr - base_addr),
				dump_addr[i * 4], dump_addr[i * 4 + 1],
				dump_addr[i * 4 + 2], dump_addr[i * 4 + 3]);
		}

		addr += REG_DUMP_ALIGN;
	}
}

struct dpu_dbg_base *dpu_dbg_get(struct drm_device *drm)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;

	priv = drm->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	return dpu_kms->dpu_dbg;
}

bool dpu_dbg_is_drm_printer_needed(struct dpu_dbg_base *dpu_dbg)
{
	if ((dpu_dbg->reg_dump_method == DPU_DBG_DUMP_IN_COREDUMP) ||
			(dpu_dbg->reg_dump_method == DPU_DBG_DUMP_IN_LOG))
		return true;
	else
		return false;
}

static void dpu_dbg_dump_dsi_regs(struct drm_device *dev)
{
	struct msm_drm_private *priv;
	int i;

	priv = dev->dev_private;

	for (i = 0; i < ARRAY_SIZE(priv->dsi); i++) {
		if (!priv->dsi[i])
			continue;

		msm_dsi_dump_regs(priv->dsi[i]);
	}
}

static void dpu_dbg_dump_dp_regs(struct drm_device *dev)
{
	struct msm_drm_private *priv;

	priv = dev->dev_private;

	if (priv->dp)
		msm_dp_dump_regs(priv->dp);

}

static void dpu_dbg_dump_mdp_regs(struct drm_device *dev)
{
	dpu_kms_dump_mdp_regs(dev);
}

static void dpu_dbg_dump_all_regs(struct drm_device *dev)
{
	dpu_dbg_dump_mdp_regs(dev);
	dpu_dbg_dump_dsi_regs(dev);
	dpu_dbg_dump_dp_regs(dev);
}

void dpu_dbg_print_regs(struct drm_device *dev, u8 reg_dump_method)
{
	struct msm_drm_private *priv;
	int i;
	struct dpu_dbg_base *dpu_dbg;
	struct drm_printer *p;

	priv = dev->dev_private;
	dpu_dbg = dpu_dbg_get(dev);
	dpu_dbg->reg_dump_method = DPU_DBG_DUMP_IN_COREDUMP;

	p = dpu_dbg->dpu_dbg_printer;

	drm_printf(p, "===================mdp regs================\n");
	dpu_kms_dump_mdp_regs(dev);

	drm_printf(p, "===================dsi regs================\n");
	for (i = 0; i < ARRAY_SIZE(priv->dsi); i++) {
		if (!priv->dsi[i])
			continue;

		msm_dsi_dump_regs(priv->dsi[i]);
	}

	drm_printf(p, "===================dp regs================\n");

	if (priv->dp)
		msm_dp_dump_regs(priv->dp);
}

void dpu_dbg_dump_blks(struct dpu_dbg_base *dpu_dbg)
{
	int i;

	for (i = 0; i < DPU_DBG_BASE_MAX; i++) {
		if (dpu_dbg->blk_names[i] != NULL) {
			DRM_DEBUG("blk name is %s\n", dpu_dbg->blk_names[i]);
			if (!strcmp(dpu_dbg->blk_names[i], "all")) {
				dpu_dbg_dump_all_regs(dpu_dbg->drm_dev);
				break;
			} else if (!strcmp(dpu_dbg->blk_names[i], "mdp")) {
				dpu_dbg_dump_mdp_regs(dpu_dbg->drm_dev);
			} else if (!strcmp(dpu_dbg->blk_names[i], "dsi")) {
				dpu_dbg_dump_dsi_regs(dpu_dbg->drm_dev);
			} else if (!strcmp(dpu_dbg->blk_names[i], "dp")) {
				dpu_dbg_dump_dp_regs(dpu_dbg->drm_dev);
			} else {
				DRM_ERROR("blk name not found %s\n", dpu_dbg->blk_names[i]);
			}
		}
	}
}

void dpu_dbg_free_blk_mem(struct drm_device *drm_dev)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_mdss_cfg *cat;
	struct dpu_hw_mdp *top;
	struct dpu_dbg_base *dpu_dbg;
	int i;

	priv = drm_dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);
	dpu_dbg = dpu_kms->dpu_dbg;

	cat = dpu_kms->catalog;
	top = dpu_kms->hw_mdp;

	/* free CTL sub-blocks mem */
	for (i = 0; i < cat->ctl_count; i++)
		kfree(dpu_dbg->mdp_regs->ctl[i]);

	/* free DSPP sub-blocks mem */
	for (i = 0; i < cat->dspp_count; i++)
		kfree(dpu_dbg->mdp_regs->dspp[i]);

	/* free INTF sub-blocks mem */
	for (i = 0; i < cat->intf_count; i++)
		kfree(dpu_dbg->mdp_regs->intf[i]);

	/* free INTF sub-blocks mem */
	for (i = 0; i < cat->pingpong_count; i++)
		kfree(dpu_dbg->mdp_regs->pp[i]);

	/* free SSPP sub-blocks mem */
	for (i = 0; i < cat->sspp_count; i++)
		kfree(dpu_dbg->mdp_regs->sspp[i]);

	/* free TOP sub-blocks mem */
	kfree(dpu_dbg->mdp_regs->top);

	/* free DSI regs mem */
	for (i = 0; i < ARRAY_SIZE(priv->dsi); i++)
		kfree(dpu_dbg->dsi_ctrl_regs[i]);

	/* free DP regs mem */
	kfree(dpu_dbg->dp_ctrl_regs);
}

void dpu_dbg_init_blk_info(struct drm_device *drm_dev)
{
	struct msm_drm_private *priv;
	struct dpu_kms *dpu_kms;
	struct dpu_mdss_cfg *cat;
	struct dpu_hw_mdp *top;
	struct dpu_dbg_base *dpu_dbg;

	priv = drm_dev->dev_private;
	dpu_kms = to_dpu_kms(priv->kms);

	cat = dpu_kms->catalog;
	top = dpu_kms->hw_mdp;
	dpu_dbg = dpu_kms->dpu_dbg;

	dpu_dbg->mdp_regs = devm_kzalloc(drm_dev->dev, sizeof(struct dpu_mdp_regs), GFP_KERNEL);

	if (dpu_dbg->mdp_regs) {
		dpu_dbg->mdp_regs->ctl = devm_kzalloc(drm_dev->dev,
				cat->ctl_count * sizeof(u32 **), GFP_KERNEL);
		dpu_dbg->mdp_regs->dspp = devm_kzalloc(drm_dev->dev,
				cat->dspp_count * sizeof(u32 **), GFP_KERNEL);
		dpu_dbg->mdp_regs->intf = devm_kzalloc(drm_dev->dev,
				cat->intf_count * sizeof(u32 **), GFP_KERNEL);
		dpu_dbg->mdp_regs->sspp = devm_kzalloc(drm_dev->dev,
				cat->sspp_count * sizeof(u32 **), GFP_KERNEL);
		dpu_dbg->mdp_regs->pp = devm_kzalloc(drm_dev->dev,
				cat->pingpong_count * sizeof(u32 **), GFP_KERNEL);
	}

	dpu_dbg->dsi_ctrl_regs = devm_kzalloc(drm_dev->dev, ARRAY_SIZE(priv->dsi) *
			sizeof(dpu_dbg->dsi_ctrl_regs), GFP_KERNEL);
}
