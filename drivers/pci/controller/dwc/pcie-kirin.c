// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Kirin Phone SoCs
 *
 * Copyright (C) 2017 HiSilicon Electronics Co., Ltd.
 *		https://www.huawei.com
 *
 * Author: Xiaowei Song <songxiaowei@huawei.com>
 */

#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/resource.h>
#include <linux/types.h>
#include "pcie-designware.h"

#define to_kirin_pcie(x) dev_get_drvdata((x)->dev)

/* PCIe ELBI registers */
#define SOC_PCIECTRL_CTRL0_ADDR		0x000
#define SOC_PCIECTRL_CTRL1_ADDR		0x004
#define PCIE_ELBI_SLV_DBI_ENABLE	(0x1 << 21)

/* info located in APB */
#define PCIE_APP_LTSSM_ENABLE	0x01c
#define PCIE_APB_PHY_STATUS0	0x400
#define PCIE_LINKUP_ENABLE	(0x8020)
#define PCIE_LTSSM_ENABLE_BIT	(0x1 << 11)
#define PIPE_CLK_STABLE		(0x1 << 19)

/* info located in sysctrl */
#define SCTRL_PCIE_CMOS_OFFSET	0x60
#define SCTRL_PCIE_CMOS_BIT	0x10
#define SCTRL_PCIE_ISO_OFFSET	0x44
#define SCTRL_PCIE_ISO_BIT	0x30
#define SCTRL_PCIE_HPCLK_OFFSET	0x190
#define SCTRL_PCIE_HPCLK_BIT	0x184000
#define SCTRL_PCIE_OE_OFFSET	0x14a
#define PCIE_DEBOUNCE_PARAM	0xF0F400
#define PCIE_OE_BYPASS		(0x3 << 28)

struct kirin_pcie {
	struct dw_pcie	*pci;
	struct phy	*phy;
	struct regmap	*apb;
};

static const struct regmap_config pcie_kirin_regmap_conf = {
	.name = "kirin_pcie_apb",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static long kirin_pcie_get_resource(struct kirin_pcie *kirin_pcie,
				    struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *apb_base;

	apb_base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(apb_base))
		return PTR_ERR(apb_base);

	kirin_pcie->apb = devm_regmap_init_mmio(dev, apb_base,
						&pcie_kirin_regmap_conf);
	if (IS_ERR(kirin_pcie->apb))
		return PTR_ERR(kirin_pcie->apb);

	return 0;
}

static int kirin_pcie_power_on(struct kirin_pcie *kirin_pcie)
{
	int ret;

	ret = phy_init(kirin_pcie->phy);
	if (ret)
		return ret;

	return phy_power_on(kirin_pcie->phy);
}

static void kirin_pcie_sideband_dbi_w_mode(struct kirin_pcie *kirin_pcie,
					   bool on)
{
	u32 val;

	regmap_read(kirin_pcie->apb, SOC_PCIECTRL_CTRL0_ADDR, &val);
	if (on)
		val = val | PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val = val & ~PCIE_ELBI_SLV_DBI_ENABLE;

	regmap_write(kirin_pcie->apb, SOC_PCIECTRL_CTRL0_ADDR, val);
}

static void kirin_pcie_sideband_dbi_r_mode(struct kirin_pcie *kirin_pcie,
					   bool on)
{
	u32 val;

	regmap_read(kirin_pcie->apb, SOC_PCIECTRL_CTRL1_ADDR, &val);
	if (on)
		val = val | PCIE_ELBI_SLV_DBI_ENABLE;
	else
		val = val & ~PCIE_ELBI_SLV_DBI_ENABLE;

	regmap_write(kirin_pcie->apb, SOC_PCIECTRL_CTRL1_ADDR, val);
}

static int kirin_pcie_rd_own_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn)) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	*val = dw_pcie_read_dbi(pci, where, size);
	return PCIBIOS_SUCCESSFUL;
}

static int kirin_pcie_wr_own_conf(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	dw_pcie_write_dbi(pci, where, size, val);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops kirin_pci_ops = {
	.read = kirin_pcie_rd_own_conf,
	.write = kirin_pcie_wr_own_conf,
};

static u32 kirin_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base,
			       u32 reg, size_t size)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);
	u32 ret;

	kirin_pcie_sideband_dbi_r_mode(kirin_pcie, true);
	dw_pcie_read(base + reg, size, &ret);
	kirin_pcie_sideband_dbi_r_mode(kirin_pcie, false);

	return ret;
}

static void kirin_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				 u32 reg, size_t size, u32 val)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);

	kirin_pcie_sideband_dbi_w_mode(kirin_pcie, true);
	dw_pcie_write(base + reg, size, val);
	kirin_pcie_sideband_dbi_w_mode(kirin_pcie, false);
}

static int kirin_pcie_link_up(struct dw_pcie *pci)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);
	u32 val;

	regmap_read(kirin_pcie->apb, PCIE_APB_PHY_STATUS0, &val);
	if ((val & PCIE_LINKUP_ENABLE) == PCIE_LINKUP_ENABLE)
		return 1;

	return 0;
}

static int kirin_pcie_start_link(struct dw_pcie *pci)
{
	struct kirin_pcie *kirin_pcie = to_kirin_pcie(pci);

	/* assert LTSSM enable */
	regmap_write(kirin_pcie->apb, PCIE_APP_LTSSM_ENABLE,
		     PCIE_LTSSM_ENABLE_BIT);

	return 0;
}

static int kirin_pcie_host_init(struct pcie_port *pp)
{
	pp->bridge->ops = &kirin_pci_ops;

	return 0;
}

static const struct dw_pcie_ops kirin_dw_pcie_ops = {
	.read_dbi = kirin_pcie_read_dbi,
	.write_dbi = kirin_pcie_write_dbi,
	.link_up = kirin_pcie_link_up,
	.start_link = kirin_pcie_start_link,
};

static const struct dw_pcie_host_ops kirin_pcie_host_ops = {
	.host_init = kirin_pcie_host_init,
};

static int kirin_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kirin_pcie *kirin_pcie;
	struct dw_pcie *pci;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "NULL node\n");
		return -EINVAL;
	}

	kirin_pcie = devm_kzalloc(dev, sizeof(struct kirin_pcie), GFP_KERNEL);
	if (!kirin_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &kirin_dw_pcie_ops;
	pci->pp.ops = &kirin_pcie_host_ops;
	kirin_pcie->pci = pci;

	kirin_pcie->phy = devm_of_phy_get(dev, dev->of_node, NULL);
	if (IS_ERR(kirin_pcie->phy))
		return PTR_ERR(kirin_pcie->phy);

	ret = kirin_pcie_get_resource(kirin_pcie, pdev);
	if (ret)
		return ret;

	ret = kirin_pcie_power_on(kirin_pcie);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, kirin_pcie);

	ret = dw_pcie_host_init(&pci->pp);
	if (ret) {
		phy_power_off(kirin_pcie->phy);
		goto err;
	}

	return 0;
err:
	phy_exit(kirin_pcie->phy);

	return ret;
}

static int __exit kirin_pcie_remove(struct platform_device *pdev)
{
	struct kirin_pcie *kirin_pcie = platform_get_drvdata(pdev);

	phy_power_off(kirin_pcie->phy);
	phy_exit(kirin_pcie->phy);

	return 0;
}

static const struct of_device_id kirin_pcie_match[] = {
	{ .compatible = "hisilicon,kirin960-pcie" },
	{ .compatible = "hisilicon,kirin970-pcie" },
	{},
};

static struct platform_driver kirin_pcie_driver = {
	.probe			= kirin_pcie_probe,
	.remove	        	= __exit_p(kirin_pcie_remove),
	.driver			= {
		.name			= "kirin-pcie",
		.of_match_table = kirin_pcie_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(kirin_pcie_driver);
