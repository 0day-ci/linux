// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include "ipa_gsi.h"
#include "ipa.h"
#include "ipa_dma.h"
#include "ipa_dma_private.h"
#include "ipa_gsi.h"
#include "ipa_trans.h"
#include "ipa_data.h"

/**
 * DOC: The IPA Smart Peripheral System Interface
 *
 * The Smart Peripheral System is a means to communicate over BAM pipes to
 * the IPA block. The Modem also uses BAM pipes to communicate with the IPA
 * core.
 *
 * Refer the GSI documentation, because BAM is a precursor to GSI and more or less
 * the same, conceptually (maybe, IDK, I have no docs to go through).
 *
 * Each channel here corresponds to 1 BAM pipe configured in BAM2BAM mode
 *
 * IPA cmds are transferred one at a time, each in one BAM transfer.
 */

/* Get and configure the BAM DMA channel */
int bam_channel_init_one(struct ipa_dma *bam,
			 const struct ipa_gsi_endpoint_data *data, bool command)
{
	struct dma_slave_config bam_config;
	u32 channel_id = data->channel_id;
	struct ipa_channel *channel = &bam->channel[channel_id];
	int ret;

	/*TODO: if (!bam_channel_data_valid(bam, data))
		return -EINVAL;*/

	channel->dma_subsys = bam;
	channel->dma_chan = dma_request_chan(bam->dev, data->channel_name);
	channel->toward_ipa = data->toward_ipa;
	channel->tlv_count = data->channel.tlv_count;
	channel->tre_count = data->channel.tre_count;
	if (IS_ERR(channel->dma_chan)) {
		dev_err(bam->dev, "failed to request BAM channel %s: %d\n",
				data->channel_name,
				(int) PTR_ERR(channel->dma_chan));
		return PTR_ERR(channel->dma_chan);
	}

	ret = ipa_channel_trans_init(bam, data->channel_id);
	if (ret)
		goto err_dma_chan_free;

	if (data->toward_ipa) {
		bam_config.direction = DMA_MEM_TO_DEV;
		bam_config.dst_maxburst = channel->tlv_count;
	} else {
		bam_config.direction = DMA_DEV_TO_MEM;
		bam_config.src_maxburst = channel->tlv_count;
	}

	dmaengine_slave_config(channel->dma_chan, &bam_config);

	if (command)
		ret = ipa_cmd_pool_init(channel, 256);

	if (!ret)
		return 0;

err_dma_chan_free:
	dma_release_channel(channel->dma_chan);
	return ret;
}

static void bam_channel_exit_one(struct ipa_channel *channel)
{
	if (channel->dma_chan) {
		dmaengine_terminate_sync(channel->dma_chan);
		dma_release_channel(channel->dma_chan);
	}
}

/* Get channels from BAM_DMA */
int bam_channel_init(struct ipa_dma *bam, u32 count,
		const struct ipa_gsi_endpoint_data *data)
{
	int ret = 0;
	u32 i;

	for (i = 0; i < count; ++i) {
		bool command = i == IPA_ENDPOINT_AP_COMMAND_TX;

		if (!data[i].channel_name || data[i].ee_id == GSI_EE_MODEM)
			continue;

		ret = bam_channel_init_one(bam, &data[i], command);
		if (ret)
			goto err_unwind;
	}

	return ret;

err_unwind:
	while (i--) {
		if (ipa_gsi_endpoint_data_empty(&data[i]))
			continue;

		bam_channel_exit_one(&bam->channel[i]);
	}
	return ret;
}

/* Inverse of bam_channel_init() */
void bam_channel_exit(struct ipa_dma *bam)
{
	u32 channel_id = BAM_CHANNEL_COUNT_MAX - 1;

	do
		bam_channel_exit_one(&bam->channel[channel_id]);
	while (channel_id--);
}

/* Inverse of bam_init() */
static void bam_exit(struct ipa_dma *bam)
{
	mutex_destroy(&bam->mutex);
	bam_channel_exit(bam);
}

/* Return the channel id associated with a given channel */
static u32 bam_channel_id(struct ipa_channel *channel)
{
	return channel - &channel->dma_subsys->channel[0];
}

static void
bam_channel_tx_update(struct ipa_channel *channel, struct ipa_trans *trans)
{
	u64 byte_count = trans->byte_count + trans->len;
	u64 trans_count = trans->trans_count + 1;

	byte_count -= channel->compl_byte_count;
	channel->compl_byte_count += byte_count;
	trans_count -= channel->compl_trans_count;
	channel->compl_trans_count += trans_count;

	ipa_gsi_channel_tx_completed(channel->dma_subsys, bam_channel_id(channel),
					   trans_count, byte_count);
}

static void
bam_channel_rx_update(struct ipa_channel *channel, struct ipa_trans *trans)
{
	/* FIXME */
	u64 byte_count = trans->byte_count + trans->len;

	channel->byte_count += byte_count;
	channel->trans_count++;
}

/* Consult hardware, move any newly completed transactions to completed list */
static void bam_channel_update(struct ipa_channel *channel)
{
	struct ipa_trans *trans;

	list_for_each_entry(trans, &channel->trans_info.pending, links) {
		enum dma_status trans_status =
				dma_async_is_tx_complete(channel->dma_chan,
					trans->cookie, NULL, NULL);
		if (trans_status == DMA_COMPLETE)
			break;
	}
	/* Get the transaction for the latest completed event.  Take a
	 * reference to keep it from completing before we give the events
	 * for this and previous transactions back to the hardware.
	 */
	refcount_inc(&trans->refcount);

	/* For RX channels, update each completed transaction with the number
	 * of bytes that were actually received.  For TX channels, report
	 * the number of transactions and bytes this completion represents
	 * up the network stack.
	 */
	if (channel->toward_ipa)
		bam_channel_tx_update(channel, trans);
	else
		bam_channel_rx_update(channel, trans);

	ipa_trans_move_complete(trans);

	ipa_trans_free(trans);
}

/**
 * bam_channel_poll_one() - Return a single completed transaction on a channel
 * @channel:	Channel to be polled
 *
 * Return:	Transaction pointer, or null if none are available
 *
 * This function returns the first entry on a channel's completed transaction
 * list.  If that list is empty, the hardware is consulted to determine
 * whether any new transactions have completed.  If so, they're moved to the
 * completed list and the new first entry is returned.  If there are no more
 * completed transactions, a null pointer is returned.
 */
static struct ipa_trans *bam_channel_poll_one(struct ipa_channel *channel)
{
	struct ipa_trans *trans;

	/* Get the first transaction from the completed list */
	trans = ipa_channel_trans_complete(channel);
	if (!trans) {
		bam_channel_update(channel);
		trans = ipa_channel_trans_complete(channel);
	}

	if (trans)
		ipa_trans_move_polled(trans);

	return trans;
}

/**
 * bam_channel_poll() - NAPI poll function for a channel
 * @napi:	NAPI structure for the channel
 * @budget:	Budget supplied by NAPI core
 *
 * Return:	Number of items polled (<= budget)
 *
 * Single transactions completed by hardware are polled until either
 * the budget is exhausted, or there are no more.  Each transaction
 * polled is passed to ipa_trans_complete(), to perform remaining
 * completion processing and retire/free the transaction.
 */
static int bam_channel_poll(struct napi_struct *napi, int budget)
{
	struct ipa_channel *channel;
	int count = 0;

	channel = container_of(napi, struct ipa_channel, napi);
	while (count < budget) {
		struct ipa_trans *trans;

		count++;
		trans = bam_channel_poll_one(channel);
		if (!trans)
			break;
		ipa_trans_complete(trans);
	}

	if (count < budget)
		napi_complete(&channel->napi);

	return count;
}

/* Setup function for a single channel */
static void bam_channel_setup_one(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	if (!channel->dma_subsys)
		return;	/* Ignore uninitialized channels */

	if (channel->toward_ipa) {
		netif_tx_napi_add(&bam->dummy_dev, &channel->napi,
				  bam_channel_poll, NAPI_POLL_WEIGHT);
	} else {
		netif_napi_add(&bam->dummy_dev, &channel->napi,
			       bam_channel_poll, NAPI_POLL_WEIGHT);
	}
	napi_enable(&channel->napi);
}

static void bam_channel_teardown_one(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	if (!channel->dma_subsys)
		return;		/* Ignore uninitialized channels */

	netif_napi_del(&channel->napi);
}

/* Setup function for channels */
static int bam_channel_setup(struct ipa_dma *bam)
{
	u32 channel_id = 0;
	int ret;

	mutex_lock(&bam->mutex);

	do
		bam_channel_setup_one(bam, channel_id);
	while (++channel_id < BAM_CHANNEL_COUNT_MAX);

	/* Make sure no channels were defined that hardware does not support */
	while (channel_id < BAM_CHANNEL_COUNT_MAX) {
		struct ipa_channel *channel = &bam->channel[channel_id++];

		if (!channel->dma_subsys)
			continue;	/* Ignore uninitialized channels */

		dev_err(bam->dev, "channel %u not supported by hardware\n",
			channel_id - 1);
		channel_id = BAM_CHANNEL_COUNT_MAX;
		goto err_unwind;
	}

	mutex_unlock(&bam->mutex);

	return 0;

err_unwind:
	while (channel_id--)
		bam_channel_teardown_one(bam, channel_id);

	mutex_unlock(&bam->mutex);

	return ret;
}

/* Inverse of bam_channel_setup() */
static void bam_channel_teardown(struct ipa_dma *bam)
{
	u32 channel_id;

	mutex_lock(&bam->mutex);

	channel_id = BAM_CHANNEL_COUNT_MAX;
	do
		bam_channel_teardown_one(bam, channel_id);
	while (channel_id--);

	mutex_unlock(&bam->mutex);
}

static int bam_setup(struct ipa_dma *bam)
{
	return bam_channel_setup(bam);
}

static void bam_teardown(struct ipa_dma *bam)
{
	bam_channel_teardown(bam);
}

static u32 bam_channel_tre_max(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	/* Hardware limit is channel->tre_count - 1 */
	return channel->tre_count - (channel->tlv_count - 1);
}

static u32 bam_channel_trans_tre_max(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	return channel->tlv_count;
}

static int bam_channel_start(struct ipa_dma *bam, u32 channel_id)
{
	return 0;
}

static int bam_channel_stop(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	return dmaengine_terminate_sync(channel->dma_chan);
}

static void bam_channel_reset(struct ipa_dma *bam, u32 channel_id, bool doorbell)
{
	bam_channel_stop(bam, channel_id);
}

static int bam_channel_suspend(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	return dmaengine_pause(channel->dma_chan);
}

static int bam_channel_resume(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	return dmaengine_resume(channel->dma_chan);
}

static void bam_suspend(struct ipa_dma *bam)
{
	/* No-op for now */
}

static void bam_resume(struct ipa_dma *bam)
{
	/* No-op for now */
}

static void bam_trans_callback(void *arg)
{
	ipa_trans_complete(arg);
}

static void bam_trans_commit(struct ipa_trans *trans, bool unused)
{
	struct ipa_channel *channel = &trans->dma_subsys->channel[trans->channel_id];
	enum ipa_cmd_opcode opcode = IPA_CMD_NONE;
	struct ipa_cmd_info *info;
	struct scatterlist *sg;
	u32 byte_count = 0;
	u32 i;
	enum dma_transfer_direction direction;

	if (channel->toward_ipa)
		direction = DMA_MEM_TO_DEV;
	else
		direction = DMA_DEV_TO_MEM;

	/* assert(trans->used > 0); */

	info = trans->info ? &trans->info[0] : NULL;
	for_each_sg(trans->sgl, sg, trans->used, i) {
		bool last_tre = i == trans->used - 1;
		dma_addr_t addr = sg_dma_address(sg);
		u32 len = sg_dma_len(sg);
		u32 dma_flags = 0;
		struct dma_async_tx_descriptor *desc;

		byte_count += len;
		if (info)
			opcode = info++->opcode;

		if (opcode != IPA_CMD_NONE) {
			len = opcode;
			dma_flags |= DMA_PREP_IMM_CMD;
		}

		if (last_tre)
			dma_flags |= DMA_PREP_INTERRUPT;

		desc = dmaengine_prep_slave_single(channel->dma_chan, addr, len,
				direction, dma_flags);

		if (last_tre) {
			desc->callback = bam_trans_callback;
			desc->callback_param = trans;
		}

		desc->cookie = dmaengine_submit(desc);

		if (last_tre)
			trans->cookie = desc->cookie;

		if (direction == DMA_DEV_TO_MEM)
			dmaengine_desc_attach_metadata(desc, &trans->len, sizeof(trans->len));
	}

	if (channel->toward_ipa) {
		/* We record TX bytes when they are sent */
		trans->len = byte_count;
		trans->trans_count = channel->trans_count;
		trans->byte_count = channel->byte_count;
		channel->trans_count++;
		channel->byte_count += byte_count;
	}

	ipa_trans_move_pending(trans);

	dma_async_issue_pending(channel->dma_chan);
}

/* Initialize the BAM DMA channels
 * Actual hw init is handled by the BAM_DMA driver
 */
int bam_init(struct ipa_dma *bam, struct platform_device *pdev,
		enum ipa_version version, u32 count,
		const struct ipa_gsi_endpoint_data *data)
{
	struct device *dev = &pdev->dev;
	int ret;

	bam->dev = dev;
	bam->version = version;
	bam->setup = bam_setup;
	bam->teardown = bam_teardown;
	bam->exit = bam_exit;
	bam->suspend = bam_suspend;
	bam->resume = bam_resume;
	bam->channel_tre_max = bam_channel_tre_max;
	bam->channel_trans_tre_max = bam_channel_trans_tre_max;
	bam->channel_start = bam_channel_start;
	bam->channel_stop = bam_channel_stop;
	bam->channel_reset = bam_channel_reset;
	bam->channel_suspend = bam_channel_suspend;
	bam->channel_resume = bam_channel_resume;
	bam->trans_commit = bam_trans_commit;

	init_dummy_netdev(&bam->dummy_dev);

	ret = bam_channel_init(bam, count, data);
	if (ret)
		return ret;

	mutex_init(&bam->mutex);

	return 0;
}
