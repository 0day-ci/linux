// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm PCIe Endpoint controller driver
 *
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Author: Siddartha Mohanadoss <smohanad@codeaurora.org
 *
 * Copyright (c) 2021, Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/phy/phy.h>
#include <linux/pm_domain.h>

#include "pcie-designware.h"

/* PARF registers */
#define PARF_SYS_CTRL				0x00
#define PARF_DB_CTRL				0x10
#define PARF_PM_CTRL				0x20
#define PARF_DEBUG_INT_EN			0x190
#define PARF_AXI_MSTR_RD_HALT_NO_WRITES		0x1a4
#define PARF_AXI_MSTR_WR_ADDR_HALT		0x1a8
#define PARF_Q2A_FLUSH				0x1aC
#define PARF_LTSSM				0x1b0
#define PARF_CFG_BITS				0x210
#define PARF_INT_ALL_STATUS			0x224
#define PARF_INT_ALL_CLEAR			0x228
#define PARF_INT_ALL_MASK			0x22c
#define PARF_SLV_ADDR_MSB_CTRL			0x2c0
#define PARF_DBI_BASE_ADDR			0x350
#define PARF_DBI_BASE_ADDR_HI			0x354
#define PARF_SLV_ADDR_SPACE_SIZE		0x358
#define PARF_SLV_ADDR_SPACE_SIZE_HI		0x35c
#define PARF_ATU_BASE_ADDR			0x634
#define PARF_ATU_BASE_ADDR_HI			0x638
#define PARF_SRIS_MODE				0x644
#define PARF_DEVICE_TYPE			0x1000
#define PARF_BDF_TO_SID_CFG			0x2c00

/* ELBI registers */
#define ELBI_SYS_STTS				0x08

/* DBI registers */
#define DBI_CAP_ID_NXT_PTR			0x40
#define DBI_CON_STATUS				0x44
#define DBI_DEVICE_CAPABILITIES			0x74
#define DBI_LINK_CAPABILITIES			0x7c
#define DBI_LINK_CONTROL2_LINK_STATUS2		0xa0
#define DBI_L1SUB_CAPABILITY			0x234
#define DBI_ACK_F_ASPM_CTRL			0x70c
#define DBI_GEN3_RELATED_OFF			0x890
#define DBI_AUX_CLK_FREQ			0xb40

#define DBI_L0S_ACCPT_LATENCY_MASK		GENMASK(8, 6)
#define DBI_L1_ACCPT_LATENCY_MASK		GENMASK(11, 9)
#define DBI_L0S_EXIT_LATENCY_MASK		GENMASK(14, 12)
#define DBI_L1_EXIT_LATENCY_MASK		GENMASK(17, 15)
#define DBI_ACK_N_FTS_MASK			GENMASK(15, 8)

/* TCSR registers */
#define TCSR_PCIE_PERST_EN			0x258
#define TCSR_PERST_SEPARATION_ENABLE		0x270

#define XMLH_LINK_UP				0x400
#define CORE_RESET_TIME_US_MIN			1000
#define CORE_RESET_TIME_US_MAX			1005
#define WAKE_DELAY_US				2000 /* 2 ms */

#define to_pcie_ep(x)				dev_get_drvdata((x)->dev)

enum qcom_pcie_ep_link_status {
	QCOM_PCIE_EP_LINK_DISABLED,
	QCOM_PCIE_EP_LINK_ENABLED,
	QCOM_PCIE_EP_LINK_UP,
	QCOM_PCIE_EP_LINK_DOWN,
};

enum qcom_pcie_ep_irq {
	QCOM_PCIE_EP_INT_RESERVED,
	QCOM_PCIE_EP_INT_LINK_DOWN,
	QCOM_PCIE_EP_INT_BME,
	QCOM_PCIE_EP_INT_PM_TURNOFF,
	QCOM_PCIE_EP_INT_DEBUG,
	QCOM_PCIE_EP_INT_LTR,
	QCOM_PCIE_EP_INT_MHI_Q6,
	QCOM_PCIE_EP_INT_MHI_A7,
	QCOM_PCIE_EP_INT_DSTATE_CHANGE,
	QCOM_PCIE_EP_INT_L1SUB_TIMEOUT,
	QCOM_PCIE_EP_INT_MMIO_WRITE,
	QCOM_PCIE_EP_INT_CFG_WRITE,
	QCOM_PCIE_EP_INT_BRIDGE_FLUSH_N,
	QCOM_PCIE_EP_INT_LINK_UP,
	QCOM_PCIE_EP_INT_AER_LEGACY,
	QCOM_PCIE_EP_INT_PLS_ERR,
	QCOM_PCIE_EP_INT_PME_LEGACY,
	QCOM_PCIE_EP_INT_PLS_PME,
	QCOM_PCIE_EP_INT_MAX,
};

static struct clk_bulk_data qcom_pcie_ep_clks[] = {
	{ .id = "cfg" },
	{ .id = "aux" },
	{ .id = "bus_master" },
	{ .id = "bus_slave" },
	{ .id = "ref" },
	{ .id = "sleep" },
	{ .id = "slave_q2a" },
};

struct qcom_pcie_ep {
	struct dw_pcie pci;

	void __iomem *parf;
	void __iomem *elbi;
	void __iomem *tcsr;

	struct reset_control *core_reset;
	struct gpio_desc *reset;
	struct gpio_desc *wake;
	struct phy *phy;

	resource_size_t dbi_phys;
	resource_size_t atu_phys;

	enum qcom_pcie_ep_link_status link_status;
	int global_irq;
	int perst_irq;
};

static void qcom_pcie_ep_enable_ltssm(struct qcom_pcie_ep *pcie_ep)
{
	u32 reg;

	reg = readl(pcie_ep->parf + PARF_LTSSM);
	reg |= BIT(8);
	writel_relaxed(reg, pcie_ep->parf + PARF_LTSSM);
}

static int qcom_pcie_ep_core_reset(struct qcom_pcie_ep *pcie_ep)
{
	struct dw_pcie *pci = &pcie_ep->pci;
	struct device *dev = pci->dev;
	int ret = 0;

	ret = reset_control_assert(pcie_ep->core_reset);
	if (ret) {
		dev_err(dev, "Cannot assert core reset\n");
		return ret;
	}

	usleep_range(CORE_RESET_TIME_US_MIN, CORE_RESET_TIME_US_MAX);

	ret = reset_control_deassert(pcie_ep->core_reset);
	if (ret) {
		dev_err(dev, "Cannot de-assert core reset\n");
		return ret;
	}

	usleep_range(CORE_RESET_TIME_US_MIN, CORE_RESET_TIME_US_MAX);

	return 0;
}

/*
 * Delatch PERST_EN and PERST_SEPARATION_ENABLE with TCSR to avoid
 * device reset during host reboot and hibernation. The driver is
 * expected to handle this situation.
 */
static void qcom_pcie_ep_configure_tcsr(struct qcom_pcie_ep *pcie_ep)
{
	writel_relaxed(0x0, pcie_ep->tcsr + TCSR_PCIE_PERST_EN);
	writel_relaxed(0x0, pcie_ep->tcsr + TCSR_PERST_SEPARATION_ENABLE);
}

static int qcom_pcie_ep_enable_resources(struct qcom_pcie_ep *pcie_ep)
{
	struct dw_pcie *pci = &pcie_ep->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(qcom_pcie_ep_clks),
				      qcom_pcie_ep_clks);
	if (ret) {
		dev_err(dev, "Failed to enable clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static void qcom_pcie_ep_disable_resources(struct qcom_pcie_ep *pcie_ep)
{
	clk_bulk_disable_unprepare(ARRAY_SIZE(qcom_pcie_ep_clks),
				   qcom_pcie_ep_clks);
}

static void qcom_pcie_ep_configure_irq(struct qcom_pcie_ep *pcie_ep)
{
	u32 val;

	writel_relaxed(0, pcie_ep->parf + PARF_INT_ALL_MASK);
	val = BIT(QCOM_PCIE_EP_INT_LINK_DOWN) |
		BIT(QCOM_PCIE_EP_INT_BME) |
		BIT(QCOM_PCIE_EP_INT_PM_TURNOFF) |
		BIT(QCOM_PCIE_EP_INT_DSTATE_CHANGE) |
		BIT(QCOM_PCIE_EP_INT_LINK_UP);
	writel_relaxed(val, pcie_ep->parf + PARF_INT_ALL_MASK);
}

static int qcom_pcie_ep_core_init(struct qcom_pcie_ep *pcie_ep)
{
	struct dw_pcie *pci = &pcie_ep->pci;
	u32 val;

	/* Disable BDF to SID mapping */
	val = readl_relaxed(pcie_ep->parf + PARF_BDF_TO_SID_CFG);
	val |= BIT(0);
	writel_relaxed(val, pcie_ep->parf + PARF_BDF_TO_SID_CFG);

	/* enable debug IRQ */
	writel_relaxed((BIT(3) | BIT(2) | BIT(1)),
		       pcie_ep->parf + PARF_DEBUG_INT_EN);

	/* Configure PCIe to endpoint mode */
	writel_relaxed(0x0, pcie_ep->parf + PARF_DEVICE_TYPE);

	/* Configure PCIe core to support 1GB aperture */
	writel_relaxed(0x40000000, pcie_ep->parf + PARF_SLV_ADDR_SPACE_SIZE);

	/* Allow entering L1 state */
	val = readl_relaxed(pcie_ep->parf + PARF_PM_CTRL);
	val &= ~BIT(5);
	writel_relaxed(val, pcie_ep->parf + PARF_PM_CTRL);

	/* Configure Slave, DBI and iATU base addresses */
	writel_relaxed(BIT(0), pcie_ep->parf + PARF_SLV_ADDR_MSB_CTRL);
	writel_relaxed(0x200, pcie_ep->parf + PARF_SLV_ADDR_SPACE_SIZE_HI);
	writel_relaxed(0x0, pcie_ep->parf + PARF_SLV_ADDR_SPACE_SIZE);
	writel_relaxed(0x100, pcie_ep->parf + PARF_DBI_BASE_ADDR_HI);
	writel_relaxed(pcie_ep->dbi_phys, pcie_ep->parf + PARF_DBI_BASE_ADDR);
	writel_relaxed(0x100, pcie_ep->parf + PARF_ATU_BASE_ADDR_HI);
	writel_relaxed(pcie_ep->atu_phys, pcie_ep->parf + PARF_ATU_BASE_ADDR);

	/* Read halts write */
	writel_relaxed(0x0, pcie_ep->parf + PARF_AXI_MSTR_RD_HALT_NO_WRITES);
	/* Write after write halt */
	writel_relaxed(BIT(31), pcie_ep->parf + PARF_AXI_MSTR_WR_ADDR_HALT);
	/* Q2A flush disable */
	writel_relaxed(0, pcie_ep->parf + PARF_Q2A_FLUSH);

	/* Disable the DBI Wakeup */
	writel_relaxed(BIT(11), pcie_ep->parf + PARF_SYS_CTRL);
	/* Disable the debouncers */
	writel_relaxed(0x73, pcie_ep->parf + PARF_DB_CTRL);
	/* Disable core clock CGC */
	writel_relaxed(BIT(6), pcie_ep->parf + PARF_SYS_CTRL);
	/* Set AUX power to be on */
	writel_relaxed(BIT(4), pcie_ep->parf + PARF_SYS_CTRL);
	/* Request to exit from L1SS for MSI and LTR MSG */
	writel_relaxed(BIT(1), pcie_ep->parf + PARF_CFG_BITS);

	dw_pcie_dbi_ro_wr_en(pci);

	/* Set the PMC Register - to support PME in D0/D3hot/D3cold */
	val = dw_pcie_readl_dbi(pci, DBI_CAP_ID_NXT_PTR);
	val |= BIT(31) | BIT(30) | BIT(27);
	dw_pcie_writel_dbi(pci, DBI_CAP_ID_NXT_PTR, val);

	/* Set the Endpoint L0s Acceptable Latency to 1us (max) */
	val = dw_pcie_readl_dbi(pci, DBI_DEVICE_CAPABILITIES);
	val |= FIELD_PREP(DBI_L0S_ACCPT_LATENCY_MASK, 0x7);
	dw_pcie_writel_dbi(pci, DBI_DEVICE_CAPABILITIES, val);

	/* Set the Endpoint L1 Acceptable Latency to 1us (max) */
	val = dw_pcie_readl_dbi(pci, DBI_DEVICE_CAPABILITIES);
	val |= FIELD_PREP(DBI_L1_ACCPT_LATENCY_MASK, 0x7);
	dw_pcie_writel_dbi(pci, DBI_DEVICE_CAPABILITIES, val);

	/* Set the L0s Exit Latency to 2us-4us = 0x6 */
	val = dw_pcie_readl_dbi(pci, DBI_LINK_CAPABILITIES);
	val |= FIELD_PREP(DBI_L0S_EXIT_LATENCY_MASK, 0x6);
	dw_pcie_writel_dbi(pci, DBI_LINK_CAPABILITIES, val);

	/* Set the L1 Exit Latency to be 32us-64 us = 0x6 */
	val = dw_pcie_readl_dbi(pci, DBI_LINK_CAPABILITIES);
	val |= FIELD_PREP(DBI_L1_EXIT_LATENCY_MASK, 0x6);
	dw_pcie_writel_dbi(pci, DBI_LINK_CAPABILITIES, val);

	/* L1ss is supported */
	val = dw_pcie_readl_dbi(pci, DBI_L1SUB_CAPABILITY);
	val |= 0x1f;
	dw_pcie_writel_dbi(pci, DBI_L1SUB_CAPABILITY, val);

	/* Enable Clock Power Management */
	val = dw_pcie_readl_dbi(pci, DBI_LINK_CAPABILITIES);
	val |= BIT(18);
	dw_pcie_writel_dbi(pci, DBI_LINK_CAPABILITIES, val);

	dw_pcie_dbi_ro_wr_dis(pci);

	/* Set FTS value to match the PHY setting */
	val = dw_pcie_readl_dbi(pci, DBI_ACK_F_ASPM_CTRL);
	val |= FIELD_PREP(DBI_ACK_N_FTS_MASK, 0x80);
	dw_pcie_writel_dbi(pci, DBI_ACK_F_ASPM_CTRL, val);

	dw_pcie_writel_dbi(pci, DBI_AUX_CLK_FREQ, 0x14);

	/* Prevent L1ss wakeup after 100ms */
	val = dw_pcie_readl_dbi(pci, DBI_GEN3_RELATED_OFF);
	val &= ~BIT(0);
	dw_pcie_writel_dbi(pci, DBI_GEN3_RELATED_OFF, val);

	/* Disable SRIS_MODE */
	val = readl_relaxed(pcie_ep->parf + PARF_SRIS_MODE);
	val &= ~BIT(0);
	writel_relaxed(val, pcie_ep->parf + PARF_SRIS_MODE);

	qcom_pcie_ep_configure_irq(pcie_ep);

	return 0;
}

static int qcom_pcie_confirm_linkup(struct dw_pcie *pci)
{
	struct qcom_pcie_ep *pcie_ep = to_pcie_ep(pci);
	u32 reg;

	reg = readl_relaxed(pcie_ep->elbi + ELBI_SYS_STTS);

	return reg & XMLH_LINK_UP;
}

static int qcom_pcie_start_link(struct dw_pcie *pci)
{
	struct qcom_pcie_ep *pcie_ep = to_pcie_ep(pci);

	enable_irq(pcie_ep->perst_irq);

	return 0;
}

static int qcom_pcie_establish_link(struct dw_pcie *pci)
{
	struct qcom_pcie_ep *pcie_ep = to_pcie_ep(pci);
	struct device *dev = pci->dev;
	int ret;

	if (pcie_ep->link_status == QCOM_PCIE_EP_LINK_ENABLED) {
		dev_info(dev, "Link is already enabled\n");
		return 0;
	}

	/* Enable power and clocks */
	ret = qcom_pcie_ep_enable_resources(pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to enable resources: %d\n", ret);
		return ret;
	}

	/* Perform controller reset */
	ret = qcom_pcie_ep_core_reset(pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to reset the core: %d\n", ret);
		goto err_disable_resources;
	}

	/* Initialize PHY */
	ret = phy_init(pcie_ep->phy);
	if (ret) {
		dev_err(dev, "Failed to initialize PHY: %d\n", ret);
		goto err_disable_resources;
	}

	/* Power ON PHY */
	ret = phy_power_on(pcie_ep->phy);
	if (ret) {
		dev_err(dev, "Failed to Power ON PHY: %d\n", ret);
		goto err_phy_exit;
	}

	/* Assert WAKE# to RC to indicate device is ready */
	gpiod_set_value_cansleep(pcie_ep->wake, 0);
	usleep_range(WAKE_DELAY_US, WAKE_DELAY_US + 500);
	gpiod_set_value_cansleep(pcie_ep->wake, 1);

	/* Configure TCSR */
	qcom_pcie_ep_configure_tcsr(pcie_ep);

	/* Initialize the controller */
	ret = qcom_pcie_ep_core_init(pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to init controller: %d\n", ret);
		goto err_phy_power_off;
	}

	ret = dw_pcie_ep_init_complete(&pcie_ep->pci.ep);
	if (ret) {
		dev_err(dev, "Failed to complete initialization: %d\n", ret);
		goto err_phy_power_off;
	}

	dw_pcie_ep_init_notify(&pcie_ep->pci.ep);

	/* Enable LTSSM */
	qcom_pcie_ep_enable_ltssm(pcie_ep);

	return 0;
/* TODO* assert reset? */
err_phy_power_off:
	phy_power_off(pcie_ep->phy);
err_phy_exit:
	phy_exit(pcie_ep->phy);
err_disable_resources:
	qcom_pcie_ep_disable_resources(pcie_ep);

	return ret;
}

static void qcom_pcie_disable_link(struct dw_pcie *pci)
{
	struct qcom_pcie_ep *pcie_ep = to_pcie_ep(pci);
	struct device *dev = pci->dev;

	if (pcie_ep->link_status == QCOM_PCIE_EP_LINK_DISABLED) {
		dev_info(dev, "Link is already disabled\n");
		return;
	}

	phy_power_off(pcie_ep->phy);
	phy_exit(pcie_ep->phy);
	qcom_pcie_ep_disable_resources(pcie_ep);
	pcie_ep->link_status = QCOM_PCIE_EP_LINK_DISABLED;
}

/* Common DWC controller ops */
static const struct dw_pcie_ops pci_ops = {
	.link_up = qcom_pcie_confirm_linkup,
	.start_link = qcom_pcie_start_link,
	.stop_link = qcom_pcie_disable_link,
};

static int qcom_pcie_ep_get_io_resources(struct platform_device *pdev,
					 struct qcom_pcie_ep *pcie_ep)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci = &pcie_ep->pci;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "parf");
	pcie_ep->parf = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie_ep->parf))
		return PTR_ERR(pcie_ep->parf);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);
	pci->dbi_base2 = pci->dbi_base;
	pcie_ep->dbi_phys = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "elbi");
	pcie_ep->elbi = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pcie_ep->elbi))
		return PTR_ERR(pcie_ep->elbi);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
	pci->atu_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pci->atu_base))
		return PTR_ERR(pci->atu_base);
	pcie_ep->atu_phys = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tcsr");
	pcie_ep->tcsr = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie_ep->tcsr))
		return PTR_ERR(pcie_ep->tcsr);

	return 0;
}

static int qcom_pcie_ep_get_resources(struct platform_device *pdev,
				      struct qcom_pcie_ep *pcie_ep)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = qcom_pcie_ep_get_io_resources(pdev, pcie_ep);
	if (ret) {
		dev_err(&pdev->dev, "failed to get io resources %d\n", ret);
		return ret;
	}

	/* Enable GDSC power domain */
	ret = dev_pm_domain_attach(dev, true);
	if (ret)
		return ret;

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(qcom_pcie_ep_clks),
				qcom_pcie_ep_clks);
	if (ret) {
		dev_err(dev, "Failed to get clocks: %d\n", ret);
		return ret;
	}

	pcie_ep->core_reset = devm_reset_control_get_exclusive(dev, "core");
	if (IS_ERR(pcie_ep->core_reset))
		return PTR_ERR(pcie_ep->core_reset);

	pcie_ep->reset = devm_gpiod_get(dev, "reset", GPIOD_IN);
	if (IS_ERR(pcie_ep->reset))
		return PTR_ERR(pcie_ep->reset);

	pcie_ep->wake = devm_gpiod_get_optional(dev, "wake", GPIOD_OUT_HIGH);
	if (IS_ERR(pcie_ep->wake))
		return PTR_ERR(pcie_ep->wake);

	pcie_ep->phy = devm_phy_optional_get(&pdev->dev, "pciephy");
	if (IS_ERR(pcie_ep->phy))
		ret = PTR_ERR(pcie_ep->phy);

	return ret;
}

/* TODO: Notify clients about PCIe state change */
static irqreturn_t qcom_pcie_ep_global_threaded_irq(int irq, void *data)
{
	struct qcom_pcie_ep *pcie_ep = data;
	struct dw_pcie *pci = &pcie_ep->pci;
	struct device *dev = pci->dev;
	u32 status = readl(pcie_ep->parf + PARF_INT_ALL_STATUS);
	u32 mask = readl(pcie_ep->parf + PARF_INT_ALL_MASK);
	u32 dstate, event, val;

	writel_relaxed(status, pcie_ep->parf + PARF_INT_ALL_CLEAR);
	status &= mask;

	for (event = 1; event < QCOM_PCIE_EP_INT_MAX; event++) {
		if (status & BIT(event)) {
			switch (event) {
			case QCOM_PCIE_EP_INT_LINK_DOWN:
				dev_info(dev, "Received Linkdown event\n");
				pcie_ep->link_status = QCOM_PCIE_EP_LINK_DOWN;
				break;
			case QCOM_PCIE_EP_INT_BME:
				dev_info(dev, "Received BME event. Link is enabled!\n");
				pcie_ep->link_status = QCOM_PCIE_EP_LINK_ENABLED;
				break;
			case QCOM_PCIE_EP_INT_PM_TURNOFF:
				dev_info(dev, "Received PM Turn-off event! Entering L23\n");
				val = readl_relaxed(pcie_ep->parf + PARF_PM_CTRL);
				val |= BIT(2);
				writel_relaxed(val, pcie_ep->parf + PARF_PM_CTRL);
				break;
			case QCOM_PCIE_EP_INT_DSTATE_CHANGE:
				dstate = dw_pcie_readl_dbi(pci, DBI_CON_STATUS) & 0x3;
				dev_info(dev, "Received D%d state event\n", dstate);
				if (dstate == 3) {
					val = readl_relaxed(pcie_ep->parf + PARF_PM_CTRL);
					val |= BIT(1);
					writel_relaxed(val, pcie_ep->parf + PARF_PM_CTRL);
				}
				/* Handle D0 state change */
				break;
			case QCOM_PCIE_EP_INT_LINK_UP:
				dev_info(dev, "Received Linkup event. Enumeration complete!\n");
				dw_pcie_ep_linkup(&pci->ep);
				pcie_ep->link_status = QCOM_PCIE_EP_LINK_UP;
				break;
			default:
				dev_err(dev, "Received unknown event: %d\n", event);
				break;
			}
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t qcom_pcie_ep_perst_threaded_irq(int irq, void *data)
{
	struct qcom_pcie_ep *pcie_ep = data;
	struct dw_pcie *pci = &pcie_ep->pci;
	struct device *dev = pci->dev;
	u32 perst;

	perst = gpiod_get_value(pcie_ep->reset);

	if (perst) {
		/* Start link training */
		dev_info(dev, "PERST de-asserted by host. Starting link training!\n");
		qcom_pcie_establish_link(pci);
	} else {
		/* Shutdown the link if the link is already on */
		dev_info(dev, "PERST asserted by host. Shutting down the PCIe link!\n");
		qcom_pcie_disable_link(pci);
	}

	/* Set trigger type based on the next expected value of perst gpio */
	irq_set_irq_type(gpiod_to_irq(pcie_ep->reset),
			 (perst ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH));

	return IRQ_HANDLED;
}

static int qcom_pcie_ep_enable_irq_resources(struct platform_device *pdev,
					     struct qcom_pcie_ep *pcie_ep)
{
	int irq, ret;

	irq = platform_get_irq_byname(pdev, "global");
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get Global IRQ\n");
		return irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					qcom_pcie_ep_global_threaded_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"global_irq", pcie_ep);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request Global IRQ\n");
		return ret;
	}

	pcie_ep->perst_irq = gpiod_to_irq(pcie_ep->reset);
	irq_set_status_flags(pcie_ep->perst_irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(&pdev->dev, pcie_ep->perst_irq, NULL,
					qcom_pcie_ep_perst_threaded_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"perst_irq", pcie_ep);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request PERST IRQ\n");
		return ret;
	}

	return 0;
}

static int qcom_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				  enum pci_epc_irq_type type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return dw_pcie_ep_raise_legacy_irq(ep, func_no);
	case PCI_EPC_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "Unknown IRQ type\n");
		return -EINVAL;
	}
}

static const struct pci_epc_features qcom_pcie_epc_features = {
	.linkup_notifier = true,
	.core_init_notifier = true,
	.msi_capable = true,
	.msix_capable = false,
};

static const struct pci_epc_features *
qcom_pcie_epc_get_features(struct dw_pcie_ep *pci_ep)
{
	return &qcom_pcie_epc_features;
}

static void qcom_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = BAR_0; bar <= BAR_5; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static struct dw_pcie_ep_ops pci_ep_ops = {
	.ep_init = qcom_pcie_ep_init,
	.raise_irq = qcom_pcie_ep_raise_irq,
	.get_features = qcom_pcie_epc_get_features,
};

static const struct of_device_id qcom_pcie_ep_match[] = {
	{ .compatible = "qcom,sdx55-pcie-ep", },
	{ }
};

static int qcom_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_pcie_ep *pcie_ep;
	int ret;

	pcie_ep = devm_kzalloc(dev, sizeof(*pcie_ep), GFP_KERNEL);
	if (!pcie_ep)
		return -ENOMEM;

	pcie_ep->pci.dev = dev;
	pcie_ep->pci.ops = &pci_ops;
	pcie_ep->pci.ep.ops = &pci_ep_ops;

	ret = qcom_pcie_ep_get_resources(pdev, pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to get resources:%d\n", ret);
		return ret;
	}

	/* Enable power and clocks */
	ret = qcom_pcie_ep_enable_resources(pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to enable resources: %d\n", ret);
		return ret;
	}

	/* Perform controller reset */
	ret = qcom_pcie_ep_core_reset(pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to reset the core: %d\n", ret);
		goto err_disable_resources;
	}

	/* Initialize PHY */
	ret = phy_init(pcie_ep->phy);
	if (ret) {
		dev_err(dev, "Failed to initialize PHY: %d\n", ret);
		goto err_disable_resources;
	}

	/* PHY needs to be powered on for dw_pcie_ep_init() */
	ret = phy_power_on(pcie_ep->phy);
	if (ret) {
		dev_err(dev, "Failed to Power ON PHY: %d\n", ret);
		goto err_phy_exit;
	}

	platform_set_drvdata(pdev, pcie_ep);

	ret = qcom_pcie_ep_enable_irq_resources(pdev, pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to get IRQ resources %d\n", ret);
		goto err_phy_power_off;
	}

	ret = dw_pcie_ep_init(&pcie_ep->pci.ep);
	if (ret) {
		dev_err(dev, "Failed to initialize endpoint:%d\n", ret);
		goto err_phy_power_off;
	}

	return 0;

err_phy_power_off:
	phy_power_off(pcie_ep->phy);
err_phy_exit:
	phy_exit(pcie_ep->phy);
err_disable_resources:
	qcom_pcie_ep_disable_resources(pcie_ep);

	return ret;
}

static struct platform_driver qcom_pcie_ep_driver = {
	.probe	= qcom_pcie_ep_probe,
	.driver	= {
		.name		= "qcom-pcie-ep",
		.suppress_bind_attrs = true,
		.of_match_table	= qcom_pcie_ep_match,
	},
};
builtin_platform_driver(qcom_pcie_ep_driver);

MODULE_AUTHOR("Siddartha Mohanadoss <smohanad@codeaurora.org>");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Qualcomm PCIe Endpoint controller driver");
MODULE_LICENSE("GPL v2");
