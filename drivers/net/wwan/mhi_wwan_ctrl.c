// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Linaro Ltd <loic.poulain@linaro.org> */
#include <linux/kernel.h>
#include <linux/mhi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/wwan.h>

/* MHI wwan flags */
#define MHI_WWAN_DL_CAP		BIT(0)
#define MHI_WWAN_UL_CAP		BIT(1)
#define MHI_WWAN_STARTED	BIT(2)

#define MHI_WWAN_MAX_MTU	0x8000

struct mhi_wwan_dev {
	/* Lower level is a mhi dev, upper level is a wwan port */
	struct mhi_device *mhi_dev;
	struct wwan_port *wwan_port;

	/* State and capabilities */
	unsigned long flags;
	size_t mtu;

	/* Protect against concurrent TX and TX-completion (bh) */
	spinlock_t tx_lock;

	struct work_struct rx_refill;
	atomic_t rx_budget;
};

static bool mhi_wwan_ctrl_refill_needed(struct mhi_wwan_dev *mhiwwan)
{
	if (!test_bit(MHI_WWAN_STARTED, &mhiwwan->flags))
		return false;

	if (!test_bit(MHI_WWAN_DL_CAP, &mhiwwan->flags))
		return false;

	if (!atomic_read(&mhiwwan->rx_budget))
		return false;

	return true;
}

void __mhi_skb_destructor(struct sk_buff *skb)
{
	struct mhi_wwan_dev *mhiwwan = skb_shinfo(skb)->destructor_arg;

	/* RX buffer has been consumed, increase the allowed budget */
	atomic_inc(&mhiwwan->rx_budget);

	if (mhi_wwan_ctrl_refill_needed(mhiwwan))
		schedule_work(&mhiwwan->rx_refill);
}

static void mhi_wwan_ctrl_refill_work(struct work_struct *work)
{
	struct mhi_wwan_dev *mhiwwan = container_of(work, struct mhi_wwan_dev, rx_refill);
	struct mhi_device *mhi_dev = mhiwwan->mhi_dev;

	if (!mhi_wwan_ctrl_refill_needed(mhiwwan))
		return;

	while (atomic_read(&mhiwwan->rx_budget)) {
		struct sk_buff *skb;

		skb = alloc_skb(mhiwwan->mtu, GFP_KERNEL);
		if (!skb)
			break;

		/* To prevent unlimited buffer allocation if nothing consumes
		 * the RX buffers (passed to WWAN core), track their lifespan
		 * to not allocate more than allowed budget.
		 */
		skb->destructor = __mhi_skb_destructor;
		skb_shinfo(skb)->destructor_arg = mhiwwan;

		if (mhi_queue_skb(mhi_dev, DMA_FROM_DEVICE, skb, mhiwwan->mtu, MHI_EOT)) {
			dev_err(&mhi_dev->dev, "Failed to queue buffer\n");
			kfree_skb(skb);
			break;
		}

		atomic_dec(&mhiwwan->rx_budget);
	}
}

static int mhi_wwan_ctrl_start(struct wwan_port *port)
{
	struct mhi_wwan_dev *mhiwwan = wwan_port_get_drvdata(port);
	int ret, rx_budget;

	/* Start mhi device's channel(s) */
	ret = mhi_prepare_for_transfer(mhiwwan->mhi_dev);
	if (ret)
		return ret;

	set_bit(MHI_WWAN_STARTED, &mhiwwan->flags);

	/* Don't allocate more buffers than MHI channel queue size */
	rx_budget = mhi_get_free_desc_count(mhiwwan->mhi_dev, DMA_FROM_DEVICE);
	atomic_set(&mhiwwan->rx_budget, rx_budget);

	/* Add buffers to the MHI inbound queue */
	mhi_wwan_ctrl_refill_work(&mhiwwan->rx_refill);

	return 0;
}

static void mhi_wwan_ctrl_stop(struct wwan_port *port)
{
	struct mhi_wwan_dev *mhiwwan = wwan_port_get_drvdata(port);

	clear_bit(MHI_WWAN_STARTED, &mhiwwan->flags);
	mhi_unprepare_from_transfer(mhiwwan->mhi_dev);
}

static int mhi_wwan_ctrl_tx(struct wwan_port *port, struct sk_buff *skb)
{
	struct mhi_wwan_dev *mhiwwan = wwan_port_get_drvdata(port);
	int ret;

	if (skb->len > mhiwwan->mtu)
		return -EMSGSIZE;

	if (!test_bit(MHI_WWAN_UL_CAP, &mhiwwan->flags))
		return -ENOTSUPP;

	spin_lock_bh(&mhiwwan->tx_lock);
	ret = mhi_queue_skb(mhiwwan->mhi_dev, DMA_TO_DEVICE, skb, skb->len, MHI_EOT);
	if (mhi_queue_is_full(mhiwwan->mhi_dev, DMA_TO_DEVICE))
		wwan_port_txoff(port);
	spin_unlock_bh(&mhiwwan->tx_lock);

	return ret;
}

static const struct wwan_port_ops wwan_pops = {
	.start = mhi_wwan_ctrl_start,
	.stop = mhi_wwan_ctrl_stop,
	.tx = mhi_wwan_ctrl_tx,
};

static void mhi_ul_xfer_cb(struct mhi_device *mhi_dev,
			   struct mhi_result *mhi_result)
{
	struct mhi_wwan_dev *mhiwwan = dev_get_drvdata(&mhi_dev->dev);
	struct wwan_port *port = mhiwwan->wwan_port;
	struct sk_buff *skb = mhi_result->buf_addr;

	dev_dbg(&mhi_dev->dev, "%s: status: %d xfer_len: %zu\n", __func__,
		mhi_result->transaction_status, mhi_result->bytes_xferd);

	/* MHI core has done with the buffer, release it */
	consume_skb(skb);

	spin_lock_bh(&mhiwwan->tx_lock);
	if (!mhi_queue_is_full(mhiwwan->mhi_dev, DMA_TO_DEVICE))
		wwan_port_txon(port);
	spin_unlock_bh(&mhiwwan->tx_lock);
}

static void mhi_dl_xfer_cb(struct mhi_device *mhi_dev,
			   struct mhi_result *mhi_result)
{
	struct mhi_wwan_dev *mhiwwan = dev_get_drvdata(&mhi_dev->dev);
	struct wwan_port *port = mhiwwan->wwan_port;
	struct sk_buff *skb = mhi_result->buf_addr;

	dev_dbg(&mhi_dev->dev, "%s: status: %d receive_len: %zu\n", __func__,
		mhi_result->transaction_status, mhi_result->bytes_xferd);

	if (mhi_result->transaction_status &&
	    mhi_result->transaction_status != -EOVERFLOW) {
		kfree_skb(skb);
		return;
	}

	/* MHI core does not update skb->len, do it before forward */
	skb_put(skb, mhi_result->bytes_xferd);
	wwan_port_rx(port, skb);
}

static int mhi_wwan_ctrl_probe(struct mhi_device *mhi_dev,
			       const struct mhi_device_id *id)
{
	struct mhi_controller *cntrl = mhi_dev->mhi_cntrl;
	struct mhi_wwan_dev *mhiwwan;

	mhiwwan = kzalloc(sizeof(*mhiwwan), GFP_KERNEL);
	if (!mhiwwan)
		return -ENOMEM;

	mhiwwan->mhi_dev = mhi_dev;
	mhiwwan->mtu = MHI_WWAN_MAX_MTU;
	INIT_WORK(&mhiwwan->rx_refill, mhi_wwan_ctrl_refill_work);
	spin_lock_init(&mhiwwan->tx_lock);

	if (mhi_dev->dl_chan)
		set_bit(MHI_WWAN_DL_CAP, &mhiwwan->flags);
	if (mhi_dev->ul_chan)
		set_bit(MHI_WWAN_UL_CAP, &mhiwwan->flags);

	dev_set_drvdata(&mhi_dev->dev, mhiwwan);

	/* Register as a wwan port, id->driver_data contains wwan port type */
	mhiwwan->wwan_port = wwan_create_port(&cntrl->mhi_dev->dev,
					      id->driver_data,
					      &wwan_pops, mhiwwan);
	if (IS_ERR(mhiwwan->wwan_port)) {
		kfree(mhiwwan);
		return PTR_ERR(mhiwwan->wwan_port);
	}

	return 0;
};

static void mhi_wwan_ctrl_remove(struct mhi_device *mhi_dev)
{
	struct mhi_wwan_dev *mhiwwan = dev_get_drvdata(&mhi_dev->dev);

	wwan_remove_port(mhiwwan->wwan_port);
	cancel_work_sync(&mhiwwan->rx_refill);
	kfree(mhiwwan);
}

static const struct mhi_device_id mhi_wwan_ctrl_match_table[] = {
	{ .chan = "DUN", .driver_data = WWAN_PORT_AT },
	{ .chan = "MBIM", .driver_data = WWAN_PORT_MBIM },
	{ .chan = "QMI", .driver_data = WWAN_PORT_QMI },
	{ .chan = "DIAG", .driver_data = WWAN_PORT_QCDM },
	{ .chan = "FIREHOSE", .driver_data = WWAN_PORT_FIREHOSE },
	{},
};
MODULE_DEVICE_TABLE(mhi, mhi_wwan_ctrl_match_table);

static struct mhi_driver mhi_wwan_ctrl_driver = {
	.id_table = mhi_wwan_ctrl_match_table,
	.remove = mhi_wwan_ctrl_remove,
	.probe = mhi_wwan_ctrl_probe,
	.ul_xfer_cb = mhi_ul_xfer_cb,
	.dl_xfer_cb = mhi_dl_xfer_cb,
	.driver = {
		.name = "mhi_wwan_ctrl",
	},
};

module_mhi_driver(mhi_wwan_ctrl_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MHI WWAN CTRL Driver");
MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
