// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe phy driver for Kirin 970
 *
 * Copyright (C) 2017 HiSilicon Electronics Co., Ltd.
 *		https://www.huawei.com
 * Copyright (C) 2021 Huawei Technologies Co., Ltd.
 *		https://www.huawei.com
 *
 * Authors:
 *	Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
 *	Manivannan Sadhasivam <mani@kernel.org>
 *
 * Based on:
 * 	https://lore.kernel.org/lkml/4c9d6581478aa966698758c0420933f5defab4dd.1612335031.git.mchehab+huawei@kernel.org/
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define AXI_CLK_FREQ			207500000
#define REF_CLK_FREQ			100000000

/* PCIe CTRL registers */
#define SOC_PCIECTRL_CTRL0_ADDR   0x000
#define SOC_PCIECTRL_CTRL1_ADDR   0x004
#define SOC_PCIECTRL_CTRL7_ADDR   0x01c
#define SOC_PCIECTRL_CTRL12_ADDR  0x030
#define SOC_PCIECTRL_CTRL20_ADDR  0x050
#define SOC_PCIECTRL_CTRL21_ADDR  0x054
#define SOC_PCIECTRL_STATE0_ADDR  0x400

/* PCIe PHY registers */
#define SOC_PCIEPHY_CTRL0_ADDR    0x000
#define SOC_PCIEPHY_CTRL1_ADDR    0x004
#define SOC_PCIEPHY_CTRL2_ADDR    0x008
#define SOC_PCIEPHY_CTRL3_ADDR    0x00c
#define SOC_PCIEPHY_CTRL38_ADDR   0x0098
#define SOC_PCIEPHY_STATE0_ADDR   0x400

#define PCIE_LINKUP_ENABLE            (0x8020)
#define PCIE_ELBI_SLV_DBI_ENABLE      (0x1 << 21)
#define PCIE_LTSSM_ENABLE_BIT         (0x1 << 11)
#define PCIEPHY_RESET_BIT             (0x1 << 17)
#define PCIEPHY_PIPE_LINE0_RESET_BIT  (0x1 << 19)

#define PORT_MSI_CTRL_ADDR            0x820
#define PORT_MSI_CTRL_UPPER_ADDR      0x824
#define PORT_MSI_CTRL_INT0_ENABLE     0x828

#define EYEPARAM_NOCFG 0xFFFFFFFF
#define RAWLANEN_DIG_PCS_XF_TX_OVRD_IN_1 0x3001
#define SUP_DIG_LVL_OVRD_IN 0xf
#define LANEN_DIG_ASIC_TX_OVRD_IN_1 0x1002
#define LANEN_DIG_ASIC_TX_OVRD_IN_2 0x1003

/* hi3670 pciephy register */
#define SOC_PCIEPHY_MMC1PLL_CTRL1  0xc04
#define SOC_PCIEPHY_MMC1PLL_CTRL16 0xC40
#define SOC_PCIEPHY_MMC1PLL_CTRL17 0xC44
#define SOC_PCIEPHY_MMC1PLL_CTRL20 0xC50
#define SOC_PCIEPHY_MMC1PLL_CTRL21 0xC54
#define SOC_PCIEPHY_MMC1PLL_STAT0  0xE00

#define CRGPERIPH_PEREN12   0x470
#define CRGPERIPH_PERDIS12  0x474
#define CRGPERIPH_PCIECTRL0 0x800

/* define ie,oe cfg */
#define IO_IE_EN_HARD_BYPASS         (0x1 << 27)
#define IO_OE_EN_HARD_BYPASS         (0x1 << 11)
#define IO_HARD_CTRL_DEBOUNCE_BYPASS (0x1 << 10)
#define IO_OE_GT_MODE                (0x2 << 7)
#define DEBOUNCE_WAITCFG_IN          (0xf << 20)
#define DEBOUNCE_WAITCFG_OUT         (0xf << 13)

/* noc power domain */
#define NOC_POWER_IDLEREQ_1 0x38c
#define NOC_POWER_IDLE_1    0x394
#define NOC_PW_MASK         0x10000
#define NOC_PW_SET_BIT      0x1

/* Number of GPIOs required by PHY */
#define MAX_GPIO_RESETS		4
#define MAX_GPIO_CLKREQ		3
#define NUM_EYEPARAM		5

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

/* peri_crg ctrl */
#define CRGCTRL_PCIE_ASSERT_OFFSET	0x88
#define CRGCTRL_PCIE_ASSERT_BIT		0x8c000000

/* Time for delay */
#define REF_2_PERST_MIN		20000
#define REF_2_PERST_MAX		25000
#define PERST_2_ACCESS_MIN	10000
#define PERST_2_ACCESS_MAX	12000
#define PIPE_CLK_WAIT_MIN	550
#define PIPE_CLK_WAIT_MAX	600
#define TIME_CMOS_MIN		100
#define TIME_CMOS_MAX		105
#define TIME_PHY_PD_MIN		10
#define TIME_PHY_PD_MAX		11

struct hi3670_pcie_phy {
	struct device	*dev;
	void __iomem	*base;
	struct regmap	*apb;
	struct regmap	*crgctrl;
	struct regmap	*sysctrl;
	struct regmap	*pmctrl;
	struct clk	*apb_sys_clk;
	struct clk	*apb_phy_clk;
	struct clk	*phy_ref_clk;
	struct clk	*pcie_aclk;
	struct clk	*pcie_aux_clk;
	int		n_gpio_resets;
	int		n_gpio_clkreq;
	int		gpio_id_reset[MAX_GPIO_RESETS];
	const char	*reset_names[MAX_GPIO_RESETS];
	int		gpio_id_clkreq[MAX_GPIO_CLKREQ];
	const char	*clkreq_names[MAX_GPIO_CLKREQ];
	u32		eye_param[NUM_EYEPARAM];
};


/* Registers in PCIePHY */
static inline void hi3670_apb_phy_writel(struct hi3670_pcie_phy *phy,
					 u32 val, u32 reg)
{
	writel(val, phy->base + 0x40000 + reg);
}

static inline u32 hi3670_apb_phy_readl(struct hi3670_pcie_phy *phy, u32 reg)
{
	return readl(phy->base + 0x40000 + reg);
}

static inline void kirin_apb_natural_phy_writel(struct hi3670_pcie_phy *phy,
						u32 val, u32 reg)
{
	writel(val, phy->base + reg * 4);
}

static inline u32 kirin_apb_natural_phy_readl(struct hi3670_pcie_phy *phy,
					      u32 reg)
{
	return readl(phy->base + reg * 4);
}

static void hi3670_pcie_phy_oe_enable(struct hi3670_pcie_phy *phy)
{
	u32 val;

	regmap_read(phy->sysctrl, SCTRL_PCIE_OE_OFFSET, &val);
	val |= PCIE_DEBOUNCE_PARAM;
	val &= ~PCIE_OE_BYPASS;
	regmap_write(phy->sysctrl, SCTRL_PCIE_OE_OFFSET, val);
}

void hi3670_pcie_get_eyeparam(struct hi3670_pcie_phy *phy)
{
	struct device *dev = phy->dev;
	struct device_node *np;
	int ret, i;

	np = dev->of_node;

	ret = of_property_read_u32_array(np, "hisilicon,eye-diagram-param",
					 phy->eye_param, NUM_EYEPARAM);
	if (!ret)
		return;

	/* There's no optional eye_param property. Set array to default */
	for (i = 0; i < NUM_EYEPARAM; i++)
		phy->eye_param[i] = EYEPARAM_NOCFG;
}

static void hi3670_pcie_set_eyeparam(struct hi3670_pcie_phy *phy)
{
	u32 val;

	val = kirin_apb_natural_phy_readl(phy, RAWLANEN_DIG_PCS_XF_TX_OVRD_IN_1);

	if (phy->eye_param[1] != EYEPARAM_NOCFG) {
		val &= (~0xf00);
		val |= (phy->eye_param[1] << 8) | (0x1 << 12);
	}
	kirin_apb_natural_phy_writel(phy, val, RAWLANEN_DIG_PCS_XF_TX_OVRD_IN_1);

	val = kirin_apb_natural_phy_readl(phy, LANEN_DIG_ASIC_TX_OVRD_IN_2);
	val &= (~0x1FBF);
	if (phy->eye_param[2] != EYEPARAM_NOCFG)
		val |= (phy->eye_param[2]<< 0) | (0x1 << 6);

	if (phy->eye_param[3] != EYEPARAM_NOCFG)
		val |= (phy->eye_param[3] << 7) | (0x1 << 13);

	kirin_apb_natural_phy_writel(phy, val, LANEN_DIG_ASIC_TX_OVRD_IN_2);

	val = kirin_apb_natural_phy_readl(phy, SUP_DIG_LVL_OVRD_IN);
	if (phy->eye_param[0] != EYEPARAM_NOCFG) {
		val &= (~0x1C0);
		val |= (phy->eye_param[0] << 6) | (0x1 << 9);
	}
	kirin_apb_natural_phy_writel(phy, val, SUP_DIG_LVL_OVRD_IN);

	val = kirin_apb_natural_phy_readl(phy, LANEN_DIG_ASIC_TX_OVRD_IN_1);
	if (phy->eye_param[4] != EYEPARAM_NOCFG) {
		val &= (~0x7E00);
		val |= (phy->eye_param[4] << 9) | (0x1 << 15);
	}
	kirin_apb_natural_phy_writel(phy, val, LANEN_DIG_ASIC_TX_OVRD_IN_1);
}

static int hi3670_pcie_gpio_request(struct hi3670_pcie_phy *phy,
				    struct device *dev)
{
	int ret, i;

	for (i = 0; i < phy->n_gpio_resets; i++) {
		if (!gpio_is_valid(phy->gpio_id_reset[i])) {
			dev_err(dev, "unable to get a valid %s gpio\n",
				phy->reset_names[i]);
			return -ENODEV;
		}

		ret = devm_gpio_request(dev, phy->gpio_id_reset[i],
					phy->reset_names[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < phy->n_gpio_clkreq; i++) {
		if (!gpio_is_valid(phy->gpio_id_clkreq[i])) {
			dev_err(dev, "unable to get a valid %s gpio\n",
				phy->clkreq_names[i]);
			return -ENODEV;
		}

		ret = devm_gpio_request(dev, phy->gpio_id_clkreq[i],
					phy->clkreq_names[i]);
		if (ret)
			return ret;
	}

	return ret;
}

static void hi3670_pcie_natural_cfg(struct hi3670_pcie_phy *phy)
{
	u32 val;

	/* change 2p mem_ctrl */
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL20_ADDR, 0x02605550);

	/* pull up sys_aux_pwr_det */
	regmap_read(phy->apb, SOC_PCIECTRL_CTRL7_ADDR, &val);
	val |= (0x1 << 10);
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL7_ADDR, val);

	/* output, pull down */
	regmap_read(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, &val);
	val &= ~(0x3 << 2);
	val |= (0x1 << 1);
	val &= ~(0x1 << 0);
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, val);

	/* Handle phy_reset and lane0_reset to HW */
	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_CTRL1_ADDR);
	val |= PCIEPHY_RESET_BIT;
	val &= ~PCIEPHY_PIPE_LINE0_RESET_BIT;
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_CTRL1_ADDR);

	/* fix chip bug: TxDetectRx fail */
	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_CTRL38_ADDR);
	val |= (0x1 << 2);
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_CTRL38_ADDR);
}

static void hi3670_pcie_pll_init(struct hi3670_pcie_phy *phy)
{
	u32 val;

	/* choose FNPLL */
	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL1);
	val |= (0x1 << 27);
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL1);

	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL16);
	val &= 0xF000FFFF;
	/* fnpll fbdiv = 0xD0 */
	val |= (0xd0 << 16);
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL16);

	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL17);
	val &= 0xFF000000;
	/* fnpll fracdiv = 0x555555 */
	val |= (0x555555 << 0);
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL17);

	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL20);
	val &= 0xF5FF88FF;
	/* fnpll dll_en = 0x1 */
	val |= (0x1 << 27);
	/* fnpll postdiv1 = 0x5 */
	val |= (0x5 << 8);
	/* fnpll postdiv2 = 0x4 */
	val |= (0x4 << 12);
	/* fnpll pll_mode = 0x0 */
	val &= ~(0x1 << 25);
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL20);

	hi3670_apb_phy_writel(phy, 0x20, SOC_PCIEPHY_MMC1PLL_CTRL21);
}

static int hi3670_pcie_pll_ctrl(struct hi3670_pcie_phy *phy, bool enable)
{
	struct device *dev = phy->dev;
	u32 val;
	int time = 200;

	if (enable) {
		/* pd = 0 */
		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL16);
		val &= ~(0x1 << 0);
		hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL16);

		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_STAT0);

		/* choose FNPLL */
		while (!(val & 0x10)) {
			if (!time) {
				dev_err(dev, "wait for pll_lock timeout\n");
				return -1;
			}
			time --;
			udelay(1);
			val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_STAT0);
		}

		/* pciepll_bp = 0 */
		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL20);
		val &= ~(0x1 << 16);
		hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL20);

	} else {
		/* pd = 1 */
		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL16);
		val |= (0x1 << 0);
		hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL16);

		/* pciepll_bp = 1 */
		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_CTRL20);
		val |= (0x1 << 16);
		hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_MMC1PLL_CTRL20);
	}

	 return 0;
}

static void hi3670_pcie_hp_debounce_gt(struct hi3670_pcie_phy *phy, bool open)
{
	if (open)
		/* gt_clk_pcie_hp/gt_clk_pcie_debounce open */
		regmap_write(phy->crgctrl, CRGPERIPH_PEREN12, 0x9000);
	else
		/* gt_clk_pcie_hp/gt_clk_pcie_debounce close */
		regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12, 0x9000);
}

static void hi3670_pcie_phyref_gt(struct hi3670_pcie_phy *phy, bool open)
{
	unsigned int val;

	regmap_read(phy->crgctrl, CRGPERIPH_PCIECTRL0, &val);

	if (open)
		val &= ~(0x1 << 1); //enable hard gt mode
	else
		val |= (0x1 << 1); //disable hard gt mode

	regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);

	/* disable soft gt mode */
	regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12, 0x4000);
}

static void hi3670_pcie_oe_ctrl(struct hi3670_pcie_phy *phy, bool en_flag)
{
	unsigned int val;

	regmap_read(phy->crgctrl , CRGPERIPH_PCIECTRL0, &val);

	/* set ie cfg */
	val |= IO_IE_EN_HARD_BYPASS;

	/* set oe cfg */
	val &= ~IO_HARD_CTRL_DEBOUNCE_BYPASS;

	/* set phy_debounce in&out time */
	val |= (DEBOUNCE_WAITCFG_IN | DEBOUNCE_WAITCFG_OUT);

	/* select oe_gt_mode */
	val |= IO_OE_GT_MODE;

	if (en_flag)
		val &= ~IO_OE_EN_HARD_BYPASS;
	else
		val |= IO_OE_EN_HARD_BYPASS;

	regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);
}

static void hi3670_pcie_ioref_gt(struct hi3670_pcie_phy *phy, bool open)
{
	unsigned int val;

	if (open) {
		regmap_write(phy->apb, SOC_PCIECTRL_CTRL21_ADDR, 0x20000070);

		hi3670_pcie_oe_ctrl(phy, true);

		/* en hard gt mode */
		regmap_read(phy->crgctrl, CRGPERIPH_PCIECTRL0, &val);
		val &= ~(0x1 << 0);
		regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);

		/* disable soft gt mode */
		regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12, 0x2000);

	} else {
		/* disable hard gt mode */
		regmap_read(phy->crgctrl, CRGPERIPH_PCIECTRL0, &val);
		val |= (0x1 << 0);
		regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);

		/* disable soft gt mode */
		regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12, 0x2000);

		hi3670_pcie_oe_ctrl(phy, false);
       }
}

static int hi3670_pcie_allclk_ctrl(struct hi3670_pcie_phy *phy, bool clk_on)
{
	struct device *dev = phy->dev;
	u32 val;
	int ret = 0;

	if (!clk_on)
		goto close_clocks;

	/* choose 100MHz clk src: Bit[8]==1 pad, Bit[8]==0 pll */
	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_CTRL1_ADDR);
	val &= ~(0x1 << 8);
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_CTRL1_ADDR);

	hi3670_pcie_pll_init(phy);

	ret = hi3670_pcie_pll_ctrl(phy, true);
	if (ret) {
		dev_err(dev, "Failed to enable pll\n");
		return -1;
	}
	hi3670_pcie_hp_debounce_gt(phy, true);
	hi3670_pcie_phyref_gt(phy, true);
	hi3670_pcie_ioref_gt(phy, true);

	ret = clk_set_rate(phy->pcie_aclk, AXI_CLK_FREQ);
	if (ret) {
		dev_err(dev, "Failed to set rate\n");
		goto close_clocks;
	}

	return 0;

close_clocks:
	hi3670_pcie_ioref_gt(phy, false);
	hi3670_pcie_phyref_gt(phy, false);
	hi3670_pcie_hp_debounce_gt(phy, false);

	hi3670_pcie_pll_ctrl(phy, false);

	return ret;
}

static bool is_pipe_clk_stable(struct hi3670_pcie_phy *phy)
{
	struct device *dev = phy->dev;
	u32 val;
	u32 time = 100;
	u32 pipe_clk_stable = 0x1 << 19;

	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_STATE0_ADDR);
	while (val & pipe_clk_stable) {
		mdelay(1);
		if (time == 0) {
			dev_err(dev, "PIPE clk is not stable\n");
			return false;
		}
		time--;
		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_STATE0_ADDR);
	}

	return true;
}

static int hi3670_pcie_noc_power(struct hi3670_pcie_phy *phy, bool enable)
{
	struct device *dev = phy->dev;
	u32 time = 100;
	unsigned int val = NOC_PW_MASK;
	int rst;

	if (enable)
		val = NOC_PW_MASK | NOC_PW_SET_BIT;
	else
		val = NOC_PW_MASK;
	rst = enable ? 1 : 0;

	regmap_write(phy->pmctrl, NOC_POWER_IDLEREQ_1, val);

	time = 100;
	regmap_read(phy->pmctrl, NOC_POWER_IDLE_1, &val);
	while((val & NOC_PW_SET_BIT) != rst) {
		udelay(10);
		if (!time) {
			dev_err(dev, "Failed to reverse noc power-status\n");
			return -1;
		}
		time--;
		regmap_read(phy->pmctrl, NOC_POWER_IDLE_1, &val);
	}

	return 0;
}

static int hi3670_pcie_get_apb(struct hi3670_pcie_phy *phy)
{
	struct device_node *pcie_port;
	struct device *dev = phy->dev;
	struct device *pcie_dev;

	pcie_port = of_get_child_by_name(dev->parent->of_node, "pcie");
	if (!pcie_port) {
		dev_err(dev, "no pcie node found in %s\n",
			dev->parent->of_node->full_name);
		return -ENODEV;
	}

	pcie_dev = bus_find_device_by_of_node(&platform_bus_type, pcie_port);
	if (!pcie_dev) {
                dev_err(dev, "Didn't find pcie device\n");
                return -ENODEV;
        }

        /*
	 * We might just use NULL instead of the APB name, as the
	 * pcie-kirin currently registers directly just one regmap (although
	 * the DWC driver register other regmaps).
	 *
	 * Yet, it sounds safer to warrant that it will be accessing the
	 * right regmap. So, let's use the named version.
	 */
	phy->apb = dev_get_regmap(pcie_dev, "kirin_pcie_apb");
	if (!phy->apb) {
		dev_err(dev, "Failed to get APB regmap\n");
		return -ENODEV;
	}

	return 0;
}


static int kirin_pcie_clk_ctrl(struct hi3670_pcie_phy *phy, bool enable)
{
	int ret = 0;

	if (!enable)
		goto close_clk;

	ret = clk_set_rate(phy->phy_ref_clk, REF_CLK_FREQ);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy->phy_ref_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy->apb_sys_clk);
	if (ret)
		goto apb_sys_fail;

	ret = clk_prepare_enable(phy->apb_phy_clk);
	if (ret)
		goto apb_phy_fail;

	ret = clk_prepare_enable(phy->pcie_aclk);
	if (ret)
		goto aclk_fail;

	ret = clk_prepare_enable(phy->pcie_aux_clk);
	if (ret)
		goto aux_clk_fail;

	return 0;

close_clk:
	clk_disable_unprepare(phy->pcie_aux_clk);
aux_clk_fail:
	clk_disable_unprepare(phy->pcie_aclk);
aclk_fail:
	clk_disable_unprepare(phy->apb_phy_clk);
apb_phy_fail:
	clk_disable_unprepare(phy->apb_sys_clk);
apb_sys_fail:
	clk_disable_unprepare(phy->phy_ref_clk);

	return ret;
}

static int hi3670_pcie_phy_init(struct phy *generic_phy)
{
	struct hi3670_pcie_phy *phy = phy_get_drvdata(generic_phy);
	struct device *dev = phy->dev;
	int ret;

	/*
	 * The code under hi3670_pcie_get_apb() need to access the
	 * DWC APB registers. So, get them from
	 * the pcie driver's regmap (see pcie-kirin regmap).
	 *
	 * Such kind of resource can only be obtained during the PCIe
	 * power_on sequence, as the code inside pcie-kirin needs to
	 * be already probed, as it needs to register the APB regmap.
	 */

	ret = hi3670_pcie_get_apb(phy);
	if (ret)
		return ret;

	/* phy regulator needs to be powered on before calling it */
	return hi3670_pcie_gpio_request(phy, dev);
}

static int hi3670_pcie_phy_power_on(struct phy *generic_phy)
{
	struct hi3670_pcie_phy *phy = phy_get_drvdata(generic_phy);
	int val, ret, i;

	/* Power supply for Host */
	regmap_write(phy->sysctrl,
		     SCTRL_PCIE_CMOS_OFFSET, SCTRL_PCIE_CMOS_BIT);
	usleep_range(TIME_CMOS_MIN, TIME_CMOS_MAX);

	hi3670_pcie_phy_oe_enable(phy);

	for (i = 0; i < phy->n_gpio_clkreq; i++) {
		ret = gpio_direction_output(phy->gpio_id_clkreq[i], 0);
		if (ret)
			return ret;
	}

	ret = kirin_pcie_clk_ctrl(phy, true);
	if (ret)
		return ret;

	/* ISO disable, PCIeCtrl, PHY assert and clk gate clear */
	regmap_write(phy->sysctrl,
		     SCTRL_PCIE_ISO_OFFSET, SCTRL_PCIE_ISO_BIT);
	regmap_write(phy->crgctrl,
		     CRGCTRL_PCIE_ASSERT_OFFSET, CRGCTRL_PCIE_ASSERT_BIT);
	regmap_write(phy->sysctrl,
		     SCTRL_PCIE_HPCLK_OFFSET, SCTRL_PCIE_HPCLK_BIT);

	hi3670_pcie_natural_cfg(phy);

	ret = hi3670_pcie_allclk_ctrl(phy, true);
	if (ret)
		goto disable_clks;

	/* pull down phy_test_powerdown signal */
	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_CTRL0_ADDR);
	val &= ~(0x1 << 22);
	hi3670_apb_phy_writel(phy, val, SOC_PCIEPHY_CTRL0_ADDR);

	/* deassert controller perst_n */
	regmap_read(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, &val);
	val |= (0x1 << 2);
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, val);
	udelay(10);

	/* perst assert Endpoints */
	usleep_range(21000, 23000);
	for (i = 0; i < phy->n_gpio_resets; i++) {
		ret = gpio_direction_output(phy->gpio_id_reset[i], 1);
		if (ret)
			return ret;
	}
	usleep_range(10000, 11000);

	ret = is_pipe_clk_stable(phy);
	if (!ret)
		goto disable_clks;

	hi3670_pcie_set_eyeparam(phy);

	ret = hi3670_pcie_noc_power(phy, false);
	if (ret)
		goto disable_clks;

	return 0;

disable_clks:
	kirin_pcie_clk_ctrl(phy, false);
	return ret;
}

static int hi3670_pcie_phy_power_off(struct phy *generic_phy)
{
	struct hi3670_pcie_phy *phy = phy_get_drvdata(generic_phy);

	/* Drop power supply for Host */
	regmap_write(phy->sysctrl, SCTRL_PCIE_CMOS_OFFSET, 0x00);

	kirin_pcie_clk_ctrl(phy, false);

	return 0;
}

static const struct phy_ops hi3670_phy_ops = {
	.init		= hi3670_pcie_phy_init,
	.power_on	= hi3670_pcie_phy_power_on,
	.power_off	= hi3670_pcie_phy_power_off,
	.owner		= THIS_MODULE,
};

static int hi3670_pcie_phy_get_resources(struct hi3670_pcie_phy *phy,
					 struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	char name[32];
	int i;

	/* syscon */
	phy->crgctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3670-crgctrl");
	if (IS_ERR(phy->crgctrl))
		return PTR_ERR(phy->crgctrl);

	phy->sysctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3670-sctrl");
	if (IS_ERR(phy->sysctrl))
		return PTR_ERR(phy->sysctrl);

	phy->pmctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3670-pmctrl");
	if (IS_ERR(phy->sysctrl))
		return PTR_ERR(phy->sysctrl);

	/* clocks */
	phy->phy_ref_clk = devm_clk_get(dev, "pcie_phy_ref");
	if (IS_ERR(phy->phy_ref_clk))
		return PTR_ERR(phy->phy_ref_clk);

	phy->pcie_aux_clk = devm_clk_get(dev, "pcie_aux");
	if (IS_ERR(phy->pcie_aux_clk))
		return PTR_ERR(phy->pcie_aux_clk);

	phy->apb_phy_clk = devm_clk_get(dev, "pcie_apb_phy");
	if (IS_ERR(phy->apb_phy_clk))
		return PTR_ERR(phy->apb_phy_clk);

	phy->apb_sys_clk = devm_clk_get(dev, "pcie_apb_sys");
	if (IS_ERR(phy->apb_sys_clk))
		return PTR_ERR(phy->apb_sys_clk);

	phy->pcie_aclk = devm_clk_get(dev, "pcie_aclk");
	if (IS_ERR(phy->pcie_aclk))
		return PTR_ERR(phy->pcie_aclk);

	/* registers */
	phy->base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	/* perst reset gpios */
	phy->n_gpio_resets = of_gpio_named_count(np, "reset-gpios");
	if (phy->n_gpio_resets > MAX_GPIO_RESETS) {
		dev_err(dev, "Too many GPIO resets!\n");
		return -EINVAL;
	}
	for (i = 0; i < phy->n_gpio_resets; i++) {
		phy->gpio_id_reset[i] = of_get_named_gpio(dev->of_node,
							  "reset-gpios", i);
		if (phy->gpio_id_reset[i] < 0)
			return phy->gpio_id_reset[i];

		sprintf(name, "pcie_perst_%d", i);

		phy->reset_names[i] = devm_kstrdup_const(dev, name,
							 GFP_KERNEL);
		if (!phy->reset_names[i])
			return -ENOMEM;
	}

	/* clock request gpios */
	phy->n_gpio_clkreq = of_gpio_named_count(np, "clkreq-gpios");
	if (phy->n_gpio_clkreq > MAX_GPIO_CLKREQ) {
		dev_err(dev, "Too many GPIO clock requests!\n");
		return -EINVAL;
	}
	for (i = 0; i < phy->n_gpio_clkreq; i++) {
		phy->gpio_id_clkreq[i] = of_get_named_gpio(dev->of_node,
							   "clkreq-gpios", i);
		if (phy->gpio_id_clkreq[i] < 0)
			return phy->gpio_id_clkreq[i];

		sprintf(name, "pcie_clkreq_%d", i);
		phy->clkreq_names[i] = devm_kstrdup_const(dev, name,
							  GFP_KERNEL);
		if (!phy->clkreq_names[i])
			return -ENOMEM;
	}

	hi3670_pcie_get_eyeparam(phy);

	return 0;
}

static int hi3670_pcie_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct hi3670_pcie_phy *phy;
	struct phy *generic_phy;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->dev = dev;

	ret = hi3670_pcie_phy_get_resources(phy, pdev);
	if (ret)
		return ret;

	generic_phy = devm_phy_create(dev, dev->of_node, &hi3670_phy_ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id hi3670_pcie_phy_match[] = {
	{
		.compatible = "hisilicon,hi970-pcie-phy",
	},
	{},
};

static struct platform_driver hi3670_pcie_phy_driver = {
	.probe	= hi3670_pcie_phy_probe,
	.driver = {
		.of_match_table	= hi3670_pcie_phy_match,
		.name		= "hi3670_pcie_phy",
		.suppress_bind_attrs = true,
	}
};
builtin_platform_driver(hi3670_pcie_phy_driver);
