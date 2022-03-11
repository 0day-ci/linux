// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Jianjun Wang <jianjun.wang@mediatek.com>
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#include "phy-mtk-io.h"

#define PEXTP_ANA_GLB_00_REG		0x9000
#define PEXTP_ANA_LN0_TX_REG		0xA004
#define PEXTP_ANA_LN0_RX_REG		0xA03C
#define PEXTP_ANA_LN1_TX_REG		0xA104
#define PEXTP_ANA_LN1_RX_REG		0xA13c

/* PEXTP_GLB_00_RG[28:24] Internal Resistor Selection of TX Bias Current */
#define EFUSE_GLB_INTR_SEL		GENMASK(28, 24)
#define EFUSE_GLB_INTR_VAL(x)		((0x1f & (x)) << 24)

/* PEXTP_ANA_LN_RX_RG[3:0] LN0 RX impedance selection */
#define EFUSE_LN_RX_SEL			GENMASK(3, 0)
#define EFUSE_LN_RX_VAL(x)		(0xf & (x))

/* PEXTP_ANA_LN_TX_RG[5:2] LN0 TX PMOS impedance selection */
#define EFUSE_LN_TX_PMOS_SEL		GENMASK(5, 2)
#define EFUSE_LN_TX_PMOS_VAL(x)		((0xf & (x)) << 2)

/* PEXTP_ANA_LN_TX_RG[11:8] LN0 TX NMOS impedance selection */
#define EFUSE_LN_TX_NMOS_SEL		GENMASK(11, 8)
#define EFUSE_LN_TX_NMOS_VAL(x)		((0xf & (x)) << 8)

struct mtk_pcie_phy {
	struct device *dev;
	struct phy *phy;
	void __iomem *sif_base;
};

static int mtk_pcie_phy_init(struct phy *phy)
{
	struct mtk_pcie_phy *pcie_phy = phy_get_drvdata(phy);
	struct device *dev = pcie_phy->dev;
	bool nvmem_enabled;
	u32 glb_intr, tx_pmos, tx_nmos, rx_data;
	int ret;

	nvmem_enabled = device_property_read_bool(dev, "nvmem-cells");
	if (!nvmem_enabled)
		return 0;

	/* Set efuse value for lane0 */
	ret = nvmem_cell_read_variable_le_u32(dev, "tx_ln0_pmos", &tx_pmos);
	if (ret) {
		dev_err(dev, "%s: Failed to read tx_ln0_pmos\n", __func__);
		return ret;
	}

	ret = nvmem_cell_read_variable_le_u32(dev, "tx_ln0_nmos", &tx_nmos);
	if (ret) {
		dev_err(dev, "%s: Failed to read tx_ln0_nmos\n", __func__);
		return ret;
	}

	ret = nvmem_cell_read_variable_le_u32(dev, "rx_ln0", &rx_data);
	if (ret) {
		dev_err(dev, "%s: Failed to read rx_ln0\n", __func__);
		return ret;
	}

	/* Don't wipe the old data if there is no data in efuse cell */
	if (!(tx_pmos || tx_nmos || rx_data)) {
		dev_warn(dev, "%s: No efuse data found, but dts enable it\n",
			 __func__);
		return 0;
	}

	mtk_phy_update_bits(pcie_phy->sif_base + PEXTP_ANA_LN0_TX_REG,
			    EFUSE_LN_TX_PMOS_SEL,
			    EFUSE_LN_TX_PMOS_VAL(tx_pmos));

	mtk_phy_update_bits(pcie_phy->sif_base + PEXTP_ANA_LN0_TX_REG,
			    EFUSE_LN_TX_NMOS_SEL,
			    EFUSE_LN_TX_NMOS_VAL(tx_nmos));

	mtk_phy_update_bits(pcie_phy->sif_base + PEXTP_ANA_LN0_RX_REG,
			    EFUSE_LN_RX_SEL, EFUSE_LN_RX_VAL(rx_data));

	/* Set global data */
	ret = nvmem_cell_read_variable_le_u32(dev, "glb_intr", &glb_intr);
	if (ret) {
		dev_err(dev, "%s: Failed to read glb_intr\n", __func__);
		return ret;
	}

	mtk_phy_update_bits(pcie_phy->sif_base + PEXTP_ANA_GLB_00_REG,
			    EFUSE_GLB_INTR_SEL, EFUSE_GLB_INTR_VAL(glb_intr));

	/*
	 * Set efuse value for lane1, only available for the platform which
	 * supports two lane.
	 */
	ret = nvmem_cell_read_variable_le_u32(dev, "tx_ln1_pmos", &tx_pmos);
	if (ret) {
		dev_err(dev, "%s: Failed to read tx_ln1_pmos, efuse value not support for lane 1\n",
			__func__);
		return 0;
	}

	ret = nvmem_cell_read_variable_le_u32(dev, "tx_ln1_nmos", &tx_nmos);
	if (ret) {
		dev_err(dev, "%s: Failed to read tx_ln1_pmos\n", __func__);
		return ret;
	}

	ret = nvmem_cell_read_variable_le_u32(dev, "rx_ln1", &rx_data);
	if (ret) {
		dev_err(dev, "%s: Failed to read rx_ln1\n", __func__);
		return ret;
	}

	if (!(tx_pmos || tx_nmos || rx_data))
		return 0;

	mtk_phy_update_bits(pcie_phy->sif_base + PEXTP_ANA_LN1_TX_REG,
			    EFUSE_LN_TX_PMOS_SEL,
			    EFUSE_LN_TX_PMOS_VAL(tx_pmos));

	mtk_phy_update_bits(pcie_phy->sif_base + PEXTP_ANA_LN1_TX_REG,
			    EFUSE_LN_TX_NMOS_SEL,
			    EFUSE_LN_TX_NMOS_VAL(tx_nmos));

	mtk_phy_update_bits(pcie_phy->sif_base + PEXTP_ANA_LN1_RX_REG,
			    EFUSE_LN_RX_SEL, EFUSE_LN_RX_VAL(rx_data));

	return 0;
}

static const struct phy_ops mtk_pcie_phy_ops = {
	.init	= mtk_pcie_phy_init,
	.owner	= THIS_MODULE,
};

static int mtk_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct mtk_pcie_phy *pcie_phy;

	pcie_phy = devm_kzalloc(dev, sizeof(*pcie_phy), GFP_KERNEL);
	if (!pcie_phy)
		return -ENOMEM;

	pcie_phy->dev = dev;

	pcie_phy->sif_base = devm_platform_ioremap_resource_byname(pdev, "sif");
	if (IS_ERR(pcie_phy->sif_base)) {
		dev_err(dev, "%s: Failed to map phy-sif base\n", __func__);
		return PTR_ERR(pcie_phy->sif_base);
	}

	pcie_phy->phy = devm_phy_create(dev, dev->of_node, &mtk_pcie_phy_ops);
	if (IS_ERR(pcie_phy->phy)) {
		dev_err(dev, "%s: Failed to create PCIe phy\n", __func__);
		return PTR_ERR(pcie_phy->phy);
	}

	phy_set_drvdata(pcie_phy->phy, pcie_phy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id mtk_pcie_phy_of_match[] = {
	{ .compatible = "mediatek,pcie-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_pcie_phy_of_match);

static struct platform_driver mtk_pcie_phy_driver = {
	.probe	= mtk_pcie_phy_probe,
	.driver	= {
		.name = "mtk-pcie-phy",
		.of_match_table = mtk_pcie_phy_of_match,
	},
};
module_platform_driver(mtk_pcie_phy_driver);

MODULE_DESCRIPTION("MediaTek PCIe PHY driver");
MODULE_AUTHOR("Jianjun Wang <jianjun.wang@mediatek.com>");
MODULE_LICENSE("GPL v2");
