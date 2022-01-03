// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 STMicroelectronics
 *
 * STMicroelectronics PCI express Driver for STi SoCs.
 * ST PCIe IPs are built around a Synopsys IP Core.
 *
 * Authors: Fabrice Gasnier <fabrice.gasnier@foss.st.com>
 *          Alain Volmat <avolmat@me.com>
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "pcie-designware.h"

/* RC_ADDRESS_TRANSLATION Registers */
#define TRANSLATION_CONTROL		0x900
/* Controls if area is inclusive or exclusive */
#define RC_PASS_ADDR_RANGE		BIT(1)

/* Base of area reserved for config accesses. Fixed size of 64K. */
#define CFG_BASE_ADDRESS		0x92c
#define CFG_REGION_SIZE			65536
#define CFG_SPACE1_OFFSET		0x1000

/* First 4K of config space has this BDF (bus,device,function) */
#define FUNC0_BDF_NUM			0x930

/* Mem regions */
#define IN0_MEM_ADDR_START		0x964
#define IN0_MEM_ADDR_LIMIT		0x968
#define IN1_MEM_ADDR_START		0x974
#define IN1_MEM_ADDR_LIMIT		0x978

/* syscfg1 bits */
#define PCIE_APP_LTSSM_ENABLE		BIT(2)
/* syscfg0 bits */
#define PCIE_TYPE_ROOT_COMPLEX		BIT(0)

/* st,syscfg offsets */
#define SYSCFG0_REG	1
#define SYSCFG1_REG	2

#define to_st_pcie(x)	dev_get_drvdata((x)->dev)

/**
 * struct st_pcie - private data of the controller
 * @dw: designware pcie
 * @syscfg0: PCIe conf register setting root complex, regmap offset
 * @syscfg1: PCIe conf register for PCIE_APP_LTSSM_ENABLE, regmap offset
 * @phy: associated pcie phy
 * @lmi: memory made available to the controller
 * @regmap: Syscfg registers bank in which PCIe port is configured
 * @pwr: power control
 * @rst: reset control
 * @reset_gpio: optional reset gpio
 * @config_window_start: start address of 64K config space area
 */
struct st_pcie {
	struct dw_pcie *dw;
	int syscfg0;
	int syscfg1;
	struct phy *phy;
	struct resource	*lmi;
	struct regmap *regmap;
	struct reset_control *pwr;
	struct reset_control *rst;
	struct gpio_desc *reset_gpio;
	phys_addr_t config_window_start;
};

/*
 * The PCI express core IP expects the following arrangement on it's address
 * bus (slv_haddr) when driving config cycles.
 * bus_number		[31:24]
 * dev_number		[23:19]
 * func_number		[18:16]
 * unused		[15:12]
 * ext_reg_number	[11:8]
 * reg_number		[7:2]
 *
 * Bits [15:12] are unused.
 *
 * In the glue logic there is a 64K region of address space that can be
 * written/read to generate config cycles. The base address of this is
 * controlled by CFG_BASE_ADDRESS. There are 8 16 bit registers called
 * FUNC0_BDF_NUM to FUNC8_BDF_NUM. These split the bottom half of the 64K
 * window into 8 regions at 4K boundaries. These control the bus, device and
 * function number you are trying to talk to.
 *
 * The decision on whether to generate a type 0 or type 1 access is controlled
 * by bits 15:12 of the address you write to.  If they are zero, then a type 0
 * is generated, if anything else it will be a type 1. Thus the bottom 4K
 * region controlled by FUNC0_BDF_NUM can only generate type 0, all the others
 * can only generate type 1.
 *
 * We only use FUNC0_BDF_NUM and FUNC1_BDF_NUM. Which one you use is selected
 * by bit 12 of the address you write to. The selected register is then used
 * for the top 16 bits of the slv_haddr to form the bus/dev/func, bit 15:12 are
 * wired to zero, and bits 11:2 form the address of the register you want to
 * read in config space.
 *
 * We always write FUNC0_BDF_NUM as a 32 bit write. So if we want type 1
 * accesses we have to shift by 16 so in effect we are writing to FUNC1_BDF_NUM
 */
static inline u32 bdf_num(int bus, int devfn, int is_root_bus)
{
	return ((bus << 8) | devfn) << (is_root_bus ? 0 : 16);
}

static void __iomem *st_pcie_other_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct pcie_port *pp = bus->sysdata;
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	u32 bdf;

	bdf = bdf_num(bus->number, devfn, pci_is_root_bus(bus));

	/* Set the config packet devfn */
	dw_pcie_writel_dbi(dw, FUNC0_BDF_NUM, bdf);
	dw_pcie_readl_dbi(dw, FUNC0_BDF_NUM);

	return pp->va_cfg0_base + where + (pci_is_root_bus(bus->parent) ? 0 : CFG_SPACE1_OFFSET);
}

static struct pci_ops st_child_pcie_ops = {
	.map_bus = st_pcie_other_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static void st_pcie_hw_setup(struct dw_pcie *dw)
{
	struct st_pcie *pcie = to_st_pcie(dw);

	/* Set up the config window to the top of the PCI address space */
	dw_pcie_writel_dbi(dw, CFG_BASE_ADDRESS, pcie->config_window_start);

	/*
	 * Open up memory to the PCI controller. We could do slightly
	 * better than this and exclude the kernel text segment and bss etc.
	 * They are base/limit registers so can be of arbitrary alignment
	 * presumably
	 */
	dw_pcie_writel_dbi(dw, IN0_MEM_ADDR_START, pcie->lmi->start);
	dw_pcie_writel_dbi(dw, IN0_MEM_ADDR_LIMIT, pcie->lmi->end);

	/* Disable the 2nd region */
	dw_pcie_writel_dbi(dw, IN1_MEM_ADDR_START, ~0);
	dw_pcie_writel_dbi(dw, IN1_MEM_ADDR_LIMIT, 0);

	dw_pcie_writel_dbi(dw, TRANSLATION_CONTROL, RC_PASS_ADDR_RANGE);
}

static int st_pcie_init(struct pcie_port *pp)
{
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	struct st_pcie *pcie = to_st_pcie(dw);
	int ret;

	/* Set device type : Root Complex */
	ret = regmap_write(pcie->regmap, pcie->syscfg0, PCIE_TYPE_ROOT_COMPLEX);
	if (ret < 0) {
		dev_err(dw->dev, "unable to set device type\n");
		return ret;
	}

	ret = reset_control_deassert(pcie->pwr);
	if (ret) {
		dev_err(dw->dev, "unable to bring out of powerdown\n");
		return ret;
	}

	ret = reset_control_deassert(pcie->rst);
	if (ret) {
		dev_err(dw->dev, "unable to bring out of softreset\n");
		return ret;
	}

	/* let the controller initialize in the proper mode (RC) after reset */
	usleep_range(1000, 2000);

	return ret;
}

static int st_pcie_control_ltssm(struct dw_pcie *dw, bool enable)
{
	struct st_pcie *pcie = to_st_pcie(dw);

	return regmap_update_bits(pcie->regmap, pcie->syscfg1, PCIE_APP_LTSSM_ENABLE,
				  FIELD_PREP(PCIE_APP_LTSSM_ENABLE, enable));
}

static int st_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	struct st_pcie *pcie = to_st_pcie(dw);
	int err;

	pcie->config_window_start = pp->cfg0_base;

	/*
	 * "Override" default ops provided by designware driver as STI
	 * PCIe uses its own translation unit rather than iATU
	 */
	pp->bridge->child_ops = &st_child_pcie_ops;

	/*
	 * We have to initialise the PCIe cell on some hardware before we can
	 * talk to the phy
	 */
	err = st_pcie_init(pp);
	if (err)
		return err;

	err = st_pcie_control_ltssm(dw, false);
	if (err) {
		dev_err(dw->dev, "disable ltssm failed, %d\n", err);
		return err;
	}

	/* Init the associated miphy */
	err = phy_init(pcie->phy);
	if (err < 0) {
		dev_err(dw->dev, "Cannot init PHY: %d\n", err);
		return err;
	}

	return 0;
}

static int st_pcie_start_link(struct dw_pcie *dw)
{
	struct st_pcie *pcie = to_st_pcie(dw);
	int err;

	/* Do all the register poking */
	st_pcie_hw_setup(dw);

	if (pcie->reset_gpio) {
		/* Assert the PERST# signal */
		gpiod_set_value(pcie->reset_gpio, 1);

		/* PERST# signal must stay asserted for at least 100us (Tperst-clk) */
		usleep_range(100, 200);

		/* Release PERST# signal */
		gpiod_set_value(pcie->reset_gpio, 0);
	}

	/* Re-enable the link, link training must begin shortly after reset */
	err = st_pcie_control_ltssm(dw, true);
	if (err) {
		dev_err(dw->dev, "enable ltssm failed, %d\n", err);
		return err;
	}

	err = dw_pcie_wait_for_link(dw);
	if (err) {
		dev_err(dw->dev, "wait for link failed, %d\n", err);
		return err;
	}

	/*
	 * PCIe specification states that you should not issue any config
	 * requests until 100ms after asserting reset, so we enforce that here
	 */
	if  (pcie->reset_gpio)
		msleep(100);

	return 0;
}

static struct dw_pcie_host_ops st_pcie_host_ops = {
	.host_init = st_pcie_host_init,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = st_pcie_start_link,
};

static int st_pcie_probe(struct platform_device *pdev)
{
	struct st_pcie *pcie;
	struct dw_pcie *dw;
	struct device_node *np = pdev->dev.of_node;
	struct pcie_port *pp;
	int ret;

	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	dw = devm_kzalloc(&pdev->dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;
	pcie->dw = dw;
	dw->dev = &pdev->dev;
	dw->ops = &dw_pcie_ops;

	pp = &dw->pp;
	pp->ops = &st_pcie_host_ops;

	/* mem regions */
	pcie->lmi = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mem-window");
	if (!pcie->lmi)
		return -ENXIO;

	/* regmap registers for PCIe IP configuration */
	pcie->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(pcie->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcie->regmap),
				     "No syscfg phandle specified\n");

	ret = of_property_read_u32_index(np, "st,syscfg", SYSCFG0_REG, &pcie->syscfg0);
	if (ret) {
		dev_err(&pdev->dev, "can't get syscfg0 offset (%d)\n", ret);
		return ret;
	}

	ret = of_property_read_u32_index(np, "st,syscfg", SYSCFG1_REG, &pcie->syscfg1);
	if (ret) {
		dev_err(&pdev->dev, "can't get syscfg1 offset (%d)\n", ret);
		return ret;
	}

	/* powerdown / resets */
	pcie->pwr = devm_reset_control_get_optional(&pdev->dev, "powerdown");
	if (IS_ERR(pcie->pwr))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcie->pwr),
				     "Error getting powerdown reset control\n");

	pcie->rst = devm_reset_control_get(&pdev->dev, "softreset");
	if (IS_ERR(pcie->rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcie->rst),
				     "Error getting softreset control\n");

	/* phy */
	pcie->phy = devm_phy_get(&pdev->dev, "pcie");
	if (IS_ERR(pcie->phy))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcie->phy), "no PHY configured\n");

	/* Claim the GPIO for PRST# if available, keep it de-asserted */
	pcie->reset_gpio = devm_gpiod_get_optional(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(pcie->reset_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcie->reset_gpio),
				     "Cannot request reset-gpios\n");

	platform_set_drvdata(pdev, pcie);

	ret = dw_pcie_host_init(pp);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to initialize host\n");

	return 0;
}

static const struct of_device_id st_pcie_of_match[] = {
	{ .compatible = "st,stih407-pcie", },
	{ },
};
MODULE_DEVICE_TABLE(of, st_pcie_of_match);

static struct platform_driver st_pcie_driver __refdata = {
	.probe  = st_pcie_probe,
	.driver = {
		.name = "st-pcie",
		.of_match_table = st_pcie_of_match,
	},
};
module_platform_driver(st_pcie_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@foss.st.com>");
MODULE_AUTHOR("Alain Volmat <avolmat@me.com>");
MODULE_DESCRIPTION("STi PCIe Controller driver");
MODULE_LICENSE("GPL v2");
