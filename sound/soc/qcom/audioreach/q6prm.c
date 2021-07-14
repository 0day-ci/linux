// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, Linaro Limited

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/jiffies.h>
#include <linux/soc/qcom/apr.h>
#include <dt-bindings/soc/qcom,gpr.h>
#include <dt-bindings/sound/qcom,q6apm.h>
#include "audioreach.h"

#define Q6PRM_MAX_CLK_ID			104

#define Q6PRM_CLK(id) &(struct q6prm_clk) {		\
		.clk_id	= id,				\
		.afe_clk_id	= Q6PRM_##id,		\
		.name = #id,				\
		.attributes = LPASS_CLK_ATTRIBUTE_COUPLE_NO, \
		.rate = 19200000,			\
		.hw.init = &(struct clk_init_data) {	\
			.ops = &clk_q6prm_ops,		\
			.name = #id,			\
		},					\
	}

#define Q6PRM_VOTE_CLK(id, blkid) &(struct q6prm_clk) { \
		.clk_id	= id,				\
		.afe_clk_id = blkid,			\
		.hw.init = &(struct clk_init_data) {	\
			.ops = &clk_vote_q6prm_ops,	\
			.name = #id,			\
		},					\
	}

struct q6prm_clk {
	struct device *dev;
	int clk_id;
	int afe_clk_id;
	char *name;
	int attributes;
	int rate;
	uint32_t handle;
	struct clk_hw hw;
};
#define to_q6prm_clk(_hw) container_of(_hw, struct q6prm_clk, hw)

struct q6prm {
	struct device *dev;
	gpr_device_t *gdev;
	wait_queue_head_t wait;
	struct gpr_ibasic_rsp_result_t result;
	struct mutex lock;
	struct q6prm_clk **clks;
	int num_clks;
};

#define PRM_CMD_REQUEST_HW_RSC		0x0100100F
#define PRM_CMD_RSP_REQUEST_HW_RSC	0x02001002
#define PRM_CMD_RELEASE_HW_RSC		0x01001010
#define PRM_CMD_RSP_RELEASE_HW_RSC	0x02001003

#define PARAM_ID_RSC_HW_CORE		0x08001032
#define PARAM_ID_RSC_LPASS_CORE		0x0800102B
#define PARAM_ID_RSC_AUDIO_HW_CLK	0x0800102C

#define Q6PRM_LPASS_CLK_ID_WSA_CORE_MCLK			0x305
#define Q6PRM_LPASS_CLK_ID_WSA_CORE_NPL_MCLK			0x306

#define Q6PRM_LPASS_CLK_ID_VA_CORE_MCLK				0x307
#define Q6PRM_LPASS_CLK_ID_VA_CORE_2X_MCLK			0x308

#define Q6PRM_LPASS_CLK_ID_TX_CORE_MCLK				0x30c
#define Q6PRM_LPASS_CLK_ID_TX_CORE_NPL_MCLK			0x30d

#define Q6PRM_LPASS_CLK_ID_RX_CORE_MCLK				0x30e
#define Q6PRM_LPASS_CLK_ID_RX_CORE_NPL_MCLK			0x30f

#define Q6PRM_LPASS_CLK_SRC_INTERNAL	1
#define Q6PRM_LPASS_CLK_ROOT_DEFAULT	0
#define Q6PRM_HW_CORE_ID_LPASS		1
#define Q6PRM_HW_CORE_ID_DCODEC		2

struct prm_cmd_request_hw_core {
	struct apm_module_param_data param_data;
	uint32_t hw_clk_id;
} __packed;

struct prm_cmd_request_rsc {
	struct apm_module_param_data param_data;
	uint32_t num_clk_id;
	struct audio_hw_clk_cfg clock_ids[1];
} __packed;

struct prm_cmd_release_rsc {
	struct apm_module_param_data param_data;
	uint32_t num_clk_id;
	struct audio_hw_clk_cfg clock_ids[1];
} __packed;

static int q6prm_send_cmd_sync(struct q6prm *prm, struct gpr_pkt *pkt,
			uint32_t rsp_opcode)
{
	gpr_device_t *gdev = prm->gdev;
	struct gpr_hdr *hdr = &pkt->hdr;
	int rc;

	mutex_lock(&prm->lock);
	prm->result.opcode = 0;
	prm->result.status = 0;

	rc = gpr_send_pkt(prm->gdev, pkt);
	if (rc < 0)
		goto err;

	if (rsp_opcode)
		rc = wait_event_timeout(prm->wait,
					(prm->result.opcode == hdr->opcode) ||
					(prm->result.opcode == rsp_opcode),
					5 * HZ);
	else
		rc = wait_event_timeout(prm->wait,
					(prm->result.opcode == hdr->opcode),
					5 * HZ);

	if (!rc) {
		dev_err(&gdev->dev, "CMD timeout for [%x] opcode\n",
			hdr->opcode);
		rc = -ETIMEDOUT;
	} else if (prm->result.status > 0) {
		dev_err(&gdev->dev, "DSP returned error[%x] %x\n", hdr->opcode,
			prm->result.status);
		rc = -EINVAL;
	} else {
		dev_err(&gdev->dev, "DSP returned [%x]\n",
			prm->result.status);
		rc = 0;
	}

err:
	mutex_unlock(&prm->lock);
	return rc;
}

static int q6prm_set_hw_core_req(struct device *dev, uint32_t hw_block_id,   bool enable)
{
	struct prm_cmd_request_hw_core *req;
	struct apm_module_param_data *param_data;
	struct gpr_pkt *pkt;
	struct q6prm *prm = dev_get_drvdata(dev);
	gpr_device_t *gdev  = prm->gdev;
	void *p;
	int rc = 0;
	uint32_t opcode, rsp_opcode;

	if (enable) {
		opcode = PRM_CMD_REQUEST_HW_RSC;
		rsp_opcode = PRM_CMD_RSP_REQUEST_HW_RSC;
	} else {
		opcode = PRM_CMD_RELEASE_HW_RSC;
		rsp_opcode = PRM_CMD_RSP_RELEASE_HW_RSC;
	}

	p = audioreach_alloc_cmd_pkt(sizeof(*req), opcode, 0, gdev->svc.id,
				     GPR_PRM_MODULE_IID);
	if (IS_ERR(p))
		return -ENOMEM;

	pkt = p;
	req = p + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = &req->param_data;

	param_data->module_instance_id = GPR_PRM_MODULE_IID;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_RSC_HW_CORE;
	param_data->param_size = sizeof(*req) - APM_MODULE_PARAM_DATA_SIZE;

	req->hw_clk_id = hw_block_id;

	q6prm_send_cmd_sync(prm, pkt, rsp_opcode);

	kfree(pkt);

	return rc;
}

static int q6prm_set_lpass_clock(struct device *dev, int clk_id, int clk_attr,
				 int clk_root, unsigned int freq)
{
	struct prm_cmd_request_rsc *req;
	struct apm_module_param_data *param_data;
	struct gpr_pkt *pkt;
	struct q6prm *prm = dev_get_drvdata(dev);
	gpr_device_t *gdev  = prm->gdev;
	void *p;
	int rc = 0;

	p = audioreach_alloc_cmd_pkt(sizeof(*req), PRM_CMD_REQUEST_HW_RSC,
				     0, gdev->svc.id, GPR_PRM_MODULE_IID);
	if (IS_ERR(p))
		return -ENOMEM;

	pkt = p;
	req = p + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	param_data = &req->param_data;

	param_data->module_instance_id = GPR_PRM_MODULE_IID;
	param_data->error_code = 0;
	param_data->param_id = PARAM_ID_RSC_AUDIO_HW_CLK;
	param_data->param_size = sizeof(*req) - APM_MODULE_PARAM_DATA_SIZE;

	req->num_clk_id = 1;
	req->clock_ids[0].clock_id = clk_id;
	req->clock_ids[0].clock_freq = freq;
	req->clock_ids[0].clock_attri = clk_attr;
	req->clock_ids[0].clock_root = clk_root;

	q6prm_send_cmd_sync(prm, pkt, PRM_CMD_RSP_REQUEST_HW_RSC);

	kfree(pkt);

	return rc;
}

static int prm_callback(struct gpr_resp_pkt *data, void *priv, int op)
{
	gpr_device_t *gdev = priv;
	struct q6prm *prm = dev_get_drvdata(&gdev->dev);
	struct gpr_ibasic_rsp_result_t *result;
	struct gpr_hdr *hdr = &data->hdr;

	result = data->payload;

	switch (hdr->opcode) {
	case PRM_CMD_RSP_REQUEST_HW_RSC:
	case PRM_CMD_RSP_RELEASE_HW_RSC:
		prm->result.opcode = hdr->opcode;
		prm->result.status = result->status;
		wake_up(&prm->wait);
		break;
	default:
		break;
	}

	return 0;
}

static int clk_q6prm_prepare(struct clk_hw *hw)
{
	struct q6prm_clk *clk = to_q6prm_clk(hw);

	return q6prm_set_lpass_clock(clk->dev, clk->afe_clk_id, clk->attributes,
				     Q6PRM_LPASS_CLK_ROOT_DEFAULT, clk->rate);
}

static void clk_q6prm_unprepare(struct clk_hw *hw)
{
	struct q6prm_clk *clk = to_q6prm_clk(hw);

	q6prm_set_lpass_clock(clk->dev, clk->afe_clk_id, clk->attributes,
			      Q6PRM_LPASS_CLK_ROOT_DEFAULT, 0);
}

static int clk_q6prm_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct q6prm_clk *clk = to_q6prm_clk(hw);

	clk->rate = rate;

	return 0;
}

static unsigned long clk_q6prm_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct q6prm_clk *clk = to_q6prm_clk(hw);

	return clk->rate;
}

static long clk_q6prm_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	return rate;
}

static const struct clk_ops clk_q6prm_ops = {
	.prepare	= clk_q6prm_prepare,
	.unprepare	= clk_q6prm_unprepare,
	.set_rate	= clk_q6prm_set_rate,
	.round_rate	= clk_q6prm_round_rate,
	.recalc_rate	= clk_q6prm_recalc_rate,
};

static int clk_vote_q6prm_block(struct clk_hw *hw)
{
	struct q6prm_clk *clk = to_q6prm_clk(hw);

	return q6prm_set_hw_core_req(clk->dev, clk->afe_clk_id, true);
}

static void clk_unvote_q6prm_block(struct clk_hw *hw)
{
	struct q6prm_clk *clk = to_q6prm_clk(hw);

	q6prm_set_hw_core_req(clk->dev, clk->afe_clk_id, false);
}

static const struct clk_ops clk_vote_q6prm_ops = {
	.prepare	= clk_vote_q6prm_block,
	.unprepare	= clk_unvote_q6prm_block,
};

static struct q6prm_clk *q6prm_clks[Q6PRM_MAX_CLK_ID] = {
	[LPASS_CLK_ID_WSA_CORE_MCLK] = Q6PRM_CLK(LPASS_CLK_ID_WSA_CORE_MCLK),
	[LPASS_CLK_ID_WSA_CORE_NPL_MCLK] =
				Q6PRM_CLK(LPASS_CLK_ID_WSA_CORE_NPL_MCLK),
	[LPASS_CLK_ID_VA_CORE_MCLK] = Q6PRM_CLK(LPASS_CLK_ID_VA_CORE_MCLK),
	[LPASS_CLK_ID_TX_CORE_MCLK] = Q6PRM_CLK(LPASS_CLK_ID_TX_CORE_MCLK),
	[LPASS_CLK_ID_TX_CORE_NPL_MCLK] =
			Q6PRM_CLK(LPASS_CLK_ID_TX_CORE_NPL_MCLK),
	[LPASS_CLK_ID_RX_CORE_MCLK] = Q6PRM_CLK(LPASS_CLK_ID_RX_CORE_MCLK),
	[LPASS_CLK_ID_RX_CORE_NPL_MCLK] =
				Q6PRM_CLK(LPASS_CLK_ID_RX_CORE_NPL_MCLK),
	[LPASS_CLK_ID_VA_CORE_2X_MCLK] =
				Q6PRM_CLK(LPASS_CLK_ID_VA_CORE_2X_MCLK),
	[LPASS_HW_MACRO_VOTE] = Q6PRM_VOTE_CLK(LPASS_HW_MACRO_VOTE,
						Q6PRM_HW_CORE_ID_LPASS),
	[LPASS_HW_DCODEC_VOTE] = Q6PRM_VOTE_CLK(LPASS_HW_DCODEC_VOTE,
						Q6PRM_HW_CORE_ID_DCODEC),
};

static struct clk_hw *q6prm_of_clk_hw_get(struct of_phandle_args *clkspec,
					  void *data)
{
	struct q6prm *cc = data;
	unsigned int idx = clkspec->args[0];
	unsigned int attr = clkspec->args[1];

	if (idx >= cc->num_clks || attr > LPASS_CLK_ATTRIBUTE_COUPLE_DIVISOR) {
		dev_err(cc->dev, "Invalid clk specifier (%d, %d)\n", idx, attr);
		return ERR_PTR(-EINVAL);
	}

	if (cc->clks[idx]) {
		cc->clks[idx]->attributes = attr;
		return &cc->clks[idx]->hw;
	}

	return ERR_PTR(-ENOENT);
}

static int prm_probe(gpr_device_t *gdev)
{
	struct device *dev = &gdev->dev;
	struct q6prm *cc;
	int i, ret;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	cc->dev = dev;
	cc->gdev = gdev;
	mutex_init(&cc->lock);
	init_waitqueue_head(&cc->wait);
	cc->clks = &q6prm_clks[0];
	cc->num_clks = ARRAY_SIZE(q6prm_clks);
	for (i = 0; i < ARRAY_SIZE(q6prm_clks); i++) {
		if (!q6prm_clks[i])
			continue;

		q6prm_clks[i]->dev = dev;

		ret = devm_clk_hw_register(dev, &q6prm_clks[i]->hw);
		if (ret)
			return ret;
	}

	ret = of_clk_add_hw_provider(dev->of_node, q6prm_of_clk_hw_get, cc);
	if (ret)
		return ret;

	dev_set_drvdata(dev, cc);

	return 0;
}

static const struct of_device_id prm_device_id[]  = {
	{ .compatible = "qcom,q6prm" },
	{},
};
MODULE_DEVICE_TABLE(of, prm_device_id);

static gpr_driver_t prm_driver = {
	.probe = prm_probe,
	.gpr_callback = prm_callback,
	.driver = {
		.name = "qcom-prm",
		.of_match_table = of_match_ptr(prm_device_id),
	},
};

module_gpr_driver(prm_driver);
MODULE_DESCRIPTION("Audio Process Manager");
MODULE_LICENSE("GPL v2");
