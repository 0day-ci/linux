// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021, Intel Corporation.
 *
 * Authors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez<ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Chandrashekar Devegowda <chandrashekar.devegowda@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/wwan.h>

#include "t7xx_common.h"
#include "t7xx_hif_cldma.h"
#include "t7xx_modem_ops.h"
#include "t7xx_port.h"
#include "t7xx_port_proxy.h"
#include "t7xx_state_monitor.h"

#define CHECK_RX_SEQ_MASK		GENMASK(14, 0)
#define Q_IDX_CTRL			0
#define Q_IDX_MBIM			2
#define Q_IDX_AT_CMD			5

#define for_each_proxy_port(i, p, proxy)	\
	for (i = 0, (p) = &(proxy)->ports_private[i];	\
	     i < (proxy)->port_number;		\
	     i++, (p) = &(proxy)->ports_private[i])

static struct t7xx_port_static t7xx_md_ports[] = {
	{
		.tx_ch = PORT_CH_CONTROL_TX,
		.rx_ch = PORT_CH_CONTROL_RX,
		.txq_index = Q_IDX_CTRL,
		.rxq_index = Q_IDX_CTRL,
		.txq_exp_index = 0,
		.rxq_exp_index = 0,
		.path_id = ID_CLDMA1,
		.flags = 0,
		.ops = &ctl_port_ops,
		.name = "t7xx_ctrl",
	},
};

static struct t7xx_port *t7xx_proxy_get_port_by_ch(struct port_proxy *port_prox, enum port_ch ch)
{
	struct t7xx_port_static *port_static;
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		port_static = port->port_static;
		if (port_static->rx_ch == ch || port_static->tx_ch == ch)
			return port;
	}

	return NULL;
}

/* Sequence numbering to track for lost packets */
void t7xx_port_proxy_set_seq_num(struct t7xx_port *port, struct ccci_header *ccci_h)
{
	if (ccci_h && port) {
		ccci_h->status &= cpu_to_le32(~HDR_FLD_SEQ);
		ccci_h->status |= cpu_to_le32(FIELD_PREP(HDR_FLD_SEQ, port->seq_nums[MTK_TX]));
		ccci_h->status &= cpu_to_le32(~HDR_FLD_AST);
		ccci_h->status |= cpu_to_le32(FIELD_PREP(HDR_FLD_AST, 1));
	}
}

static u16 t7xx_port_check_rx_seq_num(struct t7xx_port *port, struct ccci_header *ccci_h)
{
	u16 seq_num, assert_bit;

	seq_num = FIELD_GET(HDR_FLD_SEQ, le32_to_cpu(ccci_h->status));
	assert_bit = FIELD_GET(HDR_FLD_AST, le32_to_cpu(ccci_h->status));
	if (assert_bit && port->seq_nums[MTK_RX] &&
	    ((seq_num - port->seq_nums[MTK_RX]) & CHECK_RX_SEQ_MASK) != 1) {
		dev_warn_ratelimited(port->dev,
				     "seq num out-of-order %d->%d (header %X, len %X)\n",
				     seq_num, port->seq_nums[MTK_RX],
				     le32_to_cpu(ccci_h->packet_header),
				     le32_to_cpu(ccci_h->packet_len));
	}

	return seq_num;
}

void t7xx_port_proxy_reset(struct port_proxy *port_prox)
{
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		port->seq_nums[MTK_RX] = -1;
		port->seq_nums[MTK_TX] = 0;
	}
}

static int t7xx_port_get_queue_no(struct t7xx_port *port)
{
	struct t7xx_port_static *port_static = port->port_static;
	struct t7xx_fsm_ctl *ctl = port->t7xx_dev->md->fsm_ctl;

	return t7xx_fsm_get_md_state(ctl) == MD_STATE_EXCEPTION ?
		port_static->txq_exp_index : port_static->txq_index;
}

static void t7xx_port_struct_init(struct t7xx_port *port)
{
	INIT_LIST_HEAD(&port->entry);
	INIT_LIST_HEAD(&port->queue_entry);
	skb_queue_head_init(&port->rx_skb_list);
	init_waitqueue_head(&port->rx_wq);
	port->seq_nums[MTK_RX] = -1;
	port->seq_nums[MTK_TX] = 0;
	atomic_set(&port->usage_cnt, 0);
}

static void t7xx_port_adjust_skb(struct t7xx_port *port, struct sk_buff *skb)
{
	struct ccci_header *ccci_h = (struct ccci_header *)skb->data;
	struct t7xx_port_static *port_static = port->port_static;

	if (port->flags & PORT_F_USER_HEADER) {
		if (le32_to_cpu(ccci_h->packet_header) == CCCI_HEADER_NO_DATA) {
			if (skb->len > sizeof(*ccci_h)) {
				dev_err_ratelimited(port->dev,
						    "Recv unexpected data for %s, skb->len=%d\n",
						    port_static->name, skb->len);
				skb_trim(skb, sizeof(*ccci_h));
			}
		}
	} else {
		skb_pull(skb, sizeof(*ccci_h));
	}
}

/**
 * t7xx_port_recv_skb() - receive skb from modem or HIF.
 * @port: port to use.
 * @skb: skb to use.
 *
 * Used to receive native HIF RX data, which has same the RX receive flow.
 *
 * Return:
 * * 0		- Success.
 * * -ENOBUFS	- Not enough queue length.
 */
int t7xx_port_recv_skb(struct t7xx_port *port, struct sk_buff *skb)
{
	unsigned long flags;

	spin_lock_irqsave(&port->rx_wq.lock, flags);
	if (port->rx_skb_list.qlen < port->rx_length_th) {
		struct ccci_header *ccci_h = (struct ccci_header *)skb->data;
		u32 channel;

		port->flags &= ~PORT_F_RX_FULLED;
		if (port->flags & PORT_F_RX_ADJUST_HEADER)
			t7xx_port_adjust_skb(port, skb);

		channel = FIELD_GET(HDR_FLD_CHN, le32_to_cpu(ccci_h->status));
		if (channel == PORT_CH_STATUS_RX) {
			port->skb_handler(port, skb);
		} else {
			if (port->wwan_port)
				wwan_port_rx(port->wwan_port, skb);
			else
				__skb_queue_tail(&port->rx_skb_list, skb);
		}

		spin_unlock_irqrestore(&port->rx_wq.lock, flags);
		wake_up_all(&port->rx_wq);
		return 0;
	}

	port->flags |= PORT_F_RX_FULLED;
	spin_unlock_irqrestore(&port->rx_wq.lock, flags);
	return -ENOBUFS;
}

static struct cldma_ctrl *get_md_ctrl(struct t7xx_port *port)
{
	enum cldma_id id = port->port_static->path_id;

	return port->t7xx_dev->md->md_ctrl[id];
}

int t7xx_port_proxy_send_skb(struct t7xx_port *port, struct sk_buff *skb)
{
	struct ccci_header *ccci_h = (struct ccci_header *)(skb->data);
	struct cldma_ctrl *md_ctrl;
	unsigned char tx_qno;
	int ret;

	tx_qno = t7xx_port_get_queue_no(port);
	t7xx_port_proxy_set_seq_num(port, ccci_h);

	md_ctrl = get_md_ctrl(port);
	ret = t7xx_cldma_send_skb(md_ctrl, tx_qno, skb, true);
	if (ret) {
		dev_err(port->dev, "Failed to send skb: %d\n", ret);
		return ret;
	}

	/* Record the port seq_num after the data is sent to HIF.
	 * Only bits 0-14 are used, thus negating overflow.
	 */
	port->seq_nums[MTK_TX]++;

	return 0;
}

int t7xx_port_send_skb_to_md(struct t7xx_port *port, struct sk_buff *skb, bool blocking)
{
	struct t7xx_port_static *port_static = port->port_static;
	struct t7xx_fsm_ctl *ctl = port->t7xx_dev->md->fsm_ctl;
	struct cldma_ctrl *md_ctrl;
	enum md_state md_state;
	unsigned int fsm_state;

	md_state = t7xx_fsm_get_md_state(ctl);

	fsm_state = t7xx_fsm_get_ctl_state(ctl);
	if (fsm_state != FSM_STATE_PRE_START) {
		if (md_state == MD_STATE_WAITING_FOR_HS1 || md_state == MD_STATE_WAITING_FOR_HS2)
			return -ENODEV;

		if (md_state == MD_STATE_EXCEPTION && port_static->tx_ch != PORT_CH_MD_LOG_TX &&
		    port_static->tx_ch != PORT_CH_UART1_TX)
			return -EBUSY;

		if (md_state == MD_STATE_STOPPED || md_state == MD_STATE_WAITING_TO_STOP ||
		    md_state == MD_STATE_INVALID)
			return -ENODEV;
	}

	md_ctrl = get_md_ctrl(port);
	return t7xx_cldma_send_skb(md_ctrl, t7xx_port_get_queue_no(port), skb, blocking);
}

static void t7xx_proxy_setup_ch_mapping(struct port_proxy *port_prox)
{
	struct t7xx_port *port;

	int i, j;

	for (i = 0; i < ARRAY_SIZE(port_prox->rx_ch_ports); i++)
		INIT_LIST_HEAD(&port_prox->rx_ch_ports[i]);

	for (j = 0; j < ARRAY_SIZE(port_prox->queue_ports); j++) {
		for (i = 0; i < ARRAY_SIZE(port_prox->queue_ports[j]); i++)
			INIT_LIST_HEAD(&port_prox->queue_ports[j][i]);
	}

	for_each_proxy_port(i, port, port_prox) {
		struct t7xx_port_static *port_static = port->port_static;
		enum cldma_id path_id = port_static->path_id;
		u8 ch_id;

		ch_id = FIELD_GET(PORT_CH_ID_MASK, port_static->rx_ch);
		list_add_tail(&port->entry, &port_prox->rx_ch_ports[ch_id]);
		list_add_tail(&port->queue_entry,
			      &port_prox->queue_ports[path_id][port_static->rxq_index]);
	}
}

void t7xx_ccci_header_init(struct ccci_header *ccci_h, unsigned int pkt_header,
			   size_t pkt_len, enum port_ch ch, unsigned int ex_msg)
{
	ccci_h->packet_header = cpu_to_le32(pkt_header);
	ccci_h->packet_len = cpu_to_le32(pkt_len);
	ccci_h->status &= cpu_to_le32(~HDR_FLD_CHN);
	ccci_h->status |= cpu_to_le32(FIELD_PREP(HDR_FLD_CHN, ch));
	ccci_h->ex_msg = cpu_to_le32(ex_msg);
}

void t7xx_ctrl_msg_header_init(struct ctrl_msg_header *ctrl_msg_h, unsigned int msg_id,
			       unsigned int ex_msg, unsigned int len)
{
	ctrl_msg_h->ctrl_msg_id = cpu_to_le32(msg_id);
	ctrl_msg_h->ex_msg = cpu_to_le32(ex_msg);
	ctrl_msg_h->data_length = cpu_to_le32(len);
}

void t7xx_port_proxy_send_msg_to_md(struct port_proxy *port_prox, enum port_ch ch,
				    unsigned int msg, unsigned int ex_msg)
{
	struct ctrl_msg_header *ctrl_msg_h;
	struct ccci_header *ccci_h;
	struct t7xx_port *port;
	struct sk_buff *skb;
	int ret;

	port = t7xx_proxy_get_port_by_ch(port_prox, ch);
	if (!port)
		return;

	skb = __dev_alloc_skb(sizeof(*ccci_h), GFP_KERNEL);
	if (!skb)
		return;

	if (ch == PORT_CH_CONTROL_TX) {
		ccci_h = (struct ccci_header *)(skb->data);
		t7xx_ccci_header_init(ccci_h, CCCI_HEADER_NO_DATA,
				      sizeof(*ctrl_msg_h) + CCCI_H_LEN, ch, 0);
		ctrl_msg_h = (struct ctrl_msg_header *)(skb->data + CCCI_H_LEN);
		t7xx_ctrl_msg_header_init(ctrl_msg_h, msg, ex_msg, 0);
		skb_put(skb, CCCI_H_LEN + sizeof(*ctrl_msg_h));
	} else {
		ccci_h = skb_put(skb, sizeof(*ccci_h));
		t7xx_ccci_header_init(ccci_h, CCCI_HEADER_NO_DATA, msg, ch, ex_msg);
	}

	ret = t7xx_port_proxy_send_skb(port, skb);
	if (ret) {
		struct t7xx_port_static *port_static = port->port_static;

		dev_err(port->dev, "port%s send to MD fail\n", port_static->name);
		dev_kfree_skb_any(skb);
	}
}

/**
 * t7xx_port_proxy_dispatch_recv_skb() - Dispatch received skb.
 * @queue: CLDMA queue.
 * @skb: Socket buffer.
 * @drop_skb_on_err: Return value that indicates in case of an error that the skb should be dropped.
 *
 * If recv_skb return with 0 or drop_skb_on_err is true, then it's the port's duty
 * to free the request and the caller should no longer reference the request.
 * If recv_skb returns any other error, caller should free the request.
 *
 * Return:
 ** 0		- Success.
 ** -EINVAL	- Failed to get skb, channel out-of-range, or invalid MD state.
 ** -ENETDOWN	- Network time out.
 */
static int t7xx_port_proxy_dispatch_recv_skb(struct cldma_queue *queue, struct sk_buff *skb,
					     bool *drop_skb_on_err)
{
	struct ccci_header *ccci_h = (struct ccci_header *)skb->data;
	struct port_proxy *port_prox = queue->md->port_prox;
	struct t7xx_fsm_ctl *ctl = queue->md->fsm_ctl;
	struct list_head *port_list;
	struct t7xx_port *port;
	u16 seq_num, channel;
	int ret = 0;
	u8 ch_id;

	channel = FIELD_GET(HDR_FLD_CHN, le32_to_cpu(ccci_h->status));
	ch_id = FIELD_GET(PORT_CH_ID_MASK, channel);

	if (t7xx_fsm_get_md_state(ctl) == MD_STATE_INVALID) {
		*drop_skb_on_err = true;
		return -EINVAL;
	}

	port_list = &port_prox->rx_ch_ports[ch_id];
	list_for_each_entry(port, port_list, entry) {
		struct t7xx_port_static *port_static = port->port_static;

		if (queue->md_ctrl->hif_id != port_static->path_id || channel !=
		    port_static->rx_ch)
			continue;

		/* Multi-cast is not supported, because one port may be freed and can modify
		 * this request before another port can process it.
		 * However we still can use req->state to do some kind of multi-cast if needed.
		 */
		if (port_static->ops->recv_skb) {
			seq_num = t7xx_port_check_rx_seq_num(port, ccci_h);
			ret = port_static->ops->recv_skb(port, skb);
			/* If the packet is stored to RX buffer successfully or dropped,
			 * the sequence number will be updated.
			 */
			if (ret == -ENETDOWN || (ret < 0 && port->flags & PORT_F_RX_ALLOW_DROP)) {
				*drop_skb_on_err = true;
				dev_err_ratelimited(port->dev,
						    "port %s RX full, drop packet\n",
						    port_static->name);
			}

			if (!ret || drop_skb_on_err)
				port->seq_nums[MTK_RX] = seq_num;
		}

		break;
	}

	return ret;
}

static int t7xx_port_proxy_recv_skb(struct cldma_queue *queue, struct sk_buff *skb)
{
	bool drop_skb_on_err = false;
	int ret;

	if (!skb)
		return -EINVAL;

	ret = t7xx_port_proxy_dispatch_recv_skb(queue, skb, &drop_skb_on_err);
	if (ret < 0 && drop_skb_on_err) {
		dev_kfree_skb_any(skb);
		return 0;
	}

	return ret;
}

/**
 * t7xx_port_proxy_md_status_notify() - Notify all ports of state.
 *@port_prox: The port_proxy pointer.
 *@state: State.
 *
 * Called by t7xx_fsm. Used to dispatch modem status for all ports,
 * which want to know MD state transition.
 */
void t7xx_port_proxy_md_status_notify(struct port_proxy *port_prox, unsigned int state)
{
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		struct t7xx_port_static *port_static = port->port_static;

		if (port_static->ops->md_state_notify)
			port_static->ops->md_state_notify(port, state);
	}
}

static void t7xx_proxy_init_all_ports(struct t7xx_modem *md)
{
	struct port_proxy *port_prox = md->port_prox;
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		struct t7xx_port_static *port_static = port->port_static;

		t7xx_port_struct_init(port);

		if (port_static->tx_ch == PORT_CH_CONTROL_TX)
			md->core_md.ctl_port = port;

		port->t7xx_dev = md->t7xx_dev;
		port->dev = &md->t7xx_dev->pdev->dev;
		spin_lock_init(&port->port_update_lock);
		spin_lock(&port->port_update_lock);
		mutex_init(&port->tx_mutex_lock);

		if (port->flags & PORT_F_CHAR_NODE_SHOW)
			port->chan_enable = true;
		else
			port->chan_enable = false;

		port->chn_crt_stat = false;
		spin_unlock(&port->port_update_lock);

		if (port_static->ops->init)
			port_static->ops->init(port);
	}

	t7xx_proxy_setup_ch_mapping(port_prox);
}

static int t7xx_proxy_alloc(struct t7xx_modem *md)
{
	unsigned int port_number = ARRAY_SIZE(t7xx_md_ports);
	struct device *dev = &md->t7xx_dev->pdev->dev;
	struct t7xx_port *ports_private;
	struct port_proxy *port_prox;
	int i;

	port_prox = devm_kzalloc(dev, sizeof(*port_prox), GFP_KERNEL);
	if (!port_prox)
		return -ENOMEM;

	md->port_prox = port_prox;
	port_prox->dev = dev;
	port_prox->ports_shared = t7xx_md_ports;

	ports_private = devm_kzalloc(dev, sizeof(*ports_private) * port_number, GFP_KERNEL);
	if (!ports_private)
		return -ENOMEM;

	for (i = 0; i < port_number; i++) {
		ports_private[i].port_static = &port_prox->ports_shared[i];
		ports_private[i].flags = port_prox->ports_shared[i].flags;
	}

	port_prox->ports_private = ports_private;
	port_prox->port_number = port_number;
	t7xx_proxy_init_all_ports(md);
	return 0;
};

/**
 * t7xx_port_proxy_init() - Initialize ports.
 * @md: Modem.
 *
 * Create all port instances.
 *
 * Return:
 * * 0		- Success.
 * * -ERROR	- Error code from failure sub-initializations.
 */
int t7xx_port_proxy_init(struct t7xx_modem *md)
{
	int ret;

	ret = t7xx_proxy_alloc(md);
	if (ret)
		return ret;

	t7xx_cldma_set_recv_skb(md->md_ctrl[ID_CLDMA1], t7xx_port_proxy_recv_skb);
	return 0;
}

void t7xx_port_proxy_uninit(struct port_proxy *port_prox)
{
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		struct t7xx_port_static *port_static = port->port_static;

		if (port_static->ops->uninit)
			port_static->ops->uninit(port);
	}
}

/**
 * t7xx_port_proxy_node_control() - Create/remove node.
 * @md: Modem.
 * @port_msg: Message.
 *
 * Used to control create/remove device node.
 *
 * Return:
 * * 0		- Success.
 * * -EFAULT	- Message check failure.
 */
int t7xx_port_proxy_node_control(struct t7xx_modem *md, struct port_msg *port_msg)
{
	u32 *port_info_base = (void *)port_msg + sizeof(*port_msg);
	struct device *dev = &md->t7xx_dev->pdev->dev;
	unsigned int ports, i;
	unsigned int version;

	version = FIELD_GET(PORT_MSG_VERSION, le32_to_cpu(port_msg->info));
	if (version != PORT_ENUM_VER ||
	    le32_to_cpu(port_msg->head_pattern) != PORT_ENUM_HEAD_PATTERN ||
	    le32_to_cpu(port_msg->tail_pattern) != PORT_ENUM_TAIL_PATTERN) {
		dev_err(dev, "Port message enumeration invalid %x:%x:%x\n",
			version, le32_to_cpu(port_msg->head_pattern),
			le32_to_cpu(port_msg->tail_pattern));
		return -EFAULT;
	}

	ports = FIELD_GET(PORT_MSG_PRT_CNT, le32_to_cpu(port_msg->info));

	for (i = 0; i < ports; i++) {
		struct t7xx_port_static *port_static;
		u32 *port_info = port_info_base + i;
		struct t7xx_port *port;
		unsigned int ch_id;
		bool en_flag;

		ch_id = FIELD_GET(PORT_INFO_CH_ID, *port_info);
		port = t7xx_proxy_get_port_by_ch(md->port_prox, ch_id);
		if (!port) {
			dev_warn(dev, "Port:%x not found\n", ch_id);
			continue;
		}

		en_flag = !!FIELD_GET(PORT_INFO_ENFLG, *port_info);

		if (t7xx_fsm_get_md_state(md->fsm_ctl) == MD_STATE_READY) {
			port_static = port->port_static;

			if (en_flag) {
				if (port_static->ops->enable_chl)
					port_static->ops->enable_chl(port);
			} else {
				if (port_static->ops->disable_chl)
					port_static->ops->disable_chl(port);
			}
		} else {
			port->chan_enable = en_flag;
		}
	}

	return 0;
}
