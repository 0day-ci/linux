// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for HiSilicon PCIe tune and trace device
 *
 * Copyright (c) 2021 HiSilicon Limited.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/seq_file.h>
#include <linux/pci.h>
#include <linux/workqueue.h>

#define HISI_PTT_CTRL_STR_LEN			40
#define HISI_PTT_DEFAULT_TRACE_BUF_CNT		64
#define HISI_PTT_FILTER_UPDATE_FIFO_SIZE	16

#define HISI_PTT_RESET_WAIT_MS		1000UL
#define HISI_PTT_WORK_DELAY_MS		100UL
#define HISI_PTT_WAIT_TIMEOUT_US	1000000UL
#define HISI_PTT_WAIT_POLL_INTERVAL_US	100UL

#define HISI_PTT_IRQ_NUMS	1
#define HISI_PTT_DMA_IRQ	0
#define HISI_PTT_DMA_NUMS	4

#define HISI_PTT_TUNING_CTRL		0x0000
#define   HISI_PTT_TUNING_CTRL_CODE	GENMASK(15, 0)
#define   HISI_PTT_TUNING_CTRL_SUB	GENMASK(23, 16)
#define   HISI_PTT_TUNING_CTRL_CHN	GENMASK(31, 24)
#define HISI_PTT_TUNING_DATA		0x0004
#define   HISI_PTT_TUNING_DATA_VAL_MASK	GENMASK(15, 0)
#define HISI_PTT_TRACE_ADDR_SIZE	0x0800
#define HISI_PTT_TRACE_ADDR_BASE_LO_0	0x0810
#define HISI_PTT_TRACE_ADDR_BASE_HI_0	0x0814
#define HISI_PTT_TRACE_ADDR_STRIDE	0x8
#define HISI_PTT_TRACE_CTRL		0x0850
#define   HISI_PTT_TRACE_CTRL_EN	BIT(0)
#define   HISI_PTT_TRACE_CTRL_RST	BIT(1)
#define   HISI_PTT_TRACE_CTRL_RXTX_SEL	GENMASK(3, 2)
#define   HISI_PTT_TRACE_CTRL_TYPE_SEL	GENMASK(7, 4)
#define   HISI_PTT_TRACE_CTRL_DATA_FORMAT	BIT(14)
#define   HISI_PTT_TRACE_CTRL_FILTER_MODE	BIT(15)
#define   HISI_PTT_TRACE_CTRL_TARGET_SEL	GENMASK(31, 16)
#define HISI_PTT_TRACE_INT_STAT		0x0890
#define   HISI_PTT_TRACE_INT_STAT_MASK	GENMASK(3, 0)
#define HISI_PTT_TRACE_INT_MASK_REG	0x0894
#define HISI_PTT_TUNING_INT_STAT	0x0898
#define   HISI_PTT_TUNING_INT_STAT_MASK	BIT(0)
#define HISI_PTT_TUNING_INT_MASK	0x089c
#define HISI_PTT_TRACE_WR_STS		0x08a0
#define   HISI_PTT_TRACE_WR_STS_WRITE	GENMASK(27, 0)
#define   HISI_PTT_TRACE_WR_STS_BUFFER	GENMASK(29, 28)
#define HISI_PTT_TRACE_STS		0x08b0
#define   HISI_PTT_TRACE_IDLE		BIT(0)
#define HISI_PTT_DEVICE_RANGE		0x0fe0

#define HISI_PCIE_CORE_PORT_ID(v)	(((v) & 7) << 1)

static struct dentry *hisi_ptt_debugfs_root;

struct hisi_ptt_debugfs_file_desc {
	struct hisi_ptt *hisi_ptt;
	const char *name;
	const struct file_operations *fops;
};

#define PTT_FILE_INIT(__name, __fops)	\
	{ .name = __name, .fops = __fops }

struct hisi_ptt_trace_event_desc {
	const char *name;
	u32 event_code;
};

#define PTT_TRACE_EVENT_INIT(__name, __event_code) \
	{ .name = __name, .event_code = __event_code }

enum hisi_ptt_trace_rxtx {
	HISI_PTT_TRACE_RX = 0,
	HISI_PTT_TRACE_TX,
	HISI_PTT_TRACE_RXTX,
	HISI_PTT_TRACE_RXTX_NO_DMA_P2P,
};

static const struct hisi_ptt_trace_event_desc trace_rxtx[] = {
	PTT_TRACE_EVENT_INIT("rx",
			      HISI_PTT_TRACE_RX),
	PTT_TRACE_EVENT_INIT("tx",
			      HISI_PTT_TRACE_TX),
	PTT_TRACE_EVENT_INIT("rxtx",
			      HISI_PTT_TRACE_RXTX),
	PTT_TRACE_EVENT_INIT("rxtx_no_dma_p2p",
			      HISI_PTT_TRACE_RXTX_NO_DMA_P2P),
};
#define HISI_PTT_TRACE_DEFAULT_RXTX	trace_rxtx[2]


enum hisi_ptt_trace_event {
	HISI_PTT_TRACE_POSTED_REQUEST		= 1,
	HISI_PTT_TRACE_NON_POSTED_REQUEST	= 2,
	HISI_PTT_TRACE_COMPLETION		= 4,
	HISI_PTT_TRACE_ALL			= 7,
};

static const struct hisi_ptt_trace_event_desc trace_events[] = {
	PTT_TRACE_EVENT_INIT("all",
			      HISI_PTT_TRACE_POSTED_REQUEST),
	PTT_TRACE_EVENT_INIT("posted_request",
			      HISI_PTT_TRACE_NON_POSTED_REQUEST),
	PTT_TRACE_EVENT_INIT("non-posted_request",
			      HISI_PTT_TRACE_COMPLETION),
	PTT_TRACE_EVENT_INIT("completion",
			      HISI_PTT_TRACE_ALL),
};
#define HISI_PTT_TRACE_DEFAULT_EVENT	trace_events[0]

static const unsigned int available_buflet_size[] = {
	0x00200000,	/* 2  MiB */
	0x00400000,	/* 4  MiB */
};
#define HISI_PTT_TRACE_DEFAULT_BUFLET_SIZE	available_buflet_size[0]

struct hisi_ptt_dma_buflet {
	struct list_head list;
	unsigned int size;
	dma_addr_t dma;

	/*
	 * The address of the buflet holding the trace data.
	 * See Documentation/trace/hisi-ptt.rst for the details
	 * of the data format.
	 */
	u32 *addr;
	int index;
};

enum hisi_ptt_trace_data_format {
	HISI_PTT_TRACE_DATA_4DW = 0,
	HISI_PTT_TRACE_DATA_8DW,
};

struct hisi_ptt_trace_ctrl {
	struct list_head trace_buf;
	struct hisi_ptt_dma_buflet *cur;
	u64 buflet_nums;
	u32 buflet_size;
	unsigned int status; /* 0:idle, 1:tracing */
#define	HISI_PTT_TRACE_STATUS_OFF	0
#define	HISI_PTT_TRACE_STATUS_ON	1

	enum hisi_ptt_trace_data_format tr_format;
	enum hisi_ptt_trace_event tr_event;
	enum hisi_ptt_trace_rxtx rxtx;
};

struct hisi_ptt_filter_desc {
	struct list_head list;
	struct pci_dev *pdev;
	u16 val;
};

/* Structure containing the information for filter updating */
struct hisi_ptt_filter_update_info {
	struct pci_dev *pdev;
	bool is_port;
	bool is_add;
	u32 port_devid;
	u16 val;
};

struct hisi_ptt {
	struct notifier_block hisi_ptt_nb;
	struct dentry *debugfs_dir;
	struct pci_dev *pdev;
	void __iomem *iobase;
	/* protects the hisi_ptt structure */
	struct mutex mutex;
	const char *name;

	/*
	 * We use a delayed work here to avoid indefinitely waiting for
	 * the hisi_ptt->mutex which protecting the filter list. The
	 * work will be delayed only if the mutex can not be held,
	 * otherwise no delay will be applied.
	 */
	struct delayed_work work;
	spinlock_t filter_update_lock;
	DECLARE_KFIFO(filter_update_kfifo, struct hisi_ptt_filter_update_info,
		      HISI_PTT_FILTER_UPDATE_FIFO_SIZE);

	/*
	 * The upper and lower BDF numbers of the root port,
	 * which PTT device can control and trace.
	 */
	u16 upper;
	u16 lower;

	/*
	 * The trace TLP headers can either be filtered by certain
	 * root port, or by the requester ID. Organize the filters
	 * by @port_filters and @req_filters here.
	 */
	struct list_head port_filters;
	struct list_head req_filters;
	struct hisi_ptt_trace_ctrl trace_ctrl;
	struct {
		bool is_port;
		u16 val;
	} cur_filter;
};

static int hisi_ptt_write_to_buffer(void *buf, size_t len, loff_t *pos,
				    const void __user *ubuf, size_t count)
{
	char *cp;

	if (simple_write_to_buffer(buf, len, pos, ubuf, count) < 0)
		return -EINVAL;

	cp = strchr(buf, '\n');
	if (cp)
		*cp = '\0';

	return 0;
}

static u16 hisi_ptt_get_filter_val(struct pci_dev *pdev)
{
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT)
		return BIT(HISI_PCIE_CORE_PORT_ID(PCI_SLOT(pdev->devfn)));
	else
		return PCI_DEVID(pdev->bus->number, pdev->devfn);
}

static int hisi_ptt_wait_trace_hw_idle(struct hisi_ptt *hisi_ptt)
{
	u32 val;

	return readl_poll_timeout(hisi_ptt->iobase + HISI_PTT_TRACE_STS, val,
				  val & HISI_PTT_TRACE_IDLE,
				  HISI_PTT_WAIT_POLL_INTERVAL_US,
				  HISI_PTT_WAIT_TIMEOUT_US);
}

static void hisi_ptt_free_trace_buf(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	struct device *dev = &hisi_ptt->pdev->dev;
	struct hisi_ptt_dma_buflet *buflet, *tbuflet;

	if (list_empty(&ctrl->trace_buf))
		return;

	list_for_each_entry_safe(buflet, tbuflet, &ctrl->trace_buf, list) {
		dma_free_coherent(dev, buflet->size, buflet->addr, buflet->dma);
		list_del(&buflet->list);
		kfree(buflet);
	}
}

static int hisi_ptt_alloc_trace_buf(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	struct device *dev = &hisi_ptt->pdev->dev;
	struct hisi_ptt_dma_buflet *buflet;
	int i, ret;

	/* Make sure the trace buffer is empty before allocating */
	if (!list_empty(&ctrl->trace_buf))
		hisi_ptt_free_trace_buf(hisi_ptt);

	for (i = 0; i < ctrl->buflet_nums; ++i) {
		buflet = kzalloc(sizeof(*buflet), GFP_KERNEL);
		if (!buflet) {
			ret = -ENOMEM;
			goto err;
		}
		buflet->addr = dma_alloc_coherent(dev, ctrl->buflet_size,
						  &buflet->dma, GFP_KERNEL);
		if (!buflet->addr) {
			kfree(buflet);
			ret = -ENOMEM;
			goto err;
		}

		buflet->index = i;
		buflet->size = ctrl->buflet_size;
		list_add_tail(&buflet->list, &ctrl->trace_buf);
	}

	return 0;

err:
	hisi_ptt_free_trace_buf(hisi_ptt);
	return ret;
}

static void hisi_ptt_trace_end(struct hisi_ptt *hisi_ptt)
{
	writel(0, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);
	hisi_ptt->trace_ctrl.status = HISI_PTT_TRACE_STATUS_OFF;
	hisi_ptt->trace_ctrl.cur = NULL;
}

static int hisi_ptt_trace_start(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	u32 val;
	int i;

	if (!hisi_ptt->cur_filter.val) {
		pci_err(hisi_ptt->pdev, "no filter specified\n");
		return -ENODEV;
	}

	if (!ctrl->tr_event) {
		pci_err(hisi_ptt->pdev, "no trace type specified\n");
		return -EINVAL;
	}

	/* Check device idle before start trace */
	if (hisi_ptt_wait_trace_hw_idle(hisi_ptt)) {
		pci_err(hisi_ptt->pdev, "the device is still busy\n");
		return -EBUSY;
	}

	/* Reset the DMA before start tracing */
	val = readl(hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);
	val |= HISI_PTT_TRACE_CTRL_RST;
	writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);

	/* Wait for the reset finished */
	msleep(HISI_PTT_RESET_WAIT_MS);

	val = readl(hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);
	val &= ~HISI_PTT_TRACE_CTRL_RST;
	writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);

	/* clear the interrupt status */
	writel(HISI_PTT_TRACE_INT_STAT_MASK,
	       hisi_ptt->iobase + HISI_PTT_TRACE_INT_STAT);
	writel(0, hisi_ptt->iobase + HISI_PTT_TRACE_INT_MASK_REG);

	for (i = 0; i < HISI_PTT_DMA_NUMS; ++i) {
		if (!ctrl->cur)
			ctrl->cur = list_first_entry(&ctrl->trace_buf,
						     struct hisi_ptt_dma_buflet,
						     list);
		else
			ctrl->cur = list_next_entry(ctrl->cur, list);

		writel(lower_32_bits(ctrl->cur->dma),
		       hisi_ptt->iobase + HISI_PTT_TRACE_ADDR_BASE_LO_0 +
		       i * HISI_PTT_TRACE_ADDR_STRIDE);
		writel(upper_32_bits(ctrl->cur->dma),
		       hisi_ptt->iobase + HISI_PTT_TRACE_ADDR_BASE_HI_0 +
		       i * HISI_PTT_TRACE_ADDR_STRIDE);
	}
	writel(ctrl->buflet_size, hisi_ptt->iobase + HISI_PTT_TRACE_ADDR_SIZE);

	/* set the trace control register */
	val = 0;
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_TYPE_SEL, ctrl->tr_event);
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_RXTX_SEL, ctrl->rxtx);
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_DATA_FORMAT, ctrl->tr_format);
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_FILTER_MODE,
			  hisi_ptt->cur_filter.is_port ? 0 : 1);
	val |= FIELD_PREP(HISI_PTT_TRACE_CTRL_TARGET_SEL,
			  hisi_ptt->cur_filter.val);

	val |= HISI_PTT_TRACE_CTRL_EN;
	writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_CTRL);

	ctrl->status = HISI_PTT_TRACE_STATUS_ON;

	return 0;
}

#define HISI_PTT_TRACE_ATTR(__name)					\
static int __name ## _open(struct inode *inode, struct file *filp)	\
{									\
	struct hisi_ptt_debugfs_file_desc *desc = inode->i_private;	\
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;			\
	if (!mutex_trylock(&hisi_ptt->mutex))				\
		return -EBUSY;						\
	return single_open(filp, __name ## _show, hisi_ptt);		\
}									\
static int __name ## _release(struct inode *inode, struct file *filp)	\
{									\
	struct hisi_ptt_debugfs_file_desc *desc = inode->i_private;	\
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;			\
	mutex_unlock(&hisi_ptt->mutex);					\
	return seq_release(inode, filp);				\
}									\
static const struct file_operations __name ## _fops = {			\
	.owner		= THIS_MODULE,					\
	.open		= __name ## _open,				\
	.read		= seq_read,					\
	.write		= __name ## _write,				\
	.llseek		= seq_lseek,					\
	.release	= __name ## _release,				\
}

static void hisi_ptt_print_filter_list(struct seq_file *m,
				       struct hisi_ptt *hisi_ptt,
				       struct list_head *filter_list,
				       bool port_filter)
{
	struct hisi_ptt_filter_desc *filter;

	list_for_each_entry(filter, filter_list, list) {
		struct pci_dev *pdev = filter->pdev;
		bool selected = false;

		if (port_filter && hisi_ptt->cur_filter.is_port)
			selected = filter->val & hisi_ptt->cur_filter.val;
		else
			selected = filter->val == hisi_ptt->cur_filter.val;

		seq_printf(m, "%s%04x:%02x:%02x.%u%s\n",
			   selected ? "[" : "",
			   pci_domain_nr(pdev->bus),
			   pdev->bus->number, PCI_SLOT(pdev->devfn),
			   PCI_FUNC(pdev->devfn),
			   selected ? "]" : "");
	}
}

static int hisi_ptt_set_filter_show(struct seq_file *m, void *v)
{
	struct hisi_ptt *hisi_ptt = m->private;

	if (list_empty(&hisi_ptt->port_filters)) {
		seq_puts(m, "#### No available filter ####\n");
		return 0;
	}

	seq_puts(m, "#### Root Ports ####\n");
	hisi_ptt_print_filter_list(m, hisi_ptt, &hisi_ptt->port_filters, true);

	if (list_empty(&hisi_ptt->req_filters))
		return 0;

	seq_puts(m, "#### Functions ####\n");
	hisi_ptt_print_filter_list(m, hisi_ptt, &hisi_ptt->req_filters, false);

	return 0;
}

static int hisi_ptt_parse_set_filter(struct hisi_ptt *hisi_ptt, const char *buf)
{
	struct hisi_ptt_filter_desc *filter;
	u32 domain, bus, dev, fn, devfn;
	struct list_head *filter_list;
	struct pci_dev *pdev;
	u16 filter_val;
	int num;

	/*
	 * the input should be like 0000:80:01.1, etc. Parse it
	 * and check whether it's in the available func list.
	 */
	num = sscanf(buf, "%04x:%02x:%02x.%u", &domain, &bus, &dev, &fn);
	if (num != 4)
		return -EINVAL;

	devfn = PCI_DEVFN(dev, fn);
	pdev = pci_get_domain_bus_and_slot(domain, bus, devfn);
	if (!pdev)
		return -EINVAL;

	filter_val = hisi_ptt_get_filter_val(pdev);
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT)
		filter_list = &hisi_ptt->port_filters;
	else
		filter_list = &hisi_ptt->req_filters;

	list_for_each_entry(filter, filter_list, list) {
		if (filter_val != filter->val)
			continue;

		if (hisi_ptt->cur_filter.is_port &&
		    pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT)
			hisi_ptt->cur_filter.val |= filter_val;
		else
			hisi_ptt->cur_filter.val = filter_val;

		hisi_ptt->cur_filter.is_port = pci_pcie_type(pdev) ==
					       PCI_EXP_TYPE_ROOT_PORT;

		pci_dev_put(pdev);
		return 0;
	}

	pci_dev_put(pdev);
	return -EINVAL;
}

static ssize_t hisi_ptt_set_filter_write(struct file *filp,
					 const char __user *buf,
					 size_t count, loff_t *pos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_ptt *hisi_ptt = m->private;
	char tbuf[HISI_PTT_CTRL_STR_LEN];

	if (list_empty(&hisi_ptt->port_filters))
		return -EINVAL;

	if (hisi_ptt_write_to_buffer(tbuf, HISI_PTT_CTRL_STR_LEN - 1,
				     pos, buf, count))
		return -EINVAL;

	if (!strlen(tbuf)) {
		hisi_ptt->cur_filter.val = 0;
		hisi_ptt->cur_filter.is_port = false;
		return count;
	}

	if (hisi_ptt_parse_set_filter(hisi_ptt, tbuf) < 0)
		return -EINVAL;

	return count;
}
HISI_PTT_TRACE_ATTR(hisi_ptt_set_filter);

static int hisi_ptt_set_direction_show(struct seq_file *m, void *v)
{
	struct hisi_ptt *hisi_ptt = m->private;
	int i;

	for (i = 0; i < ARRAY_SIZE(trace_rxtx); ++i) {
		bool selected = false;

		if (hisi_ptt->trace_ctrl.rxtx == trace_rxtx[i].event_code)
			selected = true;

		seq_printf(m, "%s%s%s  ",
			   selected ? "[" : "", trace_rxtx[i].name,
			   selected ? "]" : "");
	}
	seq_putc(m, '\n');

	return 0;
}

static ssize_t hisi_ptt_set_direction_write(struct file *filp,
					    const char __user *buf,
					    size_t count, loff_t *pos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_ptt *hisi_ptt = m->private;
	char tbuf[HISI_PTT_CTRL_STR_LEN];
	int i;

	if (hisi_ptt_write_to_buffer(tbuf, HISI_PTT_CTRL_STR_LEN - 1,
				     pos, buf, count))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(trace_rxtx); ++i) {
		if (!strcmp(tbuf, trace_rxtx[i].name)) {
			hisi_ptt->trace_ctrl.rxtx = trace_rxtx[i].event_code;
			return count;
		}
	}

	return -EINVAL;
}
HISI_PTT_TRACE_ATTR(hisi_ptt_set_direction);

static int hisi_ptt_set_trace_type_show(struct seq_file *m, void *v)
{
	struct hisi_ptt *hisi_ptt = m->private;
	bool select_all = false;
	int i;

	if (hisi_ptt->trace_ctrl.tr_event == trace_events[0].event_code)
		select_all = true;

	for (i = 0; i < ARRAY_SIZE(trace_events); ++i) {
		bool selected = false;

		if (((hisi_ptt->trace_ctrl.tr_event &
		      trace_events[i].event_code) &&
		     !select_all && i) || (i == 0 && select_all))
			selected = true;

		seq_printf(m, "%s%s%s  ",
			   selected ? "[" : "", trace_events[i].name,
			   selected ? "]" : "");
	}
	seq_putc(m, '\n');

	return 0;
}

static ssize_t hisi_ptt_set_trace_type_write(struct file *filp,
					     const char __user *buf,
					     size_t count, loff_t *pos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_ptt *hisi_ptt = m->private;
	char tbuf[HISI_PTT_CTRL_STR_LEN];
	int i;

	if (hisi_ptt_write_to_buffer(tbuf, HISI_PTT_CTRL_STR_LEN - 1,
				     pos, buf, count))
		return -EINVAL;

	if (!strlen(tbuf)) {
		hisi_ptt->trace_ctrl.tr_event = 0;
		return count;
	}

	for (i = 0; i < ARRAY_SIZE(trace_events); ++i) {
		if (!strcmp(tbuf, trace_events[i].name)) {
			hisi_ptt->trace_ctrl.tr_event |= trace_events[i].event_code;
			return count;
		}
	}

	return -EINVAL;
}
HISI_PTT_TRACE_ATTR(hisi_ptt_set_trace_type);

static int hisi_ptt_trace_on_get(void *data, u64 *val)
{
	struct hisi_ptt_debugfs_file_desc *desc = data;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;

	if (!mutex_trylock(&hisi_ptt->mutex))
		return -EBUSY;

	*val = hisi_ptt->trace_ctrl.status;

	mutex_unlock(&hisi_ptt->mutex);
	return 0;
}

static int hisi_ptt_trace_on_set(void *data, u64 val)
{
	struct hisi_ptt_debugfs_file_desc *desc = data;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;
	int ret = 0;

	if (!mutex_trylock(&hisi_ptt->mutex))
		return -EBUSY;

	if (val) {
		if (hisi_ptt->trace_ctrl.status == HISI_PTT_TRACE_STATUS_ON)
			goto out;

		ret = hisi_ptt_alloc_trace_buf(hisi_ptt);
		if (ret)
			goto out;

		ret = hisi_ptt_trace_start(hisi_ptt);
		if (ret) {
			hisi_ptt_free_trace_buf(hisi_ptt);
			goto out;
		}
	} else {
		if (hisi_ptt->trace_ctrl.status == HISI_PTT_TRACE_STATUS_OFF)
			goto out;

		hisi_ptt_trace_end(hisi_ptt);
	}

out:
	mutex_unlock(&hisi_ptt->mutex);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(hisi_ptt_trace_on_fops, hisi_ptt_trace_on_get,
			 hisi_ptt_trace_on_set, "%llu\n");

static int hisi_ptt_set_trace_buf_nums_get(void *data, u64 *val)
{
	struct hisi_ptt_debugfs_file_desc *desc = data;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;

	if (!mutex_trylock(&hisi_ptt->mutex))
		return -EBUSY;

	*val = hisi_ptt->trace_ctrl.buflet_nums;

	mutex_unlock(&hisi_ptt->mutex);
	return 0;
}

static int hisi_ptt_set_trace_buf_nums_set(void *data, u64 val)
{
	struct hisi_ptt_debugfs_file_desc *desc = data;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;

	if (val < HISI_PTT_DMA_NUMS) {
		pci_warn(hisi_ptt->pdev, "buflet numbers should be more than %d\n",
			 HISI_PTT_DMA_NUMS - 1);
		return -EINVAL;
	}

	if (!mutex_trylock(&hisi_ptt->mutex))
		return -EBUSY;

	hisi_ptt->trace_ctrl.buflet_nums = val;

	mutex_unlock(&hisi_ptt->mutex);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(hisi_ptt_set_trace_buf_nums_fops,
			 hisi_ptt_set_trace_buf_nums_get,
			 hisi_ptt_set_trace_buf_nums_set, "%llu\n");

static int hisi_ptt_set_trace_buflet_size_show(struct seq_file *m, void *v)
{
	struct hisi_ptt *hisi_ptt = m->private;
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	int i;

	for (i = 0; i < ARRAY_SIZE(available_buflet_size); ++i) {
		bool selected = false;

		if (ctrl->buflet_size == available_buflet_size[i])
			selected = true;

		seq_printf(m, "%s%uMiB%s  ",
			   selected ? "[" : "", available_buflet_size[i] >> 20,
			   selected ? "]" : "");
	}
	seq_putc(m, '\n');

	return 0;
}

static ssize_t hisi_ptt_set_trace_buflet_size_write(struct file *filp,
						    const char __user *buf,
						    size_t count, loff_t *pos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_ptt *hisi_ptt = m->private;
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	char tbuf[HISI_PTT_CTRL_STR_LEN];
	unsigned int size;
	int i;

	if (hisi_ptt_write_to_buffer(tbuf, HISI_PTT_CTRL_STR_LEN - 1,
				     pos, buf, count))
		return -EINVAL;

	if (kstrtouint(tbuf, 0, &size))
		return -EINVAL;

	size <<= 20;
	for (i = 0; i < ARRAY_SIZE(available_buflet_size); ++i) {
		if (available_buflet_size[i] == size) {
			ctrl->buflet_size = size;
			return count;
		}
	}

	return -EINVAL;
}
HISI_PTT_TRACE_ATTR(hisi_ptt_set_trace_buflet_size);

static int hisi_ptt_free_trace_buf_get(void *data, u64 *val)
{
	struct hisi_ptt_debugfs_file_desc *desc = data;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;

	if (!mutex_trylock(&hisi_ptt->mutex))
		return -EBUSY;

	*val = list_empty(&hisi_ptt->trace_ctrl.trace_buf);

	mutex_unlock(&hisi_ptt->mutex);
	return 0;
}

static int hisi_ptt_free_trace_buf_set(void *data, u64 val)
{
	struct hisi_ptt_debugfs_file_desc *desc = data;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;
	int ret = 0;

	if (!mutex_trylock(&hisi_ptt->mutex))
		return -EBUSY;

	if (!list_empty(&hisi_ptt->trace_ctrl.trace_buf)) {
		if (hisi_ptt->trace_ctrl.status == HISI_PTT_TRACE_STATUS_ON)
			ret = -EBUSY;
		else
			hisi_ptt_free_trace_buf(hisi_ptt);
	}

	mutex_unlock(&hisi_ptt->mutex);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(hisi_ptt_free_trace_buf_fops,
			 hisi_ptt_free_trace_buf_get,
			 hisi_ptt_free_trace_buf_set, "%llu\n");

static int hisi_ptt_trace_data_format_show(struct seq_file *m, void *v)
{
	struct hisi_ptt *hisi_ptt = m->private;
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;

	seq_printf(m, "%s\n", ctrl->tr_format ? "4DW [8DW]" : "[4DW] 8DW");

	return 0;
}

static ssize_t hisi_ptt_trace_data_format_write(struct file *filp,
						const char __user *buf,
						size_t count, loff_t *pos)
{
	struct seq_file *m = filp->private_data;
	struct hisi_ptt *hisi_ptt = m->private;
	struct hisi_ptt_trace_ctrl *ctrl = &hisi_ptt->trace_ctrl;
	char tbuf[HISI_PTT_CTRL_STR_LEN];
	u32 format;

	if (hisi_ptt_write_to_buffer(tbuf, HISI_PTT_CTRL_STR_LEN - 1,
				     pos, buf, count))
		return -EINVAL;

	if (kstrtouint(tbuf, 0, &format))
		return -EINVAL;

	/* The data format can only be 4DW or 8DW */
	if (format == 4)
		ctrl->tr_format = HISI_PTT_TRACE_DATA_4DW;
	else if (format == 8)
		ctrl->tr_format = HISI_PTT_TRACE_DATA_8DW;
	else
		return -EINVAL;

	return count;
}
HISI_PTT_TRACE_ATTR(hisi_ptt_trace_data_format);

static void *hisi_ptt_trace_data_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct hisi_ptt *hisi_ptt = m->private;
	struct hisi_ptt_dma_buflet *buflet = v;

	(*pos)++;

	if (!list_is_last(&buflet->list, &hisi_ptt->trace_ctrl.trace_buf))
		return list_next_entry(buflet, list);

	return NULL;
}

static void *hisi_ptt_trace_data_start(struct seq_file *m, loff_t *pos)
{
	struct hisi_ptt *hisi_ptt = m->private;
	struct hisi_ptt_dma_buflet *buflet;
	loff_t off = 0;

	buflet = list_first_entry(&hisi_ptt->trace_ctrl.trace_buf,
				  struct hisi_ptt_dma_buflet, list);
	while (off < *pos)
		buflet = hisi_ptt_trace_data_next(m, buflet, &off);

	return buflet;
}

static void hisi_ptt_trace_data_stop(struct seq_file *m, void *p)
{
	/* Nothing to do, only a stub */
}

static int hisi_ptt_trace_data_show(struct seq_file *m, void *v)
{
	struct hisi_ptt_dma_buflet *buflet = v;

	if (buflet)
		seq_write(m, buflet->addr, buflet->size);

	return 0;
}

/*
 * The operations of the seq_file will exist when the mutex of
 * hisi_ptt being held in the open(), and the mutex will be
 * released in the release().
 */
static const struct seq_operations hisi_ptt_trace_data_seq_ops = {
	.start	= hisi_ptt_trace_data_start,
	.next	= hisi_ptt_trace_data_next,
	.stop	= hisi_ptt_trace_data_stop,
	.show	= hisi_ptt_trace_data_show,
};

static int hisi_ptt_trace_data_open(struct inode *inode, struct file *filep)
{
	struct hisi_ptt_debugfs_file_desc *desc = inode->i_private;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;
	struct seq_file *m;
	int ret;

	if (!mutex_trylock(&hisi_ptt->mutex))
		return -EBUSY;

	/*
	 * Check the trace status, we cannot read the
	 * data if the trace is still on. Then hold the
	 * lock when reading the traced data.
	 */
	if (hisi_ptt->trace_ctrl.status == HISI_PTT_TRACE_STATUS_ON ||
	    hisi_ptt_wait_trace_hw_idle(hisi_ptt)) {
		ret = -EBUSY;
		goto err;
	}

	if (list_empty(&hisi_ptt->trace_ctrl.trace_buf)) {
		pci_info(hisi_ptt->pdev, "the trace buffer is empty\n");
		ret = -ENOTTY;
		goto err;
	}

	ret = seq_open(filep, &hisi_ptt_trace_data_seq_ops);
	if (ret)
		goto err;

	m = filep->private_data;
	m->private = hisi_ptt;

	return 0;

err:
	mutex_unlock(&hisi_ptt->mutex);
	return ret;
}

static ssize_t hisi_ptt_trace_data_write(struct file *filp,
					 const char __user *buf,
					 size_t count, loff_t *off)
{
	struct seq_file *m = filp->private_data;
	struct hisi_ptt *hisi_ptt = m->private;
	char tbuf[HISI_PTT_CTRL_STR_LEN];

	if (hisi_ptt_write_to_buffer(tbuf, HISI_PTT_CTRL_STR_LEN - 1,
				     off, buf, count))
		return -EINVAL;

	/* Free the trace buffer when `echo "" > trace_data` */
	if (!strlen(tbuf) && !list_empty(&hisi_ptt->trace_ctrl.trace_buf)) {
		if (hisi_ptt->trace_ctrl.status == HISI_PTT_TRACE_STATUS_ON)
			return -EBUSY;
		hisi_ptt_free_trace_buf(hisi_ptt);
	}

	return count;
}

static int hisi_ptt_trace_data_release(struct inode *inode, struct file *filp)
{
	struct hisi_ptt_debugfs_file_desc *desc = inode->i_private;
	struct hisi_ptt *hisi_ptt = desc->hisi_ptt;

	mutex_unlock(&hisi_ptt->mutex);

	return seq_release(inode, filp);
}

static const struct file_operations hisi_ptt_trace_data_fops = {
	.owner		= THIS_MODULE,
	.open		= hisi_ptt_trace_data_open,
	.read		= seq_read,
	.write		= hisi_ptt_trace_data_write,
	.llseek		= no_llseek,
	.release	= hisi_ptt_trace_data_release,
};

static struct hisi_ptt_debugfs_file_desc trace_entries[] = {
	PTT_FILE_INIT("filter",		&hisi_ptt_set_filter_fops),
	PTT_FILE_INIT("direction",	&hisi_ptt_set_direction_fops),
	PTT_FILE_INIT("type",		&hisi_ptt_set_trace_type_fops),
	PTT_FILE_INIT("data_format",	&hisi_ptt_trace_data_format_fops),
	PTT_FILE_INIT("trace_on",	&hisi_ptt_trace_on_fops),
	PTT_FILE_INIT("buf_nums",	&hisi_ptt_set_trace_buf_nums_fops),
	PTT_FILE_INIT("buflet_size",	&hisi_ptt_set_trace_buflet_size_fops),
	PTT_FILE_INIT("free_buffer",	&hisi_ptt_free_trace_buf_fops),
	PTT_FILE_INIT("data",		&hisi_ptt_trace_data_fops),
};

static irqreturn_t hisi_ptt_isr(int irq, void *context)
{
	struct hisi_ptt *hisi_ptt = context;
	struct hisi_ptt_dma_buflet *next, *cur;
	u32 status, val, buf_idx;

	status = readl(hisi_ptt->iobase + HISI_PTT_TRACE_INT_STAT);
	buf_idx = fls(status) - 1;

	/*
	 * Check whether the trace buffer is full. Stop tracing
	 * when the last DMA buffer is finished. Otherwise, assign
	 * the address of next buflet to the DMA register.
	 */
	cur = hisi_ptt->trace_ctrl.cur;
	if (list_is_last(&cur->list, &hisi_ptt->trace_ctrl.trace_buf)) {
		if ((status & HISI_PTT_TRACE_INT_STAT_MASK) ==
		    HISI_PTT_TRACE_INT_STAT_MASK)
			hisi_ptt_trace_end(hisi_ptt);

		val = readl(hisi_ptt->iobase + HISI_PTT_TRACE_INT_MASK_REG);
		val |= BIT(buf_idx);
		writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_INT_MASK_REG);
	} else {
		next = list_next_entry(cur, list);
		writel(lower_32_bits(next->dma), hisi_ptt->iobase +
				  HISI_PTT_TRACE_ADDR_BASE_LO_0 +
				  buf_idx * HISI_PTT_TRACE_ADDR_STRIDE);
		writel(upper_32_bits(next->dma), hisi_ptt->iobase +
					HISI_PTT_TRACE_ADDR_BASE_HI_0 +
					buf_idx * HISI_PTT_TRACE_ADDR_STRIDE);
		hisi_ptt->trace_ctrl.cur = next;
		val = BIT(buf_idx);
		writel(val, hisi_ptt->iobase + HISI_PTT_TRACE_INT_STAT);
	}

	return IRQ_HANDLED;
}

static irqreturn_t hisi_ptt_irq(int irq, void *context)
{
	struct hisi_ptt *hisi_ptt = context;
	u32 status;

	status = readl(hisi_ptt->iobase + HISI_PTT_TRACE_INT_STAT);
	if (status & HISI_PTT_TRACE_INT_STAT_MASK)
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static int hisi_ptt_irq_register(struct hisi_ptt *hisi_ptt)
{
	struct pci_dev *pdev = hisi_ptt->pdev;
	int ret;

	ret = pci_alloc_irq_vectors(pdev, HISI_PTT_IRQ_NUMS, HISI_PTT_IRQ_NUMS,
				    PCI_IRQ_MSI);
	if (ret < 0) {
		pci_err(pdev, "failed to allocate irq vector, ret = %d\n", ret);
		return ret;
	}

	ret = request_threaded_irq(pci_irq_vector(pdev, HISI_PTT_DMA_IRQ),
				   hisi_ptt_irq, hisi_ptt_isr, IRQF_SHARED,
				   "hisi-ptt", hisi_ptt);
	if (ret) {
		pci_err(pdev, "failed to request irq %d, ret = %d\n",
			pci_irq_vector(pdev, HISI_PTT_DMA_IRQ), ret);
		pci_free_irq_vectors(pdev);
		return ret;
	}

	return 0;
}

static void hisi_ptt_irq_unregister(struct hisi_ptt *hisi_ptt)
{
	struct pci_dev *pdev = hisi_ptt->pdev;

	free_irq(pci_irq_vector(pdev, HISI_PTT_DMA_IRQ), hisi_ptt);
	pci_free_irq_vectors(pdev);
}

static void hisi_ptt_update_filters(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct hisi_ptt_filter_update_info info;
	struct hisi_ptt_filter_desc *filter;
	struct list_head *target_list;
	struct hisi_ptt *hisi_ptt;

	hisi_ptt = container_of(delayed_work, struct hisi_ptt, work);

	if (!mutex_trylock(&hisi_ptt->mutex)) {
		schedule_delayed_work(&hisi_ptt->work, HISI_PTT_WORK_DELAY_MS);
		return;
	}

	while (kfifo_get(&hisi_ptt->filter_update_kfifo, &info)) {
		target_list = info.is_port ? &hisi_ptt->port_filters :
			      &hisi_ptt->req_filters;

		if (info.is_add) {
			filter = kzalloc(sizeof(*filter), GFP_KERNEL);
			if (!filter) {
				pci_err(hisi_ptt->pdev,
					"failed to update the filters\n");
				continue;
			}

			filter->pdev = info.pdev;
			filter->val = info.val;

			list_add_tail(&filter->list, target_list);
		} else {
			list_for_each_entry(filter, target_list, list)
				if (filter->val == info.val) {
					list_del(&filter->list);
					kfree(filter);
					break;
				}
		}
	}

	mutex_unlock(&hisi_ptt->mutex);
}

static void hisi_ptt_update_fifo_in(struct hisi_ptt *hisi_ptt,
				    struct hisi_ptt_filter_update_info *info)
{
	struct pci_dev *root_port = pcie_find_root_port(info->pdev);

	if (!root_port)
		return;

	info->port_devid = PCI_DEVID(root_port->bus->number, root_port->devfn);
	if (info->port_devid < hisi_ptt->lower ||
	    info->port_devid > hisi_ptt->upper)
			return;

	info->is_port = pci_pcie_type(info->pdev) == PCI_EXP_TYPE_ROOT_PORT;
	info->val = hisi_ptt_get_filter_val(info->pdev);

	if (kfifo_in_spinlocked(&hisi_ptt->filter_update_kfifo, info, 1,
				&hisi_ptt->filter_update_lock))
		schedule_delayed_work(&hisi_ptt->work, 0);
	else
		pci_warn(hisi_ptt->pdev,
			 "filter update fifo overflow for target %s\n",
			 pci_name(info->pdev));
}

/*
 * A PCI bus notifier is used here for dynamically updating the filter
 * list.
 */
static int hisi_ptt_notifier_call(struct notifier_block *nb,
				  unsigned long action,
				  void *data)
{
	struct hisi_ptt *hisi_ptt = container_of(nb, struct hisi_ptt, hisi_ptt_nb);
	struct hisi_ptt_filter_update_info info;
	struct device *dev = data;
	struct pci_dev *pdev = to_pci_dev(dev);

	info.pdev = pdev;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		info.is_add = true;
		break;
	case BUS_NOTIFY_DEL_DEVICE:
		info.is_add = false;
		break;
	default:
		return 0;
	}

	hisi_ptt_update_fifo_in(hisi_ptt, &info);

	return 0;
}

static int hisi_ptt_init_filters(struct pci_dev *pdev, void *data)
{
	struct hisi_ptt_filter_update_info info = {
		.pdev = pdev,
		.is_add = true,
	};
	struct hisi_ptt *hisi_ptt = data;

	hisi_ptt_update_fifo_in(hisi_ptt, &info);

	return 0;
}

static void hisi_ptt_release_filters(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_filter_desc *filter, *tfilter;

	list_for_each_entry_safe(filter, tfilter, &hisi_ptt->req_filters, list) {
		list_del(&filter->list);
		kfree(filter);
	}

	list_for_each_entry_safe(filter, tfilter, &hisi_ptt->port_filters, list) {
		list_del(&filter->list);
		kfree(filter);
	}
}

static void hisi_ptt_init_ctrls(struct hisi_ptt *hisi_ptt)
{
	struct pci_bus *bus;
	u32 reg;

	INIT_LIST_HEAD(&hisi_ptt->port_filters);
	INIT_LIST_HEAD(&hisi_ptt->req_filters);

	/*
	 * The device range register provides the information about the
	 * root ports which the RCiEP can control and trace. The RCiEP
	 * and the root ports it support are on the same PCIe core, with
	 * same domain number but maybe different bus number. The device
	 * range register will tell us which root ports we can support,
	 * Bit[31:16] indicates the upper BDF numbers of the root port,
	 * while Bit[15:0] indicates the lower.
	 */
	reg = readl(hisi_ptt->iobase + HISI_PTT_DEVICE_RANGE);
	hisi_ptt->upper = reg >> 16;
	hisi_ptt->lower = reg & 0xffff;

	bus = pci_find_bus(pci_domain_nr(hisi_ptt->pdev->bus),
			   PCI_BUS_NUM(hisi_ptt->upper));
	if (bus)
		pci_walk_bus(bus, hisi_ptt_init_filters, hisi_ptt);

	/* Initialize trace controls */
	INIT_LIST_HEAD(&hisi_ptt->trace_ctrl.trace_buf);
	hisi_ptt->trace_ctrl.buflet_nums = HISI_PTT_DEFAULT_TRACE_BUF_CNT;
	hisi_ptt->trace_ctrl.buflet_size = HISI_PTT_TRACE_DEFAULT_BUFLET_SIZE;
	hisi_ptt->trace_ctrl.rxtx = HISI_PTT_TRACE_DEFAULT_RXTX.event_code;
	hisi_ptt->trace_ctrl.tr_event = HISI_PTT_TRACE_DEFAULT_EVENT.event_code;
}

static int hisi_ptt_create_trace_entries(struct hisi_ptt *hisi_ptt)
{
	struct hisi_ptt_debugfs_file_desc *trace_files;
	struct dentry *dir;
	int i, ret = 0;

	dir = debugfs_create_dir("trace", hisi_ptt->debugfs_dir);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	trace_files = devm_kmemdup(&hisi_ptt->pdev->dev, trace_entries,
				   sizeof(trace_entries), GFP_KERNEL);
	if (IS_ERR(trace_files)) {
		ret = PTR_ERR(trace_files);
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(trace_entries); ++i) {
		struct dentry *file;

		trace_files[i].hisi_ptt = hisi_ptt;
		file = debugfs_create_file(trace_files[i].name, 0600,
					   dir, &trace_files[i],
					   trace_files[i].fops);
		if (IS_ERR(file)) {
			ret = PTR_ERR(file);
			goto err;
		}
	}

	return 0;

err:
	debugfs_remove_recursive(dir);
	return ret;
}

static int hisi_ptt_create_debugfs_entries(struct hisi_ptt *hisi_ptt)
{
	int ret;

	hisi_ptt->debugfs_dir = debugfs_create_dir(hisi_ptt->name,
						   hisi_ptt_debugfs_root);
	if (IS_ERR(hisi_ptt->debugfs_dir))
		return PTR_ERR(hisi_ptt->debugfs_dir);

	ret = hisi_ptt_create_trace_entries(hisi_ptt);
	if (ret) {
		pci_err(hisi_ptt->pdev, "failed to create debugfs entries\n");
		debugfs_remove_recursive(hisi_ptt->debugfs_dir);
		return ret;
	}

	return 0;
}

static int hisi_ptt_probe(struct pci_dev *pdev,
			  const struct pci_device_id *id)
{
	struct hisi_ptt *hisi_ptt;
	int ret;

	hisi_ptt = devm_kzalloc(&pdev->dev, sizeof(*hisi_ptt), GFP_KERNEL);
	if (!hisi_ptt)
		return -ENOMEM;

	mutex_init(&hisi_ptt->mutex);
	hisi_ptt->pdev = pdev;

	/*
	 * Lifetime of pci_dev is longer than hisi_ptt,
	 * so directly reference to the pci name string.
	 */
	hisi_ptt->name = pci_name(pdev);
	pci_set_drvdata(pdev, hisi_ptt);

	ret = pcim_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "failed to enable device, ret = %d\n", ret);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(2), hisi_ptt->name);
	if (ret) {
		pci_err(pdev, "failed to remap io memory, ret = %d\n", ret);
		return ret;
	}

	hisi_ptt->iobase = pcim_iomap_table(pdev)[2];

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		pci_err(pdev, "failed to set 64 bit dma mask, ret = %d\n", ret);
		return ret;
	}

	pci_set_master(pdev);
	ret = hisi_ptt_irq_register(hisi_ptt);
	if (ret)
		return ret;

	spin_lock_init(&hisi_ptt->filter_update_lock);
	INIT_DELAYED_WORK(&hisi_ptt->work, hisi_ptt_update_filters);
	INIT_KFIFO(hisi_ptt->filter_update_kfifo);

	hisi_ptt_init_ctrls(hisi_ptt);

	ret = hisi_ptt_create_debugfs_entries(hisi_ptt);
	if (ret) {
		hisi_ptt_irq_unregister(hisi_ptt);
		return ret;
	}

	/* Register the bus notifier for dynamically updating the filter list */
	hisi_ptt->hisi_ptt_nb.notifier_call = hisi_ptt_notifier_call;
	ret = bus_register_notifier(&pci_bus_type, &hisi_ptt->hisi_ptt_nb);
	if (ret)
		pci_warn(pdev, "failed to register notifier block, filter list cannot be updated dynamically\n");

	return 0;
}

void hisi_ptt_remove(struct pci_dev *pdev)
{
	struct hisi_ptt *hisi_ptt = pci_get_drvdata(pdev);

	bus_unregister_notifier(&pci_bus_type, &hisi_ptt->hisi_ptt_nb);
	debugfs_remove_recursive(hisi_ptt->debugfs_dir);

	/* Cancel any work that has been queued */
	cancel_delayed_work_sync(&hisi_ptt->work);

	/* Make sure trace is ended before free trace buffer */
	if (hisi_ptt->trace_ctrl.status == HISI_PTT_TRACE_STATUS_ON)
		hisi_ptt_trace_end(hisi_ptt);

	hisi_ptt_release_filters(hisi_ptt);
	hisi_ptt_free_trace_buf(hisi_ptt);
	hisi_ptt_irq_unregister(hisi_ptt);
}

static const struct pci_device_id hisi_ptt_id_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, 0xa12e) },
	{ }
};
MODULE_DEVICE_TABLE(pci, hisi_ptt_id_tbl);

static struct pci_driver hisi_ptt_driver = {
	.name = "hisi_ptt",
	.id_table = hisi_ptt_id_tbl,
	.probe = hisi_ptt_probe,
	.remove = hisi_ptt_remove,
};

static int hisi_ptt_register_debugfs(void)
{
	if (!debugfs_initialized()) {
		pr_err("failed to create debugfs directory: debugfs uninitialized\n");
		return -ENOENT;
	}

	hisi_ptt_debugfs_root = debugfs_create_dir("hisi_ptt", NULL);
	if (IS_ERR(hisi_ptt_debugfs_root)) {
		pr_err("failed to create debugfs directory.\n");
		return PTR_ERR(hisi_ptt_debugfs_root);
	}

	return 0;
}

static void hisi_ptt_unregister_debugfs(void)
{
	debugfs_remove_recursive(hisi_ptt_debugfs_root);
}

static int __init hisi_ptt_module_init(void)
{
	int ret;

	/* The driver cannot work without debugfs entry */
	ret = hisi_ptt_register_debugfs();
	if (ret)
		return ret;

	ret = pci_register_driver(&hisi_ptt_driver);
	if (ret) {
		pr_err("failed to register hisi ptt driver, ret = %d.\n", ret);
		hisi_ptt_unregister_debugfs();
		return ret;
	}

	return 0;
}

static void __exit hisi_ptt_module_exit(void)
{
	pci_unregister_driver(&hisi_ptt_driver);
	hisi_ptt_unregister_debugfs();
}

module_init(hisi_ptt_module_init);
module_exit(hisi_ptt_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yicong Yang <yangyicong@hisilicon.com>");
MODULE_DESCRIPTION("Driver for HiSilicon PCIe tune and trace device");
