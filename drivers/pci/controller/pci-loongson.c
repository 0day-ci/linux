// SPDX-License-Identifier: GPL-2.0
/*
 * Loongson PCI Host Controller Driver
 *
 * Copyright (C) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 */

#include <linux/of_device.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "../pci.h"

#define FLAG_CFG0	BIT(0)
#define FLAG_CFG1	BIT(1)
#define FLAG_DEV_FIX	BIT(2)

struct loongson_pci {
	void __iomem *cfg0_base;
	void __iomem *cfg1_base;
	struct platform_device *pdev;
	u32 flags;
};

static void __iomem *cfg1_map(struct loongson_pci *priv, int bus,
				unsigned int devfn, int where)
{
	unsigned long addroff = 0x0;

	if (bus != 0)
		addroff |= BIT(28); /* Type 1 Access */
	addroff |= (where & 0xff) | ((where & 0xf00) << 16);
	addroff |= (bus << 16) | (devfn << 8);
	return priv->cfg1_base + addroff;
}

static void __iomem *cfg0_map(struct loongson_pci *priv, int bus,
				unsigned int devfn, int where)
{
	unsigned long addroff = 0x0;

	if (bus != 0)
		addroff |= BIT(24); /* Type 1 Access */
	addroff |= (bus << 16) | (devfn << 8) | where;
	return priv->cfg0_base + addroff;
}

static void __iomem *pci_loongson_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where)
{
	unsigned char busnum = bus->number;
	struct pci_host_bridge *bridge = pci_find_host_bridge(bus);
	struct loongson_pci *priv =  pci_host_bridge_priv(bridge);

	/*
	 * Do not read more than one device on the bus other than
	 * the host bus. For our hardware the root bus is always bus 0.
	 */
	if (priv->flags & FLAG_DEV_FIX && busnum != 0 &&
		PCI_SLOT(devfn) > 0)
		return NULL;

	/* CFG0 can only access standard space */
	if (where < PCI_CFG_SPACE_SIZE && priv->cfg0_base)
		return cfg0_map(priv, busnum, devfn, where);

	/* CFG1 can access extended space */
	if (where < PCI_CFG_SPACE_EXP_SIZE && priv->cfg1_base)
		return cfg1_map(priv, busnum, devfn, where);

	return NULL;
}

static int loongson_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;
	u8 val;

	irq = of_irq_parse_and_map_pci(dev, slot, pin);
	if (irq > 0)
		return irq;

	/* Care i8259 legacy systems */
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &val);
	/* i8259 only have 15 IRQs */
	if (val > 15)
		return 0;

	return val;
}

/* H/w only accept 32-bit PCI operations */
static struct pci_ops loongson_pci_ops = {
	.map_bus = pci_loongson_map_bus,
	.read	= pci_generic_config_read32,
	.write	= pci_generic_config_write32,
};

static const struct of_device_id loongson_pci_of_match[] = {
	{ .compatible = "loongson,ls2k-pci",
		.data = (void *)(FLAG_CFG0 | FLAG_CFG1 | FLAG_DEV_FIX), },
	{ .compatible = "loongson,ls7a-pci",
		.data = (void *)(FLAG_CFG0 | FLAG_CFG1 | FLAG_DEV_FIX), },
	{ .compatible = "loongson,rs780e-pci",
		.data = (void *)(FLAG_CFG0), },
	{}
};

static int loongson_pci_probe(struct platform_device *pdev)
{
	struct loongson_pci *priv;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct pci_host_bridge *bridge;
	struct resource *regs;

	if (!node)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*priv));
	if (!bridge)
		return -ENODEV;

	priv = pci_host_bridge_priv(bridge);
	priv->pdev = pdev;
	priv->flags = (unsigned long)of_device_get_match_data(dev);

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(dev, "missing mem resources for cfg0\n");
		return -EINVAL;
	}

	priv->cfg0_base = devm_pci_remap_cfg_resource(dev, regs);
	if (IS_ERR(priv->cfg0_base))
		return PTR_ERR(priv->cfg0_base);

	/* CFG1 is optional */
	if (priv->flags & FLAG_CFG1) {
		regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!regs)
			dev_info(dev, "missing mem resource for cfg1\n");
		else {
			priv->cfg1_base = devm_pci_remap_cfg_resource(dev, regs);
			if (IS_ERR(priv->cfg1_base))
				priv->cfg1_base = NULL;
		}
	}

	bridge->sysdata = priv;
	bridge->ops = &loongson_pci_ops;
	bridge->map_irq = loongson_map_irq;

	return pci_host_probe(bridge);
}

static struct platform_driver loongson_pci_driver = {
	.driver = {
		.name = "loongson-pci",
		.of_match_table = loongson_pci_of_match,
	},
	.probe = loongson_pci_probe,
};
builtin_platform_driver(loongson_pci_driver);
