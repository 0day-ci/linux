// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <dt-bindings/soc/qcom,gpr.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/soc/qcom/apr.h>
#include <linux/wait.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include "audioreach.h"
#include "q6apm.h"

/* Graph Management */
struct apm_graph_mgmt_cmd {
	struct apm_module_param_data param_data;
	uint32_t num_sub_graphs;
	uint32_t sub_graph_id_list[0];
} __packed;

#define APM_GRAPH_MGMT_PSIZE(n) ALIGN(sizeof(struct apm_graph_mgmt_cmd) + \
				      n * sizeof(uint32_t), 8)

int q6apm_send_cmd(struct q6apm *apm, struct gpr_pkt *pkt)
{
	int rc;

	mutex_lock(&apm->cmd_lock);
	rc = gpr_send_pkt(apm->gdev, pkt);
	mutex_unlock(&apm->cmd_lock);

	return rc;
}

int q6apm_send_cmd_sync(struct q6apm *apm, struct gpr_pkt *pkt,
			uint32_t rsp_opcode)
{
	gpr_device_t *gdev = apm->gdev;
	struct gpr_hdr *hdr = &pkt->hdr;
	int rc;

	mutex_lock(&apm->cmd_lock);
	apm->result.opcode = 0;
	apm->result.status = 0;

	rc = gpr_send_pkt(apm->gdev, pkt);
	if (rc < 0)
		goto err;

	if (rsp_opcode)
		rc = wait_event_timeout(apm->wait,
					(apm->result.opcode == hdr->opcode) ||
					(apm->result.opcode == rsp_opcode),
					5 * HZ);
	else
		rc = wait_event_timeout(apm->wait,
					(apm->result.opcode == hdr->opcode),
					5 * HZ);

	if (!rc) {
		dev_err(&gdev->dev, "CMD timeout for [%x] opcode\n",
			hdr->opcode);
		rc = -ETIMEDOUT;
	} else if (apm->result.status > 0) {
		dev_err(&gdev->dev, "DSP returned error[%x] %x\n", hdr->opcode,
			apm->result.status);
		rc = -EINVAL;
	} else {
		dev_err(&gdev->dev, "DSP returned [%x]\n",
			apm->result.status);
		rc = 0;
	}

err:
	mutex_unlock(&apm->cmd_lock);
	return rc;
}

static struct audioreach_graph *q6apm_get_audioreach_graph(struct q6apm *apm,
						      uint32_t graph_id)
{
	struct audioreach_graph *graph = NULL;
	struct audioreach_graph_info *info;
	unsigned long flags;

	spin_lock_irqsave(&apm->lock, flags);
	graph = idr_find(&apm->graph_idr, graph_id);
	spin_unlock_irqrestore(&apm->lock, flags);

	if (graph) {
		kref_get(&graph->refcount);
		return graph;
	}

	info = idr_find(&apm->graph_info_idr, graph_id);

	if (!info)
		return ERR_PTR(-ENODEV);

	graph = kzalloc(sizeof(*graph), GFP_KERNEL);
	if (!graph)
		return ERR_PTR(-ENOMEM);

	graph->apm = apm;
	graph->info = info;
	graph->id = graph_id;

	/* Assuming Linear Graphs only for now! */
	graph->graph = audioreach_alloc_graph_pkt(apm, &info->sg_list, graph_id);
	if (IS_ERR(graph->graph))
		return ERR_PTR(-ENOMEM);

	spin_lock(&apm->lock);
	idr_alloc(&apm->graph_idr, graph, graph_id,
		  graph_id + 1, GFP_ATOMIC);
	spin_unlock(&apm->lock);

	kref_init(&graph->refcount);

	q6apm_send_cmd_sync(apm, graph->graph, 0);

	return graph;
}

static int audioreach_graph_mgmt_cmd(struct audioreach_graph *graph,
				      uint32_t opcode)
{
	struct gpr_pkt *pkt;
	void *p;
	int i = 0, rc, payload_size;
	struct q6apm *apm = graph->apm;
	struct audioreach_graph_info *info = graph->info;
	int num_sub_graphs = info->num_sub_graphs;
	struct apm_graph_mgmt_cmd *mgmt_cmd;
	struct apm_module_param_data *param_data;
	struct audioreach_sub_graph *sg;

	payload_size = APM_GRAPH_MGMT_PSIZE(num_sub_graphs);

	p = audioreach_alloc_apm_cmd_pkt(payload_size, opcode, 0);
	if (IS_ERR(p))
		return -ENOMEM;

	pkt = p;
	p = p + GPR_HDR_SIZE + APM_CMD_HDR_SIZE;

	mgmt_cmd = p;
	mgmt_cmd->num_sub_graphs = num_sub_graphs;

	param_data = &mgmt_cmd->param_data;
	param_data->module_instance_id = APM_MODULE_INSTANCE_ID;
	param_data->param_id = APM_PARAM_ID_SUB_GRAPH_LIST;
	param_data->param_size = payload_size - APM_MODULE_PARAM_DATA_SIZE;

	list_for_each_entry(sg, &info->sg_list, node) {
		mgmt_cmd->sub_graph_id_list[i++] = sg->sub_graph_id;
	}

	rc = q6apm_send_cmd_sync(apm, pkt, 0);

	kfree(pkt);

	return rc;
}

static void q6apm_put_audioreach_graph(struct kref *ref)
{
	struct audioreach_graph *graph;
	struct q6apm *apm;
	unsigned long flags;

	graph = container_of(ref, struct audioreach_graph, refcount);
	apm = graph->apm;

	audioreach_graph_mgmt_cmd(graph, APM_CMD_GRAPH_CLOSE);

	spin_lock_irqsave(&apm->lock, flags);
	graph = idr_remove(&apm->graph_idr, graph->id);
	spin_unlock_irqrestore(&apm->lock, flags);

	kfree(graph->graph);
	kfree(graph);
}

static bool q6apm_get_apm_state(struct q6apm *apm)
{
	struct gpr_pkt *pkt;

	pkt = audioreach_alloc_apm_cmd_pkt(0, APM_CMD_GET_SPF_STATE, 0);
	if (IS_ERR(pkt))
		return -ENOMEM;

	q6apm_send_cmd_sync(apm, pkt, APM_CMD_RSP_GET_SPF_STATE);

	kfree(pkt);

	return !apm->state ? false : true;
}

static struct audioreach_module *__q6apm_find_module_by_mid(struct q6apm *apm,
					     struct audioreach_graph_info *info,
					     uint32_t mid)
{
	struct audioreach_sub_graph *sgs;
	struct audioreach_container *container;
	struct audioreach_module *module;

	list_for_each_entry(sgs, &info->sg_list, node) {
		list_for_each_entry(container, &sgs->container_list, node) {
			list_for_each_entry(module, &container->modules_list, node) {
				if (mid == module->module_id)
					return module;
			}
		}
	}

	return NULL;
}

static struct audioreach_module *q6apm_graph_get_last_module(struct q6apm *apm,
							     u32 sgid)
{
	struct audioreach_sub_graph *sg;
	struct audioreach_module *module;
	struct audioreach_container *container;

	spin_lock(&apm->lock);
	sg = idr_find(&apm->sub_graphs_idr, sgid);
	spin_unlock(&apm->lock);
	if (!sg)
		return NULL;

	container = list_last_entry(&sg->container_list, struct audioreach_container, node);
	module = audioreach_get_container_last_module(container);

	return module;
}

static struct audioreach_module *q6apm_graph_get_first_module(struct q6apm *apm,
							      u32 sgid)
{
	struct audioreach_sub_graph *sg;
	struct audioreach_module *module;
	struct audioreach_container *container;

	spin_lock(&apm->lock);
	sg = idr_find(&apm->sub_graphs_idr, sgid);
	spin_unlock(&apm->lock);
	if (!sg)
		return NULL;

	container = list_first_entry(&sg->container_list, struct audioreach_container, node);
	module = audioreach_get_container_first_module(container);

	return module;
}

bool q6apm_is_sub_graphs_connected(struct q6apm *apm, u32 src_sgid, u32 dst_sgid)
{
	struct audioreach_module *module;
	u32 iid;

	module = q6apm_graph_get_last_module(apm, src_sgid);
	if (!module)
		return false;

	iid = module->instance_id;
	module = q6apm_graph_get_first_module(apm, dst_sgid);
	if (!module)
		return false;

	if (module->src_mod_inst_id == iid)
		return true;

	return false;
}

int q6apm_connect_sub_graphs(struct q6apm *apm, u32 src_sgid,
			     u32 dst_sgid, bool connect)
{

	struct audioreach_module *module;
	u32 iid;

	if (connect) {
		module = q6apm_graph_get_last_module(apm, src_sgid);
		if (!module)
			return -ENODEV;

		iid = module->instance_id;
	} else {
		iid = 0;
	}

	module = q6apm_graph_get_first_module(apm, dst_sgid);
	if (!module)
		return -ENODEV;

	/* set src module in dst subgraph first module */
	module->src_mod_inst_id = iid;

	return 0;
}

int q6apm_graph_get_rx_shmem_module_iid(struct q6apm_graph *graph)
{
	struct audioreach_module *module;

	module = q6apm_find_module_by_mid(graph, MODULE_ID_WR_SHARED_MEM_EP);
	if (!module)
		return -ENODEV;

	return module->instance_id;

}
EXPORT_SYMBOL_GPL(q6apm_graph_get_rx_shmem_module_iid);

static int graph_callback(struct gpr_resp_pkt *data, void *priv, int op)
{
	struct q6apm_graph *graph = priv;
	struct device *dev = graph->dev;
	struct gpr_hdr *hdr = &data->hdr;
	struct gpr_ibasic_rsp_result_t *result;
	int ret = -EINVAL;
	uint32_t client_event = 0;

	result = data->payload;

	switch (hdr->opcode) {
	case DATA_CMD_RSP_WR_SH_MEM_EP_DATA_BUFFER_DONE_V2:
		client_event = APM_CLIENT_EVENT_DATA_WRITE_DONE;
		if (graph) {
			struct data_cmd_rsp_wr_sh_mem_ep_data_buffer_done_v2 *done;
			phys_addr_t phys;
			unsigned long flags;
			int token = hdr->token & APM_WRITE_TOKEN_MASK;

			spin_lock_irqsave(&graph->lock, flags);

			done = data->payload;
			phys = graph->rx_data.buf[token].phys;

			if (lower_32_bits(phys) != done->buf_addr_lsw ||
			    upper_32_bits(phys) != done->buf_addr_msw) {
				dev_err(dev, "WR BUFF Expected Token %d addr %pa\n", token, &phys);
				ret = -EINVAL;
			} else {
				ret = 0;
				graph->result.opcode = hdr->opcode;
				graph->result.status = done->status;
			}
			spin_unlock_irqrestore(&graph->lock, flags);
		} else {
			dev_err(dev, "No SH Mem module found\n");
			ret = -EINVAL;
		}
		if (graph->cb)
			graph->cb(client_event, hdr->token, data->payload,
				  graph->priv);

		break;
	case APM_CMD_RSP_SHARED_MEM_MAP_REGIONS:
		if (graph) {
			struct apm_cmd_rsp_shared_mem_map_regions *rsp;

			graph->result.opcode = hdr->opcode;
			graph->result.status = 0;
			rsp = data->payload;

			if (hdr->token == SNDRV_PCM_STREAM_PLAYBACK)
				graph->rx_data.mem_map_handle = rsp->mem_map_handle;
			else
				graph->tx_data.mem_map_handle = rsp->mem_map_handle;

			wake_up(&graph->cmd_wait);
			ret = 0;
		}
		break;
	case DATA_CMD_RSP_RD_SH_MEM_EP_DATA_BUFFER_V2:
		if (graph) {
			struct data_cmd_rsp_rd_sh_mem_ep_data_buffer_done_v2 *done;
			unsigned long flags;
			phys_addr_t phys;

			done = data->payload;
			spin_lock_irqsave(&graph->lock, flags);
			phys = graph->tx_data.buf[hdr->token].phys;
			if (upper_32_bits(phys) != done->buf_addr_msw ||
			    lower_32_bits(phys) != done->buf_addr_lsw) {
				dev_err(dev, "RD BUFF Expected addr %pa %08x-%08x\n",
					&phys,
					done->buf_addr_lsw,
					done->buf_addr_msw);
				ret = -EINVAL;
			} else {
				ret = 0;
			}
			spin_unlock_irqrestore(&graph->lock, flags);
			client_event = APM_CLIENT_EVENT_DATA_READ_DONE;
			wake_up(&graph->cmd_wait);

			if (graph->cb)
				graph->cb(client_event, hdr->token, data->payload,
					  graph->priv);
		}

		break;
	case DATA_CMD_WR_SH_MEM_EP_EOS_RENDERED:
		break;
	case GPR_BASIC_RSP_RESULT:
		switch (result->opcode) {
		case APM_CMD_SHARED_MEM_UNMAP_REGIONS:
			if (graph) {
				graph->result.opcode = result->opcode;
				graph->result.status = 0;
				if (hdr->token == SNDRV_PCM_STREAM_PLAYBACK)
					graph->rx_data.mem_map_handle = 0;
				else
					graph->tx_data.mem_map_handle = 0;

				wake_up(&graph->cmd_wait);
				ret = 0;
			}

		break;
		case APM_CMD_SHARED_MEM_MAP_REGIONS:
		case DATA_CMD_WR_SH_MEM_EP_MEDIA_FORMAT:
		case APM_CMD_SET_CFG:
			graph->result.opcode = result->opcode;
			graph->result.status = result->status;
			if (result->status) {
				dev_err(dev,
					"Error (%d) Processing 0x%08x cmd\n",
					result->status, result->opcode);
				ret = -EINVAL;
			} else {
				ret = 0;
			}
			wake_up(&graph->cmd_wait);
			if (graph->cb)
				graph->cb(client_event, hdr->token, data->payload,
					  graph->priv);

		}
		break;
	}

	return ret;
}

struct q6apm_graph *q6apm_graph_open(struct device *dev, q6apm_cb cb,
				     void *priv, int graph_id)
{
	struct q6apm *apm = dev_get_drvdata(dev->parent);
	struct q6apm_graph *graph;
	struct audioreach_graph *ar_graph;
	int ret;

	dev_err(dev, "%s :graph id %d\n", __func__, graph_id);
	ar_graph = q6apm_get_audioreach_graph(apm, graph_id);
	if (IS_ERR(ar_graph)) {
		dev_err(dev, "No graph found with id %d\n", graph_id);
		return ERR_CAST(ar_graph);
	}

	graph = kzalloc(sizeof(*graph), GFP_KERNEL);
	if (!graph) {
		ret = -ENOMEM;
		goto err;
	}

	graph->apm = apm;
	graph->priv = priv;
	graph->cb = cb;
	graph->info = ar_graph->info;
	graph->ar_graph = ar_graph;
	graph->id = ar_graph->id;
	graph->dev = dev;

	spin_lock_init(&graph->lock);
	init_waitqueue_head(&graph->cmd_wait);
	mutex_init(&graph->cmd_lock);

	graph->port = gpr_alloc_port(apm->gdev, dev, graph_callback, graph);
	if (!graph->port) {
		kfree(graph);
		ret = -ENOMEM;
		goto err;
	}

	dev_dbg(dev, "%s: GRAPH-DEBUG Opening graph id %d with port id 0x%08x\n", __func__,
		graph_id, graph->port->id);

	return graph;
err:
	kref_put(&ar_graph->refcount, q6apm_put_audioreach_graph);
	return ERR_PTR(ret);
}

int q6apm_graph_close(struct q6apm_graph *graph)
{
	struct audioreach_graph *ar_graph = graph->ar_graph;

	gpr_free_port(graph->port);
	graph->port = NULL;
	kref_put(&ar_graph->refcount, q6apm_put_audioreach_graph);
	kfree(graph);

	return 0;
}

int q6apm_graph_prepare(struct q6apm_graph *graph)
{
	return audioreach_graph_mgmt_cmd(graph->ar_graph,
					  APM_CMD_GRAPH_PREPARE);
}

int q6apm_graph_start(struct q6apm_graph *graph)
{
	struct audioreach_graph *ar_graph = graph->ar_graph;
	int ret = 0;

	if (ar_graph->start_count == 0)
		ret = audioreach_graph_mgmt_cmd(ar_graph, APM_CMD_GRAPH_START);

	ar_graph->start_count++;

	return ret;
}

int q6apm_graph_stop(struct q6apm_graph *graph)
{
	struct audioreach_graph *ar_graph = graph->ar_graph;

	if (--ar_graph->start_count > 0)
		return 0;

	return audioreach_graph_mgmt_cmd(ar_graph, APM_CMD_GRAPH_STOP);
}

int q6apm_graph_flush(struct q6apm_graph *graph)
{
	return audioreach_graph_mgmt_cmd(graph->ar_graph, APM_CMD_GRAPH_FLUSH);
}

static int q6apm_audio_probe(struct snd_soc_component *component)
{
	return 0;
}

static void q6apm_audio_remove(struct snd_soc_component *component)
{
}

#define APM_AUDIO_DRV_NAME "q6apm-audio"

static const struct snd_soc_component_driver q6apm_audio_component = {
	.name		= APM_AUDIO_DRV_NAME,
	.probe		= q6apm_audio_probe,
	.remove		= q6apm_audio_remove,
};

static int apm_probe(gpr_device_t *gdev)
{
	struct device *dev = &gdev->dev;
	struct q6apm *apm;

	apm = devm_kzalloc(dev, sizeof(*apm), GFP_KERNEL);
	if (!apm)
		return -ENOMEM;

	dev_set_drvdata(dev, apm);

	mutex_init(&apm->cmd_lock);
	apm->dev = dev;
	apm->gdev = gdev;
	init_waitqueue_head(&apm->wait);

	idr_init(&apm->graph_idr);
	idr_init(&apm->graph_info_idr);
	idr_init(&apm->sub_graphs_idr);
	idr_init(&apm->containers_idr);

	idr_init(&apm->modules_idr);
	spin_lock_init(&apm->lock);

	q6apm_get_apm_state(apm);

	devm_snd_soc_register_component(dev, &q6apm_audio_component, NULL, 0);

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static int apm_exit(gpr_device_t *gdev)
{
	struct q6apm *apm = dev_get_drvdata(&gdev->dev);

	kfree(apm);

	return 0;
}

struct audioreach_module *q6apm_find_module_by_mid(struct q6apm_graph *graph,
						    uint32_t mid)
{
	struct audioreach_graph_info *info = graph->info;
	struct q6apm *apm = graph->apm;

	return __q6apm_find_module_by_mid(apm, info, mid);

}

struct audioreach_module *q6apm_find_module(struct q6apm *apm, uint32_t iid)
{
	struct audioreach_module *module;
	unsigned long flags;

	spin_lock_irqsave(&apm->lock, flags);
	module = idr_find(&apm->modules_idr, iid);
	spin_unlock_irqrestore(&apm->lock, flags);

	return module;
}

static int apm_callback(struct gpr_resp_pkt *data, void *priv, int op)
{
	gpr_device_t *gdev = priv;
	struct q6apm *apm = dev_get_drvdata(&gdev->dev);
	struct device *dev = &gdev->dev;
	struct gpr_ibasic_rsp_result_t *result;
	struct gpr_hdr *hdr = &data->hdr;
	int ret = -EINVAL;

	result = data->payload;

	switch (hdr->opcode) {
	case APM_CMD_RSP_GET_SPF_STATE:
		apm->result.opcode = hdr->opcode;
		apm->result.status = 0;
		/* First word of result it state */
		apm->state = result->opcode;
		wake_up(&apm->wait);
		break;
	case GPR_BASIC_RSP_RESULT:
		switch (result->opcode) {
		case APM_CMD_GRAPH_START:
		case APM_CMD_GRAPH_OPEN:
		case APM_CMD_GRAPH_PREPARE:
		case APM_CMD_GRAPH_CLOSE:
		case APM_CMD_GRAPH_FLUSH:
		case APM_CMD_GRAPH_STOP:
		case APM_CMD_SET_CFG:
			apm->result.opcode = result->opcode;
			apm->result.status = result->status;
			if (result->status) {
				dev_err(dev,
					"Error (%d) Processing 0x%08x cmd\n",
					result->status, result->opcode);
				ret = -EINVAL;
			} else {
				ret = 0;
			}
			wake_up(&apm->wait);

		}
		break;
	}

	return ret;
}

static const struct of_device_id apm_device_id[]  = {
	{ .compatible = "qcom,q6apm" },
	{},
};
MODULE_DEVICE_TABLE(of, apm_device_id);

static gpr_driver_t apm_driver = {
	.probe = apm_probe,
	.remove = apm_exit,
	.gpr_callback = apm_callback,
	.driver = {
		.name = "qcom-apm",
		.of_match_table = of_match_ptr(apm_device_id),
	},
};

module_gpr_driver(apm_driver);
MODULE_DESCRIPTION("Audio Process Manager");
MODULE_LICENSE("GPL v2");
