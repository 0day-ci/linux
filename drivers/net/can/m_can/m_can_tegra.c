// SPDX-License-Identifier: GPL-2.0
// IOMapped CAN bus driver for Bosch M_CAN controller on NVIDIA Tegra

#include <linux/platform_device.h>
#include <linux/reset.h>

#include "m_can.h"

struct m_can_tegra_priv {
	struct m_can_classdev cdev;

	void __iomem *base;
	void __iomem *mram_base;
	void __iomem *glue_base;
	// cclk is core_clk if it exists, otherwise can_clk.
	struct clk *core_clk, *can_clk, *pll_clk;
};

static inline struct m_can_tegra_priv *cdev_to_priv(struct m_can_classdev *cdev)
{
	return container_of(cdev, struct m_can_tegra_priv, cdev);
}

static u32 iomap_read_reg(struct m_can_classdev *cdev, int reg)
{
	struct m_can_tegra_priv *priv = cdev_to_priv(cdev);

	return readl(priv->base + reg);
}

static u32 iomap_read_fifo(struct m_can_classdev *cdev, int offset)
{
	struct m_can_tegra_priv *priv = cdev_to_priv(cdev);

	return readl(priv->mram_base + offset);
}

static u32 iomap_read_glue(struct m_can_classdev *cdev, int reg)
{
	struct m_can_tegra_priv *priv = cdev_to_priv(cdev);

	return readl(priv->glue_base + reg);
}

static int iomap_write_reg(struct m_can_classdev *cdev, int reg, int val)
{
	struct m_can_tegra_priv *priv = cdev_to_priv(cdev);

	writel(val, priv->base + reg);

	return 0;
}

static int iomap_write_fifo(struct m_can_classdev *cdev, int offset, int val)
{
	struct m_can_tegra_priv *priv = cdev_to_priv(cdev);

	writel(val, priv->mram_base + offset);

	return 0;
}

static int iomap_write_glue(struct m_can_classdev *cdev, int reg, int val)
{
	struct m_can_tegra_priv *priv = cdev_to_priv(cdev);

	writel(val, priv->glue_base + reg);

	return 0;
}

static struct m_can_ops m_can_tegra_ops = {
	.read_reg = iomap_read_reg,
	.write_reg = iomap_write_reg,
	.write_fifo = iomap_write_fifo,
	.read_fifo = iomap_read_fifo,
};

/* Glue logic apperature */
#define ADDR_M_TTCAN_IR          0x00
#define ADDR_M_TTCAN_TTIR        0x04
#define ADDR_M_TTCAN_TXBRP       0x08
#define ADDR_M_TTCAN_FD_DATA     0x0C
#define ADDR_M_TTCAN_STATUS_REG  0x10
#define ADDR_M_TTCAN_CNTRL_REG   0x14
#define ADDR_M_TTCAN_DMA_INTF0   0x18
#define ADDR_M_TTCAN_CLK_STOP    0x1C
#define ADDR_M_TTCAN_HSM_MASK0   0x20
#define ADDR_M_TTCAN_HSM_MASK1   0x24
#define ADDR_M_TTCAN_EXT_SYC_SLT 0x28
#define ADDR_M_TTCAN_HSM_SW_OVRD 0x2C
#define ADDR_M_TTCAN_TIME_STAMP  0x30

#define M_TTCAN_CNTRL_REG_COK           (1<<3)
#define M_TTCAN_TIME_STAMP_OFFSET_SEL   4

static void tegra_can_set_ok(struct m_can_classdev *cdev)
{
	u32 val;

	val = iomap_read_glue(cdev, ADDR_M_TTCAN_CNTRL_REG);
	val |= M_TTCAN_CNTRL_REG_COK;
	iomap_write_glue(cdev, ADDR_M_TTCAN_CNTRL_REG, val);
}


static int m_can_tegra_probe(struct platform_device *pdev)
{
	struct m_can_classdev *mcan_class;
	struct m_can_tegra_priv *priv;
	struct resource *res;
	void __iomem *addr;
	void __iomem *mram_addr;
	void __iomem *glue_addr;
	struct reset_control *rstc;
	struct clk *host_clk = NULL, *can_clk = NULL, *core_clk = NULL, *pclk = NULL;
	int irq, ret = 0;
	u32 rate;
	unsigned long new_rate;

	mcan_class = m_can_class_allocate_dev(&pdev->dev,
					      sizeof(struct m_can_tegra_priv));
	if (!mcan_class)
		return -ENOMEM;

	priv = cdev_to_priv(mcan_class);

	host_clk = devm_clk_get(&pdev->dev, "can_host");
	if (IS_ERR(host_clk)) {
		ret = PTR_ERR(host_clk);
		goto probe_fail;
	}
	can_clk = devm_clk_get(&pdev->dev, "can");
	if (IS_ERR(can_clk)) {
		ret = PTR_ERR(can_clk);
		goto probe_fail;
	}

	core_clk = devm_clk_get(&pdev->dev, "can_core");
	if (IS_ERR(core_clk)) {
		core_clk = NULL;
	}

	pclk = clk_get(&pdev->dev, "pll");
	if (IS_ERR(pclk)) {
		ret = PTR_ERR(pclk);
		goto probe_fail;
	}

	ret = clk_set_parent(can_clk, pclk);
	if (ret) {
		goto probe_fail;
	}

	ret = fwnode_property_read_u32(dev_fwnode(&pdev->dev), "can-clk-rate", &rate);
	if (ret) {
		goto probe_fail;
	}

	new_rate = clk_round_rate(can_clk, rate);
	if (!new_rate)
		dev_warn(&pdev->dev, "incorrect CAN clock rate\n");

	ret = clk_set_rate(can_clk, new_rate > 0 ? new_rate : rate);
	if (ret) {
		goto probe_fail;
	}

	ret = clk_set_rate(host_clk, new_rate > 0 ? new_rate : rate);
	if (ret) {
		goto probe_fail;
	}

	if (core_clk) {
		ret = fwnode_property_read_u32(dev_fwnode(&pdev->dev), "core-clk-rate", &rate);
		if (ret) {
			goto probe_fail;
		}
		new_rate = clk_round_rate(core_clk, rate);
		if (!new_rate)
			dev_warn(&pdev->dev, "incorrect CAN_CORE clock rate\n");

		ret = clk_set_rate(core_clk, new_rate > 0 ? new_rate : rate);
		if (ret) {
			goto probe_fail;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "m_can");
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto probe_fail;
	}

	irq = platform_get_irq_byname(pdev, "int0");
	if (irq < 0) {
		ret = -ENODEV;
		goto probe_fail;
	}

	/* message ram could be shared */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "message_ram");
	mram_addr = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!mram_addr) {
		ret = -ENOMEM;
		goto probe_fail;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "glue_regs");
	glue_addr = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!glue_addr) {
		ret = -ENOMEM;
		goto probe_fail;
	}

	rstc = devm_reset_control_get(&pdev->dev, "can");
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		goto probe_fail;
	}
	reset_control_reset(rstc);

	priv->can_clk = can_clk;
	mcan_class->hclk = host_clk;

	if (core_clk) {
		mcan_class->cclk = core_clk;
	} else {
		mcan_class->cclk = can_clk;
	}
	priv->core_clk = core_clk;

	priv->base = addr;
	priv->mram_base = mram_addr;
	priv->glue_base = glue_addr;

	mcan_class->net->irq = irq;
	mcan_class->pm_clock_support = 1;
	mcan_class->can.clock.freq = clk_get_rate(mcan_class->cclk);
	mcan_class->dev = &pdev->dev;

	mcan_class->ops = &m_can_tegra_ops;

	mcan_class->is_peripheral = false;

	platform_set_drvdata(pdev, mcan_class);

	pm_runtime_enable(mcan_class->dev);

	ret = pm_runtime_get_sync(mcan_class->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(mcan_class->dev);
		goto out_runtime_disable;
	}
	tegra_can_set_ok(mcan_class);
	m_can_init_ram(mcan_class);
	pm_runtime_put_sync(mcan_class->dev);

	ret = m_can_class_register(mcan_class);
	if (ret)
		goto out_runtime_disable;

	return ret;

out_runtime_disable:
	pm_runtime_disable(mcan_class->dev);
probe_fail:
	m_can_class_free_dev(mcan_class->net);
	return ret;
}

static __maybe_unused int m_can_suspend(struct device *dev)
{
	return m_can_class_suspend(dev);
}

static __maybe_unused int m_can_resume(struct device *dev)
{
	return m_can_class_resume(dev);
}

static int m_can_tegra_remove(struct platform_device *pdev)
{
	struct m_can_tegra_priv *priv = platform_get_drvdata(pdev);
	struct m_can_classdev *mcan_class = &priv->cdev;

	m_can_class_unregister(mcan_class);

	m_can_class_free_dev(mcan_class->net);

	return 0;
}

static int __maybe_unused m_can_runtime_suspend(struct device *dev)
{
	struct m_can_tegra_priv *priv = dev_get_drvdata(dev);
	struct m_can_classdev *mcan_class = &priv->cdev;

	if (priv->core_clk)
		clk_disable_unprepare(priv->core_clk);

	clk_disable_unprepare(mcan_class->hclk);
	clk_disable_unprepare(priv->can_clk);

	return 0;
}

static int __maybe_unused m_can_runtime_resume(struct device *dev)
{
	struct m_can_tegra_priv *priv = dev_get_drvdata(dev);
	struct m_can_classdev *mcan_class = &priv->cdev;
	int err;

	err = clk_prepare_enable(priv->can_clk);
	if (err) {
		return err;
	}

	err = clk_prepare_enable(mcan_class->hclk);
	if (err) {
		clk_disable_unprepare(priv->can_clk);
	}

	if (priv->core_clk) {
		err = clk_prepare_enable(priv->core_clk);
		if (err) {
			clk_disable_unprepare(mcan_class->hclk);
			clk_disable_unprepare(priv->can_clk);
		}
	}

	return err;
}

static const struct dev_pm_ops m_can_pmops = {
	SET_RUNTIME_PM_OPS(m_can_runtime_suspend,
			   m_can_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(m_can_suspend, m_can_resume)
};

static const struct of_device_id m_can_of_table[] = {
	{ .compatible = "nvidia,tegra194-m_can", .data = NULL },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, m_can_of_table);

static struct platform_driver m_can_tegra_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = m_can_of_table,
		.pm     = &m_can_pmops,
	},
	.probe = m_can_tegra_probe,
	.remove = m_can_tegra_remove,
};

module_platform_driver(m_can_tegra_driver);

MODULE_AUTHOR("Brian Silverman <brian.silverman@bluerivertech.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("M_CAN driver for IO Mapped Bosch controllers on NVIDIA Tegra");
