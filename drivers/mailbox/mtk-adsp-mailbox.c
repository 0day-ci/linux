// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Corporation. All rights reserved.
 * Author: Allen-KH Cheng <allen-kh.cheng@mediatek.com>
 */

#include <linux/firmware/mediatek/mtk-adsp-ipc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>

/* adsp mbox register offset */
#define MTK_ADSP_MBOX_IN_CMD 0x00
#define MTK_ADSP_MBOX_IN_CMD_CLR 0x04
#define MTK_ADSP_MBOX_OUT_CMD 0x1c
#define MTK_ADSP_MBOX_OUT_CMD_CLR 0x20
#define MTK_ADSP_MBOX_IN_MSG0 0x08
#define MTK_ADSP_MBOX_IN_MSG1 0x0C
#define MTK_ADSP_MBOX_OUT_MSG0 0x24
#define MTK_ADSP_MBOX_OUT_MSG1 0x28

struct mtk_adsp_mbox_priv {
	struct device *dev;
	struct mbox_controller mbox;
	void __iomem *va_mboxreg;
};

static irqreturn_t mtk_adsp_ipc_irq_handler(int irq, void *data)
{
	struct mbox_chan *ch = (struct mbox_chan *)data;
	struct adsp_mbox_ch_info *ch_info = ch->con_priv;
	void __iomem *reg = ch_info->va_reg;
	u32 op = readl(reg + MTK_ADSP_MBOX_OUT_CMD);

	writel(op, reg + MTK_ADSP_MBOX_OUT_CMD_CLR);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t mtk_adsp_ipc_handler(int irq, void *data)
{
	struct mbox_chan *ch = (struct mbox_chan *)data;
	struct adsp_mbox_ch_info *ch_info = ch->con_priv;

	mbox_chan_received_data(ch, ch_info);

	return IRQ_HANDLED;
}

static struct mbox_chan *mtk_adsp_mbox_xlate(struct mbox_controller *mbox,
					     const struct of_phandle_args *sp)
{
	return &mbox->chans[sp->args[0]];
}

static int mtk_adsp_mbox_startup(struct mbox_chan *chan)
{
	struct adsp_mbox_ch_info *ch_info = chan->con_priv;
	void __iomem *reg = ch_info->va_reg;

	/* Clear DSP mbox command */
	writel(0xFFFFFFFF, reg + MTK_ADSP_MBOX_IN_CMD_CLR);
	writel(0xFFFFFFFF, reg + MTK_ADSP_MBOX_OUT_CMD_CLR);

	return 0;
}

static void mtk_adsp_mbox_shutdown(struct mbox_chan *chan)
{
	chan->con_priv = NULL;
}

static int mtk_adsp_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct adsp_mbox_ch_info *ch_info = chan->con_priv;
	void __iomem *reg = ch_info->va_reg;

	spin_lock(&ch_info->lock);
	writel(ch_info->ipc_op_val, reg + MTK_ADSP_MBOX_IN_CMD);
	spin_unlock(&ch_info->lock);

	return 0;
}

static bool mtk_adsp_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct adsp_mbox_ch_info *ch_info = chan->con_priv;
	void __iomem *reg = ch_info->va_reg;
	u32 op = readl(reg + MTK_ADSP_MBOX_IN_CMD);

	return (op == 0) ? true : false;
}

static const struct mbox_chan_ops adsp_mbox_chan_ops = {
	.send_data	= mtk_adsp_mbox_send_data,
	.startup	= mtk_adsp_mbox_startup,
	.shutdown	= mtk_adsp_mbox_shutdown,
	.last_tx_done	= mtk_adsp_mbox_last_tx_done,
};

static int mtk_adsp_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mbox_controller *mbox;
	struct mtk_adsp_mbox_priv *priv;
	struct resource *res;
	struct adsp_mbox_ch_info *ch_info;
	u32 size;
	int ret;
	int irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mbox = &priv->mbox;
	mbox->dev = dev;
	mbox->ops = &adsp_mbox_chan_ops;
	mbox->txdone_irq = false;
	mbox->txdone_poll = true;
	mbox->of_xlate = mtk_adsp_mbox_xlate;
	mbox->num_chans = 1;
	mbox->chans = devm_kzalloc(mbox->dev, sizeof(*mbox->chans), GFP_KERNEL);
	if (!mbox->chans)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "no adsp mbox register resource\n");
		return -ENXIO;
	}

	size = resource_size(res);
	priv->va_mboxreg = devm_ioremap(dev, (phys_addr_t)res->start, size);
	if (IS_ERR(priv->va_mboxreg))
		return PTR_ERR(priv->va_mboxreg);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq,
					mtk_adsp_ipc_irq_handler, mtk_adsp_ipc_handler,
					IRQF_TRIGGER_NONE, dev_name(dev),
					mbox->chans);
	if (ret < 0)
		return ret;

	/* set adsp mbox channel info */
	ch_info = devm_kzalloc(mbox->dev, sizeof(*ch_info), GFP_KERNEL);
	if (!ch_info)
		return -ENOMEM;

	spin_lock_init(&ch_info->lock);
	ch_info->va_reg = priv->va_mboxreg;
	mbox->chans->con_priv = ch_info;
	platform_set_drvdata(pdev, priv);
	ret = devm_mbox_controller_register(dev, &priv->mbox);
	if (ret < 0)
		dev_err(dev, "error: failed to register mailbox:%d\n", ret);

	return ret;
}

static const struct of_device_id mtk_adsp_mbox_of_match[] = {
	{ .compatible = "mediatek,mt8195-adsp-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_adsp_mbox_of_match);

static struct platform_driver mtk_adsp_ipc_mbox_driver = {
	.probe		= mtk_adsp_mbox_probe,
	.driver = {
		.name	= "mtk_adsp_mbox",
		.of_match_table = mtk_adsp_mbox_of_match,
	},
};
module_platform_driver(mtk_adsp_ipc_mbox_driver);

MODULE_AUTHOR("Allen-KH Cheng <Allen-KH.Cheng@mediatek.com>");
MODULE_DESCRIPTION("MTK ADSP mailbox IPC driver");
MODULE_LICENSE("GPL v2");
