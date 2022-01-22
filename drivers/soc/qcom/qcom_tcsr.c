// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#define TCSR_USB_PORT_SEL_REG			0xb0
#define TCSR_USB_PORT_SEL_MASK			GENMASK(1, 0)

#define TCSR_USB_SELECT_USB3_P0			FIELD_PREP(TCSR_USB_PORT_SEL_MASK, 0x1)
#define TCSR_USB_SELECT_USB3_P1			FIELD_PREP(TCSR_USB_PORT_SEL_MASK, 0x2)
#define TCSR_USB_SELECT_USB3_DUAL		FIELD_PREP(TCSR_USB_PORT_SEL_MASK, 0x3)

/* IPQ40xx HS PHY Mode Select */
#define TCSR_USB_HSPHY_CONFIG_REG		0xc
#define TCSR_USB_HSPHY_MODE_MASK		BIT(21)
#define TCSR_USB_HSPHY_MODE_HOST_MODE		FIELD_PREP(TCSR_USB_HSPHY_MODE_MASK, 0x0)
#define TCSR_USB_HSPHY_MODE_DEVICE_MODE		FIELD_PREP(TCSR_USB_HSPHY_MODE_MASK, 0x1)

/* IPQ40xx ess interface mode select */
#define TCSR_ESS_INTERFACE_SEL_REG		0x0
#define TCSR_ESS_INTERFACE_SEL_MASK		GENMASK(3, 0)
#define TCSR_ESS_PSGMII				FIELD_PREP(TCSR_ESS_INTERFACE_SEL_MASK, 0x0)
#define TCSR_ESS_PSGMII_RGMII5			FIELD_PREP(TCSR_ESS_INTERFACE_SEL_MASK, 0x1)
#define TCSR_ESS_PSGMII_RMII0			FIELD_PREP(TCSR_ESS_INTERFACE_SEL_MASK, 0x2)
#define TCSR_ESS_PSGMII_RMII1			FIELD_PREP(TCSR_ESS_INTERFACE_SEL_MASK, 0x4)
#define TCSR_ESS_PSGMII_RMII0_RMII1		FIELD_PREP(TCSR_ESS_INTERFACE_SEL_MASK, 0x6)
#define TCSR_ESS_PSGMII_RGMII4			FIELD_PREP(TCSR_ESS_INTERFACE_SEL_MASK, 0x9)

/* IPQ40xx WiFi Global Config */
#define TCSR_WIFI0_GLB_CFG_OFFSET_REG		0x0
#define TCSR_WIFI1_GLB_CFG_OFFSET_REG		0x4
/* Enable AXI master bus Axid translating to confirm all txn submitted by order */
#define TCSR_WIFI_GLB_CFG_AXID_EN		BIT(30)
/* 1:  use locally generate socslv_wxi_bvalid for performance.
 * 0:  use SNOC socslv_wxi_bvalid.
 */
#define TCSR_WIFI_GLB_CFG_SOCSLV_WXI_BVALID	BIT(24)
#define TCSR_WIFI_GLB_CFG_SOCSLV_SNOC		FIELD_PREP(TCSR_WIFI_GLB_CFG_SOCSLV_WXI_BVALID, 0x0)
#define TCSR_WIFI_GLB_CFG_SOCSLV_LOCAL		FIELD_PREP(TCSR_WIFI_GLB_CFG_SOCSLV_WXI_BVALID, 0x1)

/* Configure special wifi memory type needed for some IPQ40xx devices */
#define TCSR_PNOC_SNOC_MEMTYPE_M0_M2_REG	0x4
#define TCSR_WIFI_NOC_MEMTYPE_MASK		GENMASK(26, 24)
#define TCSR_WIFI_NOC_MEMTYPE_M0_M2		FIELD_PREP(TCSR_WIFI_NOC_MEMTYPE_MASK, 0x2)

static int qcom_tcsr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct regmap *tcsr;
	int ret, val;

	node = dev->of_node;
	tcsr = syscon_node_to_regmap(node);
	if (IS_ERR(tcsr))
		return PTR_ERR(tcsr);

	if (of_find_property(node, "qcom,usb-ctrl-select", NULL) &&
	    of_device_is_compatible(node, "qcom,tcsr-ipq8064")) {
		if (of_property_match_string(node, "qcom,usb-ctrl-select",
					     "p0")) {
			val = TCSR_USB_SELECT_USB3_P0;
		} else if (of_property_match_string(node, "qcom,usb-ctrl-select",
						  "p1")) {
			val = TCSR_USB_SELECT_USB3_P1;
		} else if (of_property_match_string(node, "qcom,usb-ctrl-select",
						  "dual")) {
			val = TCSR_USB_SELECT_USB3_DUAL;
		} else {
			dev_err(dev, "invalid value for qcom,usb-ctrl-select");
			return -EINVAL;
		}

		ret = regmap_update_bits(tcsr, TCSR_USB_PORT_SEL_REG,
					 TCSR_USB_PORT_SEL_MASK, val);
		if (ret)
			return ret;
	}

	if (of_find_property(node, "qcom,usb-hsphy-mode-select", NULL) &&
	    of_device_is_compatible(node, "qcom,tcsr-ipq4019")) {
		if (of_property_match_string(node, "qcom,usb-hsphy-mode-select",
					     "host")) {
			val = TCSR_USB_HSPHY_MODE_HOST_MODE;
		} else if (of_property_match_string(node, "qcom,usb-hsphy-mode-select",
						  "device")) {
			val = TCSR_USB_HSPHY_MODE_DEVICE_MODE;
		} else {
			dev_err(dev, "invalid value for qcom,usb-hsphy-mode-select");
			return -EINVAL;
		}

		ret = regmap_update_bits(tcsr, TCSR_USB_HSPHY_CONFIG_REG,
					 TCSR_USB_HSPHY_MODE_MASK, val);
		if (ret)
			return ret;
	}

	if (of_find_property(node, "qcom,ess-interface-select", NULL) &&
	    of_device_is_compatible(node, "qcom,tcsr-ipq4019")) {
		if (of_property_match_string(node, "qcom,ess-interface-select",
					     "psgmii")) {
			val = TCSR_ESS_PSGMII;
		} else if (of_property_match_string(node, "qcom,ess-interface-select",
						  "rgmii5")) {
			val = TCSR_ESS_PSGMII_RGMII5;
		} else if (of_property_match_string(node, "qcom,ess-interface-select",
						  "rmii0")) {
			val = TCSR_ESS_PSGMII_RMII0;
		} else if (of_property_match_string(node, "qcom,ess-interface-select",
						  "rmii1")) {
			val = TCSR_ESS_PSGMII_RMII1;
		} else if (of_property_match_string(node, "qcom,ess-interface-select",
						  "rmii0_rmii1")) {
			val = TCSR_ESS_PSGMII_RMII0_RMII1;
		} else if (of_property_match_string(node, "qcom,ess-interface-select",
						  "rgmii4")) {
			val = TCSR_ESS_PSGMII_RGMII4;
		} else {
			dev_err(dev, "invalid value for qcom,ess-interface-select");
			return -EINVAL;
		}

		ret = regmap_update_bits(tcsr, TCSR_ESS_INTERFACE_SEL_REG,
					 TCSR_ESS_INTERFACE_SEL_MASK, val);
		if (ret)
			return ret;
	}

	if (of_find_property(node, "qcom,wifi-glb-cfg-enable-axid", NULL) &&
	    of_device_is_compatible(node, "qcom,tcsr-ipq4019")) {
		ret = regmap_set_bits(tcsr, TCSR_WIFI0_GLB_CFG_OFFSET_REG,
				      TCSR_WIFI_GLB_CFG_AXID_EN);
		ret = regmap_set_bits(tcsr, TCSR_WIFI1_GLB_CFG_OFFSET_REG,
				      TCSR_WIFI_GLB_CFG_AXID_EN);
		if (ret)
			return ret;
	}

	if (of_find_property(node, "qcom,wifi-glb-cfg-socslv-mode", NULL) &&
	    of_device_is_compatible(node, "qcom,tcsr-ipq4019")) {
		if (of_property_match_string(node, "qcom,wifi-glb-cfg-socslv-mode",
					     "snoc")) {
			val = TCSR_WIFI_GLB_CFG_SOCSLV_SNOC;
		} else if (of_property_match_string(node, "qcom,wifi-glb-cfg-socslv-mode",
						  "local")) {
			val = TCSR_WIFI_GLB_CFG_SOCSLV_SNOC;
		} else {
			dev_err(dev, "invalid value for qcom,wifi-glb-cfg-socslv-mode");
			return -EINVAL;
		}

		ret = regmap_update_bits(tcsr, TCSR_WIFI0_GLB_CFG_OFFSET_REG,
					 TCSR_WIFI_GLB_CFG_SOCSLV_WXI_BVALID, val);
		ret = regmap_update_bits(tcsr, TCSR_WIFI1_GLB_CFG_OFFSET_REG,
					 TCSR_WIFI_GLB_CFG_SOCSLV_WXI_BVALID, val);
	}

	if (of_find_property(node, "qcom,wifi_noc_memtype_m0_m2", NULL) &&
	    of_device_is_compatible(node, "qcom,tcsr-ipq4019")) {
		ret = regmap_update_bits(tcsr, TCSR_PNOC_SNOC_MEMTYPE_M0_M2_REG,
					 TCSR_WIFI_NOC_MEMTYPE_MASK,
					 TCSR_WIFI_NOC_MEMTYPE_M0_M2);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id qcom_tcsr_dt_match[] = {
	{ .compatible = "qcom,tcsr-ipq8064", },
	{ .compatible = "qcom,tcsr-ipq4019", },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_tcsr_dt_match);

static struct platform_driver qcom_tcsr_driver = {
	.probe = qcom_tcsr_probe,
	.driver = {
		.name		= "qcom-tcsr",
		.of_match_table	= qcom_tcsr_dt_match,
	},
};

module_platform_driver(qcom_tcsr_driver);

MODULE_AUTHOR("Ansuel Smith <ansuelsmth@gmail.com>");
MODULE_DESCRIPTION("QCOM TCSR driver");
MODULE_LICENSE("GPL v2");
