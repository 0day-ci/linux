// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Corporation. All rights reserved.
 * Author: Allen-KH Cheng <allen-kh.cheng@mediatek.com>
 */

#include <linux/firmware/mediatek/mtk-adsp-ipc.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

int adsp_ipc_send(struct mtk_adsp_ipc *ipc, unsigned int idx, uint32_t op)
{
	struct mtk_adsp_chan *dsp_chan = &ipc->chans[idx];
	struct adsp_mbox_ch_info *ch_info = dsp_chan->ch->con_priv;
	int ret;

	if (idx >= MTK_ADSP_MBOX_NUM)
		return -EINVAL;

	ch_info->ipc_op_val = op;
	ret = mbox_send_message(dsp_chan->ch, NULL);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(adsp_ipc_send);

static void adsp_ipc_recv(struct mbox_client *c, void *msg)
{
	struct mtk_adsp_chan *chan = container_of(c, struct mtk_adsp_chan, cl);

	if (chan->idx == MTK_ADSP_MBOX_REPLY)
		chan->ipc->ops->handle_reply(chan->ipc);
	else
		chan->ipc->ops->handle_request(chan->ipc);
}

static int mtk_adsp_ipc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_adsp_ipc *dsp_ipc;
	struct mtk_adsp_chan *dsp_chan;
	struct mbox_client *cl;
	char *chan_name;
	int ret;
	int i, j;

	device_set_of_node_from_dev(&pdev->dev, pdev->dev.parent);

	dsp_ipc = devm_kzalloc(dev, sizeof(*dsp_ipc), GFP_KERNEL);
	if (!dsp_ipc)
		return -ENOMEM;

	for (i = 0; i < MTK_ADSP_MBOX_NUM; i++) {
		chan_name = kasprintf(GFP_KERNEL, "mbox%d", i);
		if (!chan_name)
			return -ENOMEM;

		dsp_chan = &dsp_ipc->chans[i];
		cl = &dsp_chan->cl;
		cl->dev = dev->parent;
		cl->tx_block = false;
		cl->knows_txdone = false;
		cl->tx_prepare = NULL;
		cl->rx_callback = adsp_ipc_recv;

		dsp_chan->ipc = dsp_ipc;
		dsp_chan->idx = i;
		dsp_chan->ch = mbox_request_channel_byname(cl, chan_name);
		if (IS_ERR(dsp_chan->ch)) {
			ret = PTR_ERR(dsp_chan->ch);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to request mbox chan %d ret %d\n",
					i, ret);
			goto out;
		}

		dev_dbg(dev, "request mbox chan %s\n", chan_name);
		kfree(chan_name);
	}

	dsp_ipc->dev = dev;
	dev_set_drvdata(dev, dsp_ipc);
	dev_dbg(dev, "MTK ADSP IPC initialized\n");

	return 0;

out:
	kfree(chan_name);
	for (j = 0; j < i; j++) {
		dsp_chan = &dsp_ipc->chans[j];
		mbox_free_channel(dsp_chan->ch);
	}

	return ret;
}

static int mtk_adsp_remove(struct platform_device *pdev)
{
	struct mtk_adsp_chan *dsp_chan;
	struct mtk_adsp_ipc *dsp_ipc;
	int i;

	dsp_ipc = dev_get_drvdata(&pdev->dev);

	for (i = 0; i < MTK_ADSP_MBOX_NUM; i++) {
		dsp_chan = &dsp_ipc->chans[i];
		mbox_free_channel(dsp_chan->ch);
	}

	return 0;
}

static struct platform_driver mtk_adsp_ipc_driver = {
	.driver = {
		.name = "mtk-adsp-ipc",
	},
	.probe = mtk_adsp_ipc_probe,
	.remove = mtk_adsp_remove,
};
builtin_platform_driver(mtk_adsp_ipc_driver);

MODULE_AUTHOR("Allen-KH Cheng <allen-kh.cheng@mediatek.com>");
MODULE_DESCRIPTION("MTK ADSP IPC protocol driver");
MODULE_LICENSE("GPL v2");
