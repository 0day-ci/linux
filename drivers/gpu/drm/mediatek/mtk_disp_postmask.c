// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_disp_drv.h"
#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"

#define DISP_POSTMASK_EN			0x0000
#define POSTMASK_EN					BIT(0)
#define DISP_POSTMASK_CFG			0x0020
#define POSTMASK_RELAY_MODE				BIT(0)
#define DISP_POSTMASK_SIZE			0x0030

struct mtk_disp_postmask_data {
	u32 reserved;
};

/*
 * struct mtk_disp_postmask - DISP_POSTMASK driver structure
 */
struct mtk_disp_postmask {
	struct clk *clk;
	void __iomem *regs;
	struct cmdq_client_reg cmdq_reg;
	const struct mtk_disp_postmask_data *data;
};

int mtk_postmask_clk_enable(struct device *dev)
{
	struct mtk_disp_postmask *postmask = dev_get_drvdata(dev);

	return clk_prepare_enable(postmask->clk);
}

void mtk_postmask_clk_disable(struct device *dev)
{
	struct mtk_disp_postmask *postmask = dev_get_drvdata(dev);

	clk_disable_unprepare(postmask->clk);
}

void mtk_postmask_config(struct device *dev, unsigned int w,
				unsigned int h, unsigned int vrefresh,
				unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_postmask *postmask = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &postmask->cmdq_reg, postmask->regs,
		      DISP_POSTMASK_SIZE);
	mtk_ddp_write(cmdq_pkt, POSTMASK_RELAY_MODE, &postmask->cmdq_reg,
		      postmask->regs, DISP_POSTMASK_CFG);
}

void mtk_postmask_start(struct device *dev)
{
	struct mtk_disp_postmask *postmask = dev_get_drvdata(dev);

	writel(POSTMASK_EN, postmask->regs + DISP_POSTMASK_EN);
}

void mtk_postmask_stop(struct device *dev)
{
	struct mtk_disp_postmask *postmask = dev_get_drvdata(dev);

	writel_relaxed(0x0, postmask->regs + DISP_POSTMASK_EN);
}

static int mtk_disp_postmask_bind(struct device *dev, struct device *master,
				  void *data)
{
	return 0;
}

static void mtk_disp_postmask_unbind(struct device *dev, struct device *master,
				     void *data)
{
}

static const struct component_ops mtk_disp_postmask_component_ops = {
	.bind	= mtk_disp_postmask_bind,
	.unbind = mtk_disp_postmask_unbind,
};

static int mtk_disp_postmask_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_postmask *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get postmask clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap postmask\n");
		return PTR_ERR(priv->regs);
	}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_postmask_component_ops);
	if (ret)
		dev_err(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static int mtk_disp_postmask_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_postmask_component_ops);

	return 0;
}

static const struct of_device_id mtk_disp_postmask_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8192-disp-postmask"},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_postmask_driver_dt_match);

struct platform_driver mtk_disp_postmask_driver = {
	.probe		= mtk_disp_postmask_probe,
	.remove		= mtk_disp_postmask_remove,
	.driver		= {
		.name	= "mediatek-disp-postmask",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_postmask_driver_dt_match,
	},
};
