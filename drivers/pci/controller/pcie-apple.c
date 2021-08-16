// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host bridge driver for Apple system-on-chips.
 *
 * The HW is ECAM compliant, so once the controller is initialized, the driver
 * mostly only needs MSI handling. Initialization requires enabling power and
 * clocks, along with a number of register pokes.
 *
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2021 Google LLC
 * Copyright (C) 2021 Corellium LLC
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 *
 * Author: Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Author: Marc Zyngier <maz@kernel.org>
 */

#include <linux/kernel.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/pci-ecam.h>
#include <linux/iopoll.h>
#include <linux/gpio/consumer.h>

#define CORE_RC_PHYIF_CTL		0x00024
#define   CORE_RC_PHYIF_CTL_RUN		BIT(0)
#define CORE_RC_PHYIF_STAT		0x00028
#define   CORE_RC_PHYIF_STAT_REFCLK	BIT(4)
#define CORE_RC_CTL			0x00050
#define   CORE_RC_CTL_RUN		BIT(0)
#define CORE_RC_STAT			0x00058
#define   CORE_RC_STAT_READY		BIT(0)
#define CORE_FABRIC_STAT		0x04000
#define   CORE_FABRIC_STAT_MASK		0x001F001F
#define CORE_LANE_CFG(port)		(0x84000 + 0x4000 * (port))
#define   CORE_LANE_CFG_REFCLK0REQ	BIT(0)
#define   CORE_LANE_CFG_REFCLK1		BIT(1)
#define   CORE_LANE_CFG_REFCLK0ACK	BIT(2)
#define   CORE_LANE_CFG_REFCLKEN	(BIT(9) | BIT(10))
#define CORE_LANE_CTL(port)		(0x84004 + 0x4000 * (port))
#define   CORE_LANE_CTL_CFGACC		BIT(15)

#define PORT_LTSSMCTL			0x00080
#define   PORT_LTSSMCTL_START		BIT(0)
#define PORT_INTSTAT			0x00100
#define   PORT_INT_TUNNEL_ERR		BIT(31)
#define   PORT_INT_CPL_TIMEOUT		BIT(23)
#define   PORT_INT_RID2SID_MAPERR	BIT(22)
#define   PORT_INT_CPL_ABORT		BIT(21)
#define   PORT_INT_MSI_BAD_DATA		BIT(19)
#define   PORT_INT_MSI_ERR		BIT(18)
#define   PORT_INT_REQADDR_GT32		BIT(17)
#define   PORT_INT_AF_TIMEOUT		BIT(15)
#define   PORT_INT_LINK_DOWN		BIT(14)
#define   PORT_INT_LINK_UP		BIT(12)
#define   PORT_INT_LINK_BWMGMT		BIT(11)
#define   PORT_INT_AER_MASK		(15 << 4)
#define   PORT_INT_PORT_ERR		BIT(4)
#define   PORT_INT_INTx(i)		BIT(i)
#define   PORT_INT_INTxALL		15
#define PORT_INTMSK			0x00104
#define PORT_INTMSKSET			0x00108
#define PORT_INTMSKCLR			0x0010c
#define PORT_MSICFG			0x00124
#define   PORT_MSICFG_EN		BIT(0)
#define   PORT_MSICFG_L2MSINUM_SHIFT	4
#define PORT_MSIBASE			0x00128
#define   PORT_MSIBASE_1_SHIFT		16
#define PORT_MSIADDR			0x00168
#define PORT_LINKSTS			0x00208
#define   PORT_LINKSTS_UP		BIT(0)
#define   PORT_LINKSTS_BUSY		BIT(2)
#define PORT_LINKCMDSTS			0x00210
#define PORT_OUTS_NPREQS		0x00284
#define   PORT_OUTS_NPREQS_REQ		BIT(24)
#define   PORT_OUTS_NPREQS_CPL		BIT(16)
#define PORT_RXWR_FIFO			0x00288
#define   PORT_RXWR_FIFO_HDR		GENMASK(15, 10)
#define   PORT_RXWR_FIFO_DATA		GENMASK(9, 0)
#define PORT_RXRD_FIFO			0x0028C
#define   PORT_RXRD_FIFO_REQ		GENMASK(6, 0)
#define PORT_OUTS_CPLS			0x00290
#define   PORT_OUTS_CPLS_SHRD		GENMASK(14, 8)
#define   PORT_OUTS_CPLS_WAIT		GENMASK(6, 0)
#define PORT_APPCLK			0x00800
#define   PORT_APPCLK_EN		BIT(0)
#define   PORT_APPCLK_CGDIS		BIT(8)
#define PORT_STATUS			0x00804
#define   PORT_STATUS_READY		BIT(0)
#define PORT_REFCLK			0x00810
#define   PORT_REFCLK_EN		BIT(0)
#define   PORT_REFCLK_CGDIS		BIT(8)
#define PORT_PERST			0x00814
#define   PORT_PERST_OFF		BIT(0)
#define PORT_RID2SID(i16)		(0x00828 + 4 * (i16))
#define   PORT_RID2SID_VALID		BIT(31)
#define   PORT_RID2SID_SID_SHIFT	16
#define   PORT_RID2SID_BUS_SHIFT	8
#define   PORT_RID2SID_DEV_SHIFT	3
#define   PORT_RID2SID_FUNC_SHIFT	0
#define PORT_OUTS_PREQS_HDR		0x00980
#define   PORT_OUTS_PREQS_HDR_MASK	GENMASK(9, 0)
#define PORT_OUTS_PREQS_DATA		0x00984
#define   PORT_OUTS_PREQS_DATA_MASK	GENMASK(15, 0)
#define PORT_TUNCTRL			0x00988
#define   PORT_TUNCTRL_PERST_ON		BIT(0)
#define   PORT_TUNCTRL_PERST_ACK_REQ	BIT(1)
#define PORT_TUNSTAT			0x0098c
#define   PORT_TUNSTAT_PERST_ON		BIT(0)
#define   PORT_TUNSTAT_PERST_ACK_PEND	BIT(1)
#define PORT_PREFMEM_ENABLE		0x00994

/* The doorbell address is "well known" */
#define DOORBELL_ADDR			0xfffff000

struct apple_pcie {
	struct mutex		lock;
	struct device		*dev;
	void __iomem            *rc;
};

static inline void rmwl(u32 clr, u32 set, void __iomem *addr)
{
	writel_relaxed((readl_relaxed(addr) & ~clr) | set, addr);
}

static int apple_pcie_setup_refclk(void __iomem *rc,
				   void __iomem *port,
				   unsigned int idx)
{
	u32 stat;
	int res;

	res = readl_relaxed_poll_timeout(rc + CORE_RC_PHYIF_STAT, stat,
					 stat & CORE_RC_PHYIF_STAT_REFCLK,
					 100, 50000);
	if (res < 0)
		return res;

	rmwl(0, CORE_LANE_CTL_CFGACC, rc + CORE_LANE_CTL(idx));
	rmwl(0, CORE_LANE_CFG_REFCLK0REQ, rc + CORE_LANE_CFG(idx));

	res = readl_relaxed_poll_timeout(rc + CORE_LANE_CFG(idx), stat,
					 stat & CORE_LANE_CFG_REFCLK0ACK,
					 100, 50000);
	if (res < 0)
		return res;

	rmwl(0, CORE_LANE_CFG_REFCLK1, rc + CORE_LANE_CFG(idx));
	res = readl_relaxed_poll_timeout(rc + CORE_LANE_CFG(idx), stat,
					 stat & CORE_LANE_CFG_REFCLK1,
					 100, 50000);

	if (res < 0)
		return res;

	rmwl(CORE_LANE_CTL_CFGACC, 0, rc + CORE_LANE_CTL(idx));

	/* Flush writes before enabling the clocks */
	dma_wmb();

	rmwl(0, CORE_LANE_CFG_REFCLKEN, rc + CORE_LANE_CFG(idx));
	rmwl(0, PORT_REFCLK_EN, port + PORT_REFCLK);

	return 0;
}

static int apple_pcie_setup_port(struct apple_pcie *pcie,
				 struct gpio_desc *reset,
				 unsigned int i)
{
	struct platform_device *platform = to_platform_device(pcie->dev);
	void __iomem *port;
	uint32_t stat;
	int ret;

	port = devm_platform_ioremap_resource(platform, i + 2);

	if (IS_ERR(port))
		return -ENODEV;

	/* Skip setup if the link was already enabled by the bootloader */
	if (readl_relaxed(port + PORT_LINKSTS) & PORT_LINKSTS_UP)
		return 0;

	rmwl(0, PORT_APPCLK_EN, port + PORT_APPCLK);

	ret = apple_pcie_setup_refclk(pcie->rc, port, i);
	if (ret < 0)
		return ret;

	rmwl(0, PORT_PERST_OFF, port + PORT_PERST);
	gpiod_set_value(reset, 1);

	ret = readl_relaxed_poll_timeout(port + PORT_STATUS, stat,
					 stat & PORT_STATUS_READY, 100, 250000);
	if (ret < 0) {
		dev_err(pcie->dev, "port %u ready wait timeout\n", i);
		return ret;
	}

	/* Configure MSIs */
	writel_relaxed(DOORBELL_ADDR, port + PORT_MSIADDR);
	writel_relaxed(0, port + PORT_MSIBASE);

	/* Enable 32 MSIs */
	writel_relaxed((5 << PORT_MSICFG_L2MSINUM_SHIFT) | PORT_MSICFG_EN,
		       port + PORT_MSICFG);

	/* Enable link interrupts */
	writel_relaxed(0xfb512fff, port + PORT_INTMSKSET);

	writel_relaxed(PORT_INT_LINK_UP | PORT_INT_LINK_DOWN |
		       PORT_INT_AF_TIMEOUT | PORT_INT_REQADDR_GT32 |
		       PORT_INT_MSI_ERR | PORT_INT_MSI_BAD_DATA |
		       PORT_INT_CPL_ABORT | PORT_INT_CPL_TIMEOUT | (1 << 26),
		       port + PORT_INTSTAT);

	/* Flush writes and enable the link */
	dma_wmb();
	writel_relaxed(PORT_LTSSMCTL_START, port + PORT_LTSSMCTL);

	return 0;
}

static int apple_m1_pci_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct platform_device *platform = to_platform_device(dev);

	struct apple_pcie *pcie;
	struct device_node *of_port;
	int ret, i;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = dev;

	mutex_init(&pcie->lock);

	pcie->rc = devm_platform_ioremap_resource(platform, 1);

	if (IS_ERR(pcie->rc))
		return -ENODEV;

	i = 0;

	for_each_child_of_node(dev->of_node, of_port) {
		struct gpio_desc *reset;

		reset = gpiod_get_from_of_node(of_port, "reset-gpios", 0,
					       GPIOD_OUT_LOW, "#PERST");
		if (IS_ERR(reset))
			return PTR_ERR(reset);

		ret = apple_pcie_setup_port(pcie, reset, i);

		if (ret) {
			dev_err(pcie->dev, "Port %u setup fail: %d\n", i, ret);
			return ret;
		}

		++i;
	}

	return 0;
}

static const struct pci_ecam_ops apple_m1_cfg_ecam_ops = {
	.init		= apple_m1_pci_init,
	.pci_ops	= {
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};

static const struct of_device_id apple_pci_of_match[] = {
	{ .compatible = "apple,pcie", .data = &apple_m1_cfg_ecam_ops },
	{ }
};
MODULE_DEVICE_TABLE(of, apple_pci_of_match);

static struct platform_driver apple_pci_driver = {
	.driver = {
		.name = "pcie-apple",
		.of_match_table = apple_pci_of_match,
	},
	.probe = pci_host_common_probe,
	.remove = pci_host_common_remove,
};
module_platform_driver(apple_pci_driver);

MODULE_LICENSE("GPL v2");
