// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>
#include <linux/workqueue.h>
#include <linux/rtnetlink.h>
#include <linux/string.h>
#include "r8169.h"
#include "r8169_dash.h"

#define IBCR0				0xf8
#define IBCR2				0xf9
#define IBIMR0				0xfa
#define IBISR0				0xfb

#define RTXS_LS				BIT(12)
#define RTXS_FS				BIT(13)
#define RTXS_EOR			BIT(14)
#define RTXS_OWN			BIT(15)

#define DASH_ISR_ROK			BIT(0)
#define DASH_ISR_RDU			BIT(1)
#define DASH_ISR_TOK			BIT(2)
#define DASH_ISR_TDU			BIT(3)
#define DASH_ISR_TX_DISABLE_IDLE	BIT(5)
#define DASH_ISR_RX_DISABLE_IDLE	BIT(6)

#define CMAC_DESC_NUM		4
#define CMAC_DESC_SIZE		(CMAC_DESC_NUM * sizeof(struct cmac_desc))
#define CMAC_TIMEOUT		(HZ * 5)

#define OOB_CMD_DRIVER_START		0x05
#define OOB_CMD_DRIVER_STOP		0x06
#define OOB_CMD_CMAC_STOP		0x25
#define OOB_CMD_CMAC_INIT		0x26
#define OOB_CMD_CMAC_RESET		0x2a

enum dash_cmac_state {
	CMAC_STATE_STOP = 0,
	CMAC_STATE_READY,
	CMAC_STATE_RUNNING,
};

enum dash_flag {
	DASH_FLAG_CHECK_CMAC = 0,
	DASH_FLAG_MAX
};

#pragma pack(push)
#pragma pack(1)

struct cmac_desc {
	__le16 length;
	__le16 status;
	__le32 resv;
	__le64 dma_addr;
};

struct oob_hdr {
	__le32 len;
	u8 type;
	u8 flag;
	u8 host_req;
	u8 res;
};

#pragma pack(pop)

struct dash_tx_info {
	u8 *buf;
	u32 len;
	bool ack;
};

struct rtl_dash {
	struct rtl8169_private *tp;
	struct pci_dev *pdev_cmac;
	void __iomem *cmac_ioaddr;
	struct cmac_desc *tx_desc, *rx_desc;
	struct page *tx_buf, *rx_buf;
	struct dash_tx_info tx_info[CMAC_DESC_NUM];
	struct tasklet_struct tl;
	struct completion cmac_tx, cmac_rx, fw_ack;
	spinlock_t cmac_lock; /* spin lock for CMAC */
	dma_addr_t tx_desc_dma, rx_desc_dma;
	enum rtl_dash_type hw_dash_ver;
	enum dash_cmac_state cmac_state;

	struct {
		DECLARE_BITMAP(flags, DASH_FLAG_MAX);
		struct work_struct work;
	} wk;

	/* u64 */

	/* u32 */
	u32 tx_free, tx_used;
	u32 rx_cur;

	/* u16 */

	/* u8 */
};

/* cmac write/read MMIO register */
#define RTL_CMAC_W8(d, reg, v8)		writeb((v8), (d)->cmac_ioaddr + (reg))
#define RTL_CMAC_W16(d, reg, v16)	writew((v16), (d)->cmac_ioaddr + (reg))
#define RTL_CMAC_W32(d, reg, v32)	writel((v32), (d)->cmac_ioaddr + (reg))
#define RTL_CMAC_R8(d, reg)		readb((d)->cmac_ioaddr + (reg))
#define RTL_CMAC_R16(d, reg)		readw((d)->cmac_ioaddr + (reg))
#define RTL_CMAC_R32(d, reg)		readl((d)->cmac_ioaddr + (reg))

struct rtl_dash_cond {
	bool (*check)(struct rtl_dash *dash);
	const char *msg;
};

static bool rtl_dash_loop_wait(struct rtl_dash *dash,
			       const struct rtl_dash_cond *c,
			       unsigned long usecs, int n, bool high)
{
	int i;

	for (i = 0; i < n; i++) {
		if (c->check(dash) == high)
			return true;
		fsleep(usecs);
	}

	if (net_ratelimit())
		dev_err(&dash->pdev_cmac->dev,
			"%s == %d (loop: %d, delay: %lu).\n",
			c->msg, !high, n, usecs);
	return false;
}

static bool rtl_dash_loop_wait_high(struct rtl_dash *dash,
				    const struct rtl_dash_cond *c,
				    unsigned long d, int n)
{
	return rtl_dash_loop_wait(dash, c, d, n, true);
}

static bool rtl_dash_loop_wait_low(struct rtl_dash *dash,
				   const struct rtl_dash_cond *c,
				   unsigned long d, int n)
{
	return rtl_dash_loop_wait(dash, c, d, n, false);
}

#define DECLARE_RTL_DASH_COND(name)			\
static bool name ## _check(struct rtl_dash *dash);	\
							\
static const struct rtl_dash_cond name = {		\
	.check	= name ## _check,			\
	.msg	= #name					\
};							\
							\
static bool name ## _check(struct rtl_dash *dash)

static inline void rtl_dash_intr_en(struct rtl_dash *dash)
{
	RTL_CMAC_W8(dash, IBIMR0, DASH_ISR_ROK | DASH_ISR_RDU | DASH_ISR_TOK |
				  DASH_ISR_TDU | DASH_ISR_RX_DISABLE_IDLE);
}

static void dash_tx_bottom(struct rtl_dash *dash)
{
	u32 tx_used = dash->tx_used;

	while (tx_used != dash->tx_free) {
		struct device *d = &dash->pdev_cmac->dev;
		u32 index = tx_used % CMAC_DESC_NUM;
		struct cmac_desc *tx_desc = dash->tx_desc + index;
		struct dash_tx_info *info = dash->tx_info + index;

		if (le16_to_cpu(tx_desc->status) & RTXS_OWN)
			break;

		dma_unmap_single(d, le64_to_cpu(tx_desc->dma_addr), info->len,
				 DMA_TO_DEVICE);

		if (!info->ack) {
			complete(&dash->cmac_tx);
			dev_dbg(d, "CMAC send TX OK\n");
		}

		info->len = 0;
		tx_used++;
	}

	if (dash->tx_used != tx_used)
		dash->tx_used = tx_used;
}

static int cmac_start_xmit(struct rtl_dash *dash, u8 *data, u32 size, bool ack)
{
	struct device *d = &dash->pdev_cmac->dev;
	u32 index = dash->tx_free % CMAC_DESC_NUM;
	struct cmac_desc *desc = dash->tx_desc + index;
	struct dash_tx_info *info = dash->tx_info + index;
	dma_addr_t mapping;
	int ret;

	if (dash->cmac_state != CMAC_STATE_RUNNING)
		return -ENETDOWN;

	if ((dash->tx_free - dash->tx_used) >= CMAC_DESC_NUM)
		return -EBUSY;

	if (IS_ERR_OR_NULL(data) || size > CMAC_BUF_SIZE)
		return -EINVAL;

	if (unlikely(le16_to_cpu(desc->status) & RTXS_OWN))
		return -EFAULT;

	memcpy(info->buf, data, size);
	if (ack) {
		struct oob_hdr *hdr = (struct oob_hdr *)info->buf;

		hdr->len = 0;
	}

	mapping = dma_map_single(d, info->buf, size, DMA_TO_DEVICE);
	ret = dma_mapping_error(d, mapping);
	if (unlikely(ret)) {
		dev_err(d, "Failed to map TX DMA!\n");
		goto err;
	}

	info->len = size;
	info->ack = ack;

	desc->dma_addr = cpu_to_le64(mapping);
	desc->length = cpu_to_le16(size);
	dma_wmb();
	desc->status |= cpu_to_le16(RTXS_OWN);
	dma_wmb();
	RTL_CMAC_W8(dash, IBCR2, RTL_CMAC_R8(dash, IBCR2) | BIT(1));

	dash->tx_free++;
	ret = size;

err:
	return ret;
}

static int dash_rx_data(struct rtl_dash *dash, u8 *target, u32 size)
{
	u32 cur = dash->rx_cur;
	int i, ret = 0;

	for (i = 0; i < CMAC_DESC_NUM; i++) {
		u32 index = cur % CMAC_DESC_NUM;
		struct cmac_desc *desc = dash->rx_desc + index;
		struct device *d = &dash->pdev_cmac->dev;
		struct oob_hdr *hdr;
		dma_addr_t mapping;
		u8 *addr;
		u16 pkt_size;

		if (le16_to_cpu(desc->status) & RTXS_OWN)
			break;

		addr = page_address(dash->rx_buf) + index * CMAC_BUF_SIZE;
		pkt_size = le16_to_cpu(desc->length);

		mapping = le64_to_cpu(desc->dma_addr);
		if (mapping) {
			dma_unmap_single(d, mapping, pkt_size, DMA_FROM_DEVICE);
			desc->dma_addr = 0;
		}

		if (pkt_size < sizeof(*hdr))
			goto re_map;

		hdr = (struct oob_hdr *)addr;
		switch (hdr->host_req) {
		case 0x91:
			dev_dbg(d, "CMAC RX DATA\n");
			if (mapping &&
			    cmac_start_xmit(dash, addr, sizeof(struct oob_hdr),
					    true) < 0)
				dev_err(d, "send ACK fail\n");

			complete(&dash->cmac_rx);

			if (!target) {
				ret = -EDESTADDRREQ;
				break;
			} else if (size < pkt_size) {
				ret = -EMSGSIZE;
				break;
			}
			memcpy(target, addr, pkt_size);
			ret = pkt_size;
			break;
		case 0x92:
			dev_dbg(d, "CMAC RX ACK\n");
			complete(&dash->fw_ack);
			break;
		default:
			break;
		}

		if (ret < 0)
			break;

re_map:
		mapping = dma_map_single(d, addr, CMAC_BUF_SIZE,
					 DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(d, mapping))) {
			dev_err(d, "Failed to map RX DMA!\n");
			desc->length = 0;
			break;
		}

		desc->dma_addr = cpu_to_le64(mapping);
		desc->length = cpu_to_le16(CMAC_BUF_SIZE);
		dma_wmb();
		desc->status |= cpu_to_le16(RTXS_OWN);

		cur++;

		if (ret == pkt_size)
			break;
	}

	if (dash->rx_cur != cur)
		dash->rx_cur = cur;

	return ret;
}

static void dash_half(struct tasklet_struct *t)
{
	struct rtl_dash *dash = from_tasklet(dash, t, tl);

	dash_tx_bottom(dash);

	spin_lock(&dash->cmac_lock);
	dash_rx_data(dash, NULL, 0);
	spin_unlock(&dash->cmac_lock);

	rtl_dash_intr_en(dash);
}

static void rtl_dash_change_cmac_state(struct rtl_dash *dash, u8 state);

DECLARE_RTL_DASH_COND(rtl_cmac_tx_cond)
{
	return RTL_CMAC_R8(dash, IBISR0) & DASH_ISR_TX_DISABLE_IDLE;
}

static void rtl_cmac_disable(struct rtl_dash *dash)
{
	u8 status;
	int i;

	if (dash->cmac_state == CMAC_STATE_RUNNING)
		tasklet_disable(&dash->tl);

	dash->cmac_state = CMAC_STATE_STOP;

	status = RTL_CMAC_R8(dash, IBCR2);
	if (status & 0x01) {
		RTL_CMAC_W8(dash, IBCR2, status & ~0x01);
		rtl_dash_loop_wait_high(dash, &rtl_cmac_tx_cond, 5000, 2000);
	}

	status = RTL_CMAC_R8(dash, IBCR0);
	if (status & 0x01)
		RTL_CMAC_W8(dash, IBCR0, status & ~0x01);

	for (i = 0; i < CMAC_DESC_NUM; i++) {
		struct device *d = &dash->pdev_cmac->dev;
		struct cmac_desc *tx_desc = dash->tx_desc + i;
		struct cmac_desc *rx_desc = dash->rx_desc + i;

		if (dash->tx_info[i].len)
			dma_unmap_single(d, le64_to_cpu(tx_desc->dma_addr),
					 dash->tx_info[i].len, DMA_TO_DEVICE);

		if (rx_desc->dma_addr)
			dma_unmap_single(d, le64_to_cpu(rx_desc->dma_addr),
					 CMAC_BUF_SIZE, DMA_TO_DEVICE);
	}

	memset(dash->tx_desc, 0, CMAC_DESC_SIZE);
	memset(dash->rx_desc, 0, CMAC_DESC_SIZE);
	memset(dash->tx_info, 0, sizeof(dash->tx_info));

	RTL_CMAC_W8(dash, IBIMR0, 0);
	RTL_CMAC_W8(dash, IBISR0, RTL_CMAC_R8(dash, IBISR0));
}

static int rtl_cmac_enable(struct rtl_dash *dash)
{
	u32 desc_addr;
	int i;

	for (i = 0; i < CMAC_DESC_NUM; i++) {
		struct dash_tx_info *info = dash->tx_info + i;
		struct cmac_desc *rx_desc = dash->rx_desc + i;
		struct cmac_desc *tx_desc = dash->tx_desc + i;
		struct device *d = &dash->pdev_cmac->dev;
		dma_addr_t mapping;
		u8 *addr;
		u16 ops_rx, ops_tx;

		info->buf = page_address(dash->tx_buf) + i * CMAC_BUF_SIZE;
		info->len = 0;
		info->ack = false;

		addr = page_address(dash->rx_buf) + i * CMAC_BUF_SIZE;
		mapping = dma_map_single(d, addr, CMAC_BUF_SIZE,
					 DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(d, mapping))) {
			dev_err(d, "Failed to map RX DMA!\n");
			rtl_dash_change_cmac_state(dash, OOB_CMD_CMAC_STOP);
			return -ENOMEM;
		}

		rx_desc->dma_addr = cpu_to_le64(mapping);
		rx_desc->length = CMAC_BUF_SIZE;
		rx_desc->resv = 0;

		if (i == (CMAC_DESC_NUM - 1)) {
			ops_rx = RTXS_OWN | RTXS_EOR;
			ops_tx = RTXS_FS | RTXS_LS | RTXS_EOR;
		} else {
			ops_rx = RTXS_OWN;
			ops_tx = RTXS_FS | RTXS_LS;
		}

		rx_desc->status = cpu_to_le16(ops_rx);
		tx_desc->status = cpu_to_le16(ops_tx);
	}

	dash->tx_free = 0;
	dash->tx_used = 0;
	dash->rx_cur = 0;

	switch (dash->hw_dash_ver) {
	case RTL_DASH_EP:
		desc_addr = 0x890;
		break;
	case RTL_DASH_FP:
		desc_addr = 0xf20090;
		break;
	default:
		desc_addr = 0xf20090;
		WARN_ON_ONCE(1);
		break;
	}

	r8168_type2_write(dash->tp, 0xf, desc_addr,
			  dash->rx_desc_dma & DMA_BIT_MASK(32));
	r8168_type2_write(dash->tp, 0xf, desc_addr + 4,
			  dash->rx_desc_dma >> 32);
	r8168_type2_write(dash->tp, 0xf, desc_addr + 8,
			  dash->tx_desc_dma & DMA_BIT_MASK(32));
	r8168_type2_write(dash->tp, 0xf, desc_addr + 12,
			  dash->tx_desc_dma >> 32);

	RTL_CMAC_W8(dash, IBCR2, RTL_CMAC_R8(dash, IBCR2) | 0x01);
	RTL_CMAC_W8(dash, IBCR0, RTL_CMAC_R8(dash, IBCR0) | 0x01);

	tasklet_enable(&dash->tl);
	dash->cmac_state = CMAC_STATE_RUNNING;

	rtl_dash_intr_en(dash);

	return 0;
}

static void rtl_dash_oob_notify(struct rtl_dash *dash, u8 cmd)
{
	struct rtl8169_private *tp = dash->tp;

	r8168ep_ocp_write(tp, 0x01, 0x180, cmd);
	r8168ep_ocp_write(tp, 0x01, 0x30, r8168ep_ocp_read(tp, 0x30) | 0x01);
}

static void rtl_cmac_hw_reset(struct rtl_dash *dash)
{
	struct rtl8169_private *tp = dash->tp;
	u32 TmpUlong;

	TmpUlong = r8168ep_ocp_read(tp, 0x150);
	r8168ep_ocp_write(tp, 0xf, 0x150, TmpUlong | BIT(5));

	switch (dash->hw_dash_ver) {
	case RTL_DASH_EP:
		r8168ep_ocp_write(tp, 0xf, 0x150, TmpUlong & ~BIT(5));
		RTL_CMAC_W8(dash, IBISR0,
			    RTL_CMAC_R8(dash, IBISR0) | DASH_ISR_ROK);

		TmpUlong = r8168ep_ocp_read(tp, 0x80c);
		r8168ep_ocp_write(tp, 0xf, 0x80c, TmpUlong | BIT(24));
		break;
	case RTL_DASH_FP:
		fsleep(1);
		RTL_CMAC_W8(dash, IBISR0,
			    RTL_CMAC_R8(dash, IBISR0) | DASH_ISR_ROK);
		break;
	default:
		break;
	}

	dash->cmac_state = CMAC_STATE_READY;
}

static void rtl_dash_change_cmac_state(struct rtl_dash *dash, u8 state)
{
	switch (state) {
	case OOB_CMD_CMAC_INIT:
		if (likely(dash->cmac_state != CMAC_STATE_RUNNING)) {
			rtl_cmac_hw_reset(dash);
			break;
		}
		state = OOB_CMD_CMAC_STOP;
		WARN_ON_ONCE(1);
		fallthrough;
	case OOB_CMD_CMAC_STOP:
		rtl_cmac_disable(dash);
		break;

	case OOB_CMD_CMAC_RESET:
	default:
		WARN_ON_ONCE(1);
		return;
	}

	rtl_dash_oob_notify(dash, state);
}

static void rtl_dash_cmac_reset_routine(struct rtl_dash *dash)
{
	u32 reg, state;

	switch (dash->hw_dash_ver) {
	case RTL_DASH_EP:
		reg = 0x2c20;
		break;
	case RTL_DASH_FP:
		reg = 0xf80420;
		break;
	default:
		WARN_ON_ONCE(1);
		reg = 0xf80420;
		break;
	}

	state = r8168_type2_read(dash->tp, reg);
	r8168_type2_write(dash->tp, 0xf, reg, 0);

	switch (state) {
	case OOB_CMD_CMAC_RESET:
		dev_dbg(&dash->pdev_cmac->dev, "OOB_CMD_CMAC_RESET\n");
		rtl_dash_change_cmac_state(dash, OOB_CMD_CMAC_STOP);
		break;
	case OOB_CMD_CMAC_STOP:
		dev_dbg(&dash->pdev_cmac->dev, "OOB_CMD_CMAC_STOP\n");
		rtl_dash_change_cmac_state(dash, OOB_CMD_CMAC_INIT);
		break;
	case OOB_CMD_CMAC_INIT:
		dev_dbg(&dash->pdev_cmac->dev, "OOB_CMD_CMAC_INIT\n");
		rtl_cmac_enable(dash);
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}
}

static void rtl_dash_schedule_work(struct rtl_dash *dash, enum dash_flag flag)
{
	set_bit(flag, dash->wk.flags);
	queue_work(system_long_wq, &dash->wk.work);
}

static void rtl_dash_task(struct work_struct *work)
{
	struct rtl_dash *dash = container_of(work, struct rtl_dash, wk.work);

	rtnl_lock();

	if (test_and_clear_bit(DASH_FLAG_CHECK_CMAC, dash->wk.flags))
		rtl_dash_cmac_reset_routine(dash);

	rtnl_unlock();
}

DECLARE_RTL_DASH_COND(rtl_dash_state_cond)
{
	return r8168ep_ocp_read(dash->tp, 0x124) & 0x00000001;
}

static void rtl_driver_start(struct rtl_dash *dash)
{
	rtl_dash_oob_notify(dash, OOB_CMD_DRIVER_START);
	rtl_dash_loop_wait_high(dash, &rtl_dash_state_cond, 10000, 10);
}

static void rtl_driver_stop(struct rtl_dash *dash)
{
	rtl_dash_oob_notify(dash, OOB_CMD_DRIVER_STOP);
	rtl_dash_loop_wait_low(dash, &rtl_dash_state_cond, 10000, 10);
}

static int rtl_get_cmac_resource(struct rtl_dash **pdash, struct pci_dev *pdev)
{
	struct pci_dev *pdev_cmac;
	void __iomem *cmac_ioaddr;

	pdev_cmac = pci_get_slot(pdev->bus,
				 PCI_DEVFN(PCI_SLOT(pdev->devfn), 0));
	cmac_ioaddr = ioremap(pci_resource_start((*pdash)->pdev_cmac, 2), 256);
	if (cmac_ioaddr) {
		(*pdash)->pdev_cmac = pdev_cmac;
		(*pdash)->cmac_ioaddr = cmac_ioaddr;
		return 0;
	} else {
		return -ENOMEM;
	}
}

struct rtl_dash *rtl_request_dash(struct rtl8169_private *tp,
				  struct pci_dev *pci_dev, enum mac_version ver,
				  void __iomem *mmio_addr)
{
	unsigned int order = get_order(CMAC_DESC_NUM * CMAC_BUF_SIZE);
	struct rtl_dash *dash;
	int node, ret = -ENOMEM;

	dash = kzalloc(sizeof(*dash), GFP_KERNEL);
	if (!dash)
		goto err1;

	dash->tp = tp;

	switch (ver) {
	case RTL_GIGA_MAC_VER_49 ... RTL_GIGA_MAC_VER_51:
		dash->pdev_cmac = pci_dev;
		dash->cmac_ioaddr = mmio_addr;
		dash->hw_dash_ver = RTL_DASH_EP;
		break;
	case RTL_GIGA_MAC_VER_52 ... RTL_GIGA_MAC_VER_53:
		if (rtl_get_cmac_resource(&dash, pci_dev) < 0)
			goto err2;
		dash->hw_dash_ver = RTL_DASH_FP;
		break;
	default:
		ret = -ENODEV;
		goto err2;
	}

	dash->tx_desc = dma_alloc_coherent(&dash->pdev_cmac->dev,
					   CMAC_DESC_SIZE, &dash->tx_desc_dma,
					   GFP_KERNEL);
	if (!dash->tx_desc)
		goto err2;

	dash->rx_desc = dma_alloc_coherent(&dash->pdev_cmac->dev,
					   CMAC_DESC_SIZE, &dash->rx_desc_dma,
					   GFP_KERNEL);
	if (!dash->rx_desc)
		goto free_tx_desc;

	node = dev_to_node(&dash->pdev_cmac->dev);

	dash->tx_buf = alloc_pages_node(node, GFP_KERNEL, order);
	if (!dash->tx_buf)
		goto free_rx_desc;

	dash->rx_buf = alloc_pages_node(node, GFP_KERNEL, order);
	if (!dash->rx_buf)
		goto free_tx_fuf;

	memset(dash->tx_desc, 0, CMAC_DESC_SIZE);
	memset(dash->rx_desc, 0, CMAC_DESC_SIZE);
	memset(dash->tx_info, 0, sizeof(dash->tx_info));

	INIT_WORK(&dash->wk.work, rtl_dash_task);
	tasklet_setup(&dash->tl, dash_half);
	tasklet_disable(&dash->tl);
	init_completion(&dash->cmac_tx);
	init_completion(&dash->cmac_rx);
	init_completion(&dash->fw_ack);
	spin_lock_init(&dash->cmac_lock);

	return dash;

free_tx_fuf:
	__free_pages(dash->tx_buf, order);
free_rx_desc:
	dma_free_coherent(&dash->pdev_cmac->dev, CMAC_DESC_SIZE, dash->rx_desc,
			  dash->rx_desc_dma);
free_tx_desc:
	dma_free_coherent(&dash->pdev_cmac->dev, CMAC_DESC_SIZE, dash->tx_desc,
			  dash->tx_desc_dma);
err2:
	kfree(dash);
err1:
	return ERR_PTR(ret);
}

void rtl_release_dash(struct rtl_dash *dash)
{
	unsigned int order = get_order(CMAC_DESC_NUM * CMAC_BUF_SIZE);

	if (IS_ERR_OR_NULL(dash))
		return;

	tasklet_kill(&dash->tl);

	__free_pages(dash->rx_buf, order);
	__free_pages(dash->tx_buf, order);
	dma_free_coherent(&dash->pdev_cmac->dev, CMAC_DESC_SIZE, dash->rx_desc,
			  dash->rx_desc_dma);
	dma_free_coherent(&dash->pdev_cmac->dev, CMAC_DESC_SIZE, dash->tx_desc,
			  dash->tx_desc_dma);

	if (dash->hw_dash_ver != RTL_DASH_EP)
		iounmap(dash->cmac_ioaddr);

	kfree(dash);
}

void rtl_dash_up(struct rtl_dash *dash)
{
	rtl_driver_start(dash);
	rtl_dash_change_cmac_state(dash, OOB_CMD_CMAC_STOP);
}

void rtl_dash_down(struct rtl_dash *dash)
{
	bitmap_zero(dash->wk.flags, DASH_FLAG_MAX);
	cancel_work_sync(&dash->wk.work);

	rtl_cmac_disable(dash);
	rtl_driver_stop(dash);
}

void rtl_dash_cmac_reset_indicate(struct rtl_dash *dash)
{
	rtl_dash_schedule_work(dash, DASH_FLAG_CHECK_CMAC);
}

void rtl_dash_interrupt(struct rtl_dash *dash)
{
	RTL_CMAC_W8(dash, IBIMR0, 0);
	RTL_CMAC_W8(dash, IBISR0, RTL_CMAC_R8(dash, IBISR0));
	tasklet_schedule(&dash->tl);
}

