// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 BayLibre SAS
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>

#include "remoteproc_internal.h"

#define SW_RST					(0x0000000C)
#define SW_RST_OCD_HALT_ON_RST			BIT(12)
#define SW_RST_IPU_D_RST			BIT(8)
#define SW_RST_IPU_B_RST			BIT(4)
#define CORE_CTRL				(0x00000110)
#define CORE_CTRL_PDEBUG_ENABLE			BIT(31)
#define CORE_CTRL_SRAM_64K_iMEM			(0x00 << 27)
#define CORE_CTRL_SRAM_96K_iMEM			(0x01 << 27)
#define CORE_CTRL_SRAM_128K_iMEM		(0x02 << 27)
#define CORE_CTRL_SRAM_192K_iMEM		(0x03 << 27)
#define CORE_CTRL_SRAM_256K_iMEM		(0x04 << 27)
#define CORE_CTRL_PBCLK_ENABLE			BIT(26)
#define CORE_CTRL_RUN_STALL			BIT(23)
#define CORE_CTRL_STATE_VECTOR_SELECT		BIT(19)
#define CORE_CTRL_PIF_GATED			BIT(17)
#define CORE_CTRL_NMI				BIT(0)
#define CORE_XTENSA_INT				(0x00000114)
#define CORE_CTL_XTENSA_INT			(0x00000118)
#define CORE_DEFAULT0				(0x0000013C)
#define CORE_DEFAULT0_QOS_SWAP_0		(0x00 << 28)
#define CORE_DEFAULT0_QOS_SWAP_1		(0x01 << 28)
#define CORE_DEFAULT0_QOS_SWAP_2		(0x02 << 28)
#define CORE_DEFAULT0_QOS_SWAP_3		(0x03 << 28)
#define CORE_DEFAULT0_ARUSER_USE_IOMMU		(0x10 << 23)
#define CORE_DEFAULT0_AWUSER_USE_IOMMU		(0x10 << 18)
#define CORE_DEFAULT1				(0x00000140)
#define CORE_DEFAULT0_ARUSER_IDMA_USE_IOMMU	(0x10 << 0)
#define CORE_DEFAULT0_AWUSER_IDMA_USE_IOMMU	(0x10 << 5)
#define CORE_DEFAULT2				(0x00000144)
#define CORE_DEFAULT2_DBG_EN			BIT(3)
#define CORE_DEFAULT2_NIDEN			BIT(2)
#define CORE_DEFAULT2_SPNIDEN			BIT(1)
#define CORE_DEFAULT2_SPIDEN			BIT(0)
#define CORE_XTENSA_ALTRESETVEC			(0x000001F8)

#define RSC_VENDOR_CARVEOUT			(RSC_VENDOR_START + 1)

#define APU_RESET_DELAY				(27)

struct mtk_apu_rproc {
	struct device *dev;
	void __iomem *base;
	int irq;
	struct clk_bulk_data clks[3];
	struct list_head mappings;

#ifdef CONFIG_MTK_APU_JTAG
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_jtag;
	bool jtag_enabled;
	struct mutex jtag_mutex;
#endif
};

static int mtk_apu_iommu_map(struct rproc *rproc, struct rproc_mem_entry *entry)
{
	struct mtk_apu_rproc *apu_rproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	struct rproc_mem_entry *mapping;
	struct iommu_domain *domain;
	int ret;
	u64 pa;

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;

	if (!entry->va)
		pa = entry->dma;
	else
		pa = rproc_va_to_pa(entry->va);

	if ((strcmp(entry->name, "vdev0vring0") == 0 ||
		strcmp(entry->name, "vdev0vring1") == 0)) {
		entry->va = memremap(entry->dma, entry->len, MEMREMAP_WC);
		if (IS_ERR_OR_NULL(entry->va)) {
			dev_err(dev, "Unable to map memory region: %pa+%lx\n",
				&entry->dma, entry->len);
			ret = PTR_ERR(mapping->va);
			goto free_mapping;
		}
		mapping->va = entry->va;
	}

	domain = iommu_get_domain_for_dev(dev);
	ret = iommu_map(domain, entry->da, pa, entry->len, entry->flags);
	if (ret) {
		dev_err(dev, "iommu_map failed: %d\n", ret);
		goto err_memunmap;
	}

	mapping->da = entry->da;
	mapping->len = entry->len;
	list_add_tail(&mapping->node, &apu_rproc->mappings);

	return 0;

err_memunmap:
	memunmap(mapping->va);
free_mapping:
	kfree(mapping);

	return ret;
}

static void mtk_apu_iommu_unmap_all(struct rproc *rproc)
{
	struct mtk_apu_rproc *apu_rproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	struct rproc_mem_entry *entry, *tmp;
	struct iommu_domain *domain;

	/* clean up iommu mapping entries */
	list_for_each_entry_safe(entry, tmp, &apu_rproc->mappings, node) {
		size_t unmapped;

		domain = iommu_get_domain_for_dev(dev);
		unmapped = iommu_unmap(domain, entry->da, entry->len);
		if (unmapped != entry->len) {
			/* nothing much to do besides complaining */
			dev_err(dev, "failed to unmap %zx/%zu\n", entry->len,
				unmapped);
		}
		memunmap(entry->va);

		list_del(&entry->node);
		kfree(entry);
	}
}

static int mtk_apu_rproc_prepare(struct rproc *rproc)
{
	struct mtk_apu_rproc *apu_rproc = rproc->priv;
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(apu_rproc->clks),
				      apu_rproc->clks);
	if (ret)
		dev_err(apu_rproc->dev, "Failed to enable clocks\n");

	return ret;
}

static int mtk_apu_rproc_unprepare(struct rproc *rproc)
{
	struct mtk_apu_rproc *apu_rproc = rproc->priv;

	clk_bulk_disable_unprepare(ARRAY_SIZE(apu_rproc->clks),
				   apu_rproc->clks);

	return 0;
}

static int mtk_apu_rproc_start(struct rproc *rproc)
{
	struct mtk_apu_rproc *apu_rproc = rproc->priv;
	u32 core_ctrl;

	/* Set reset vector of APU firmware boot address */
	writel(rproc->bootaddr, apu_rproc->base + CORE_XTENSA_ALTRESETVEC);

	/* Turn on the clocks and stall the APU */
	core_ctrl = readl(apu_rproc->base + CORE_CTRL);
	core_ctrl |= CORE_CTRL_PDEBUG_ENABLE | CORE_CTRL_PBCLK_ENABLE |
		     CORE_CTRL_STATE_VECTOR_SELECT | CORE_CTRL_RUN_STALL |
		     CORE_CTRL_PIF_GATED;
	writel(core_ctrl, apu_rproc->base + CORE_CTRL);

	/* Reset the APU: this requires 27 ns to be effective on any platform */
	writel(SW_RST_OCD_HALT_ON_RST | SW_RST_IPU_B_RST | SW_RST_IPU_D_RST,
		apu_rproc->base + SW_RST);
	ndelay(APU_RESET_DELAY);
	writel(0, apu_rproc->base + SW_RST);

	core_ctrl &= ~CORE_CTRL_PIF_GATED;
	writel(core_ctrl, apu_rproc->base + CORE_CTRL);

	/* Configure memory accesses to go through the IOMMU */
	writel(CORE_DEFAULT0_AWUSER_USE_IOMMU | CORE_DEFAULT0_ARUSER_USE_IOMMU |
	      CORE_DEFAULT0_QOS_SWAP_1, apu_rproc->base + CORE_DEFAULT0);
	writel(CORE_DEFAULT0_AWUSER_IDMA_USE_IOMMU |
		CORE_DEFAULT0_ARUSER_IDMA_USE_IOMMU,
		apu_rproc->base + CORE_DEFAULT1);

	/* Release the APU */
	core_ctrl &= ~CORE_CTRL_RUN_STALL;
	writel(core_ctrl, apu_rproc->base + CORE_CTRL);

	return 0;
}

static int mtk_apu_rproc_stop(struct rproc *rproc)
{
	struct mtk_apu_rproc *apu_rproc = rproc->priv;
	u32 core_ctrl;

	core_ctrl = readl(apu_rproc->base + CORE_CTRL);
	writel(core_ctrl | CORE_CTRL_RUN_STALL, apu_rproc->base + CORE_CTRL);

	mtk_apu_iommu_unmap_all(rproc);

	return 0;
}

static void mtk_apu_rproc_kick(struct rproc *rproc, int vqid)
{
	struct mtk_apu_rproc *apu_rproc = rproc->priv;

	writel(1 << vqid, apu_rproc->base + CORE_CTL_XTENSA_INT);
}

static int mtk_apu_load(struct rproc *rproc, const struct firmware *fw)

{
	struct rproc_mem_entry *entry, *tmp;
	int ret;

	ret = rproc_elf_load_segments(rproc, fw);
	if (ret)
		return ret;

	list_for_each_entry_safe(entry, tmp, &rproc->carveouts, node) {
		ret = mtk_apu_iommu_map(rproc, entry);
		if (ret)
			goto err_unmap_all;
	}

	return 0;

err_unmap_all:
	mtk_apu_iommu_unmap_all(rproc);

	return ret;
}

static struct reserved_mem *of_reserved_mem_by_name(struct rproc *rproc,
						    const char *name)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct device_node *target;
	struct reserved_mem *rmem;
	int idx;

	idx = of_property_match_string(np, "memory-region-names", name);
	if (idx < 0) {
		dev_err(dev, "failed to find %s memory\n", name);
		return NULL;
	}

	target = of_parse_phandle(np, "memory-region", idx);
	if (!target)
		return NULL;

	rmem = of_reserved_mem_lookup(target);
	if (!rmem)
		dev_err(dev, "unable to acquire memory-region\n");
	of_node_put(target);

	return rmem;
}

static int mtk_apu_handle_rsc(struct rproc *rproc, u32 rsc_type, void *rsc,
			      int offset, int avail)
{
	struct device *dev = rproc->dev.parent;

	if (rsc_type == RSC_VENDOR_CARVEOUT) {
		struct fw_rsc_carveout *rsc_carveout = rsc;
		struct rproc_mem_entry *mem;
		struct reserved_mem *rmem;

		rmem = of_reserved_mem_by_name(rproc, rsc_carveout->name);
		if (!rmem)
			return -ENOMEM;

		if (rmem->size < rsc_carveout->len) {
			dev_err(dev, "The reserved memory is too small\n");
			return -ENOMEM;
		}

		mem = rproc_mem_entry_init(dev, NULL, (dma_addr_t)rmem->base,
					   rsc_carveout->len, rsc_carveout->da,
					   NULL, NULL, rsc_carveout->name);
		if (!mem)
			return -ENOMEM;

		mem->flags = rsc_carveout->flags;
		rsc_carveout->pa = rmem->base;
		rproc_add_carveout(rproc, mem);
	}

	return RSC_HANDLED;
}

static const struct rproc_ops mtk_apu_rproc_ops = {
	.prepare		= mtk_apu_rproc_prepare,
	.unprepare		= mtk_apu_rproc_unprepare,
	.start			= mtk_apu_rproc_start,
	.stop			= mtk_apu_rproc_stop,
	.kick			= mtk_apu_rproc_kick,
	.load			= mtk_apu_load,
	.parse_fw		= rproc_elf_load_rsc_table,
	.find_loaded_rsc_table	= rproc_elf_find_loaded_rsc_table,
	.sanity_check		= rproc_elf_sanity_check,
	.get_boot_addr		= rproc_elf_get_boot_addr,
	.handle_rsc		= mtk_apu_handle_rsc,
};

static irqreturn_t mtk_apu_rproc_callback(int irq, void *data)
{
	struct rproc *rproc = data;
	struct mtk_apu_rproc *apu_rproc = (struct mtk_apu_rproc *)rproc->priv;

	writel(1, apu_rproc->base + CORE_XTENSA_INT);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t handle_event(int irq, void *data)
{
	struct rproc *rproc = data;

	rproc_vq_interrupt(rproc, 0);
	rproc_vq_interrupt(rproc, 1);

	return IRQ_HANDLED;
}

#ifdef CONFIG_MTK_APU_JTAG

static int apu_enable_jtag(struct mtk_apu_rproc *apu_rproc)
{
	int ret = 0;

	mutex_lock(&apu_rproc->jtag_mutex);
	if (apu_rproc->jtag_enabled)
		goto err_mutex_unlock;

	writel(CORE_DEFAULT2_SPNIDEN | CORE_DEFAULT2_SPIDEN |
		CORE_DEFAULT2_NIDEN | CORE_DEFAULT2_DBG_EN,
		apu_rproc->base + CORE_DEFAULT2);

	apu_rproc->jtag_enabled = 1;

err_mutex_unlock:
	mutex_unlock(&apu_rproc->jtag_mutex);

	return ret;
}

static int apu_disable_jtag(struct mtk_apu_rproc *apu_rproc)
{
	int ret = 0;

	mutex_lock(&apu_rproc->jtag_mutex);
	if (!apu_rproc->jtag_enabled)
		goto err_mutex_unlock;

	writel(0, apu_rproc->base + CORE_DEFAULT2);

	apu_rproc->jtag_enabled = 0;

err_mutex_unlock:
	mutex_unlock(&apu_rproc->jtag_mutex);

	return ret;
}

static ssize_t rproc_jtag_read(struct file *filp, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	struct mtk_apu_rproc *apu_rproc = (struct mtk_apu_rproc *)rproc->priv;
	char *buf = apu_rproc->jtag_enabled ? "enabled\n" : "disabled\n";

	return simple_read_from_buffer(userbuf, count, ppos, buf, strlen(buf));
}

static ssize_t rproc_jtag_write(struct file *filp, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct rproc *rproc = filp->private_data;
	struct mtk_apu_rproc *apu_rproc = (struct mtk_apu_rproc *)rproc->priv;
	char buf[10];
	int ret;

	if (count < 1 || count > sizeof(buf))
		return -EINVAL;

	ret = copy_from_user(buf, user_buf, count);
	if (ret)
		return -EFAULT;

	/* remove end of line */
	if (buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	if (!strncmp(buf, "enabled", count))
		ret = apu_enable_jtag(apu_rproc);
	else if (!strncmp(buf, "disabled", count))
		ret = apu_disable_jtag(apu_rproc);
	else
		return -EINVAL;

	return ret ? ret : count;
}

static const struct file_operations rproc_jtag_ops = {
	.read = rproc_jtag_read,
	.write = rproc_jtag_write,
	.open = simple_open,
};

static int apu_jtag_probe(struct mtk_apu_rproc *apu_rproc)
{
	int ret;

	if (!apu_rproc->rproc->dbg_dir)
		return -ENODEV;

	apu_rproc->pinctrl = devm_pinctrl_get(apu_rproc->dev);
	if (IS_ERR(apu_rproc->pinctrl)) {
		dev_warn(apu_rproc->dev, "Failed to find JTAG pinctrl\n");
		return PTR_ERR(apu_rproc->pinctrl);
	}

	apu_rproc->pinctrl_jtag = pinctrl_lookup_state(apu_rproc->pinctrl,
						       "jtag");
	if (IS_ERR(apu_rproc->pinctrl_jtag))
		return PTR_ERR(apu_rproc->pinctrl_jtag);

	ret = pinctrl_select_state(apu_rproc->pinctrl,
				   apu_rproc->pinctrl_jtag);
	if (ret < 0)
		return ret;

	mutex_init(&apu_rproc->jtag_mutex);

	debugfs_create_file("jtag", 0600, apu_rproc->rproc->dbg_dir,
			    apu_rproc->rproc, &rproc_jtag_ops);

	return 0;
}
#else
static int apu_jtag_probe(struct mtk_apu_rproc *apu_rproc)
{
	return 0;
}

static int apu_disable_jtag(struct mtk_apu_rproc *apu_rproc)
{
	return 0;
}
#endif /* CONFIG_MTK_APU_JTAG */

static int mtk_apu_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_apu_rproc *apu_rproc;
	struct rproc *rproc;
	struct resource *res;
	int ret;

	rproc = rproc_alloc(dev, dev_name(dev), &mtk_apu_rproc_ops, NULL,
			    sizeof(*apu_rproc));
	if (!rproc)
		return -ENOMEM;

	rproc->recovery_disabled = true;
	rproc->has_iommu = false;
	rproc->auto_boot = false;

	apu_rproc = rproc->priv;
	apu_rproc->dev = dev;
	INIT_LIST_HEAD(&apu_rproc->mappings);

	platform_set_drvdata(pdev, rproc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	apu_rproc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(apu_rproc->base)) {
		dev_err(dev, "Failed to map mmio\n");
		ret = PTR_ERR(apu_rproc->base);
		goto free_rproc;
	}

	apu_rproc->irq = platform_get_irq(pdev, 0);
	if (apu_rproc->irq < 0) {
		ret = apu_rproc->irq;
		goto free_rproc;
	}

	ret = devm_request_threaded_irq(dev, apu_rproc->irq,
					mtk_apu_rproc_callback, handle_event,
					IRQF_SHARED | IRQF_ONESHOT,
					NULL, rproc);
	if (ret) {
		dev_err(dev, "devm_request_threaded_irq error: %d\n", ret);
		goto free_rproc;
	}

	apu_rproc->clks[0].id = "ipu";
	apu_rproc->clks[1].id = "axi";
	apu_rproc->clks[2].id = "jtag";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(apu_rproc->clks),
				apu_rproc->clks);
	if (ret) {
		dev_err(dev, "Failed to get clocks\n");
		goto free_rproc;
	}

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed: %d\n", ret);
		goto free_rproc;
	}

	ret = apu_jtag_probe(apu_rproc);
	if (ret)
		dev_warn(dev, "Failed to configure jtag\n");

	return 0;

free_rproc:
	rproc_free(rproc);

	return ret;
}

static int mtk_apu_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct mtk_apu_rproc *apu_rproc = (struct mtk_apu_rproc *)rproc->priv;
	struct device *dev = &pdev->dev;

	disable_irq(apu_rproc->irq);
	apu_disable_jtag(apu_rproc);
	rproc_del(rproc);
	of_reserved_mem_device_release(dev);
	rproc_free(rproc);

	return 0;
}

static const struct of_device_id mtk_apu_rproc_of_match[] = {
	{ .compatible = "mediatek,mt8183-apu", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_apu_rproc_of_match);

static struct platform_driver mtk_apu_rproc_driver = {
	.probe = mtk_apu_rproc_probe,
	.remove = mtk_apu_rproc_remove,
	.driver = {
		.name = "mtk_apu-rproc",
		.of_match_table = of_match_ptr(mtk_apu_rproc_of_match),
	},
};
module_platform_driver(mtk_apu_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alexandre Bailon");
MODULE_DESCRIPTION("MTK APU Remote Processor control driver");
