// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2019 ASPEED Technology Inc. */
/* Copyright (C) 2019 IBM Corp. */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "sdhci-pltfm.h"

#define ASPEED_SDC_INFO			0x00
#define   ASPEED_SDC_S1_MMC8		BIT(25)
#define   ASPEED_SDC_S0_MMC8		BIT(24)
#define ASPEED_SDC_PHASE		0xf4
#define   ASPEED_SDC_S1_PHASE_IN	GENMASK(25, 21)
#define   ASPEED_SDC_S0_PHASE_IN	GENMASK(20, 16)
#define   ASPEED_SDC_S1_PHASE_OUT	GENMASK(15, 11)
#define   ASPEED_SDC_S1_PHASE_IN_EN	BIT(10)
#define   ASPEED_SDC_S1_PHASE_OUT_EN	GENMASK(9, 8)
#define   ASPEED_SDC_S0_PHASE_OUT	GENMASK(7, 3)
#define   ASPEED_SDC_S0_PHASE_IN_EN	BIT(2)
#define   ASPEED_SDC_S0_PHASE_OUT_EN	GENMASK(1, 0)
#define   ASPEED_SDC_PHASE_MAX		31
#define ASPEED_CLOCK_PHASE 0xf4

struct aspeed_sdc {
	struct clk *clk;
	struct resource *res;

	spinlock_t lock;
	void __iomem *regs;
};

struct aspeed_sdhci_tap_param {
	bool valid;

#define ASPEED_SDHCI_TAP_PARAM_INVERT_CLK	BIT(4)
	u8 in;
	u8 out;
};

struct aspeed_sdhci_tap_desc {
	u32 tap_mask;
	u32 enable_mask;
	u8 enable_value;
};

struct aspeed_sdhci_phase_desc {
	struct aspeed_sdhci_tap_desc in;
	struct aspeed_sdhci_tap_desc out;
};

struct aspeed_sdhci_data {
	unsigned int clk_div_start;
	const struct aspeed_sdhci_phase_desc *phase_desc;
	size_t nr_phase_descs;
	const struct sdhci_pltfm_data *pdata;
};

struct aspeed_sdhci {
	const struct aspeed_sdhci_data *data;
	struct aspeed_sdc *parent;
	u32 width_mask;
	struct mmc_clk_phase_map phase_map;
	const struct aspeed_sdhci_phase_desc *phase_desc;
	struct gpio_desc *pwr_pin;
	struct gpio_desc *pwr_sw_pin;
};

static void aspeed_sdc_configure_8bit_mode(struct aspeed_sdc *sdc,
					   struct aspeed_sdhci *sdhci,
					   bool bus8)
{
	u32 info;

	/* Set/clear 8 bit mode */
	spin_lock(&sdc->lock);
	info = readl(sdc->regs + ASPEED_SDC_INFO);
	if (bus8)
		info |= sdhci->width_mask;
	else
		info &= ~sdhci->width_mask;
	writel(info, sdc->regs + ASPEED_SDC_INFO);
	spin_unlock(&sdc->lock);
}

static u32
aspeed_sdc_set_phase_tap(const struct aspeed_sdhci_tap_desc *desc,
			 u8 tap, bool enable, u32 reg)
{
	reg &= ~(desc->enable_mask | desc->tap_mask);
	if (enable) {
		reg |= tap << __ffs(desc->tap_mask);
		reg |= desc->enable_value << __ffs(desc->enable_mask);
	}

	return reg;
}

static void
aspeed_sdc_set_phase_taps(struct aspeed_sdc *sdc,
			  const struct aspeed_sdhci_phase_desc *desc,
			  const struct aspeed_sdhci_tap_param *taps)
{
	u32 reg;

	spin_lock(&sdc->lock);
	reg = readl(sdc->regs + ASPEED_SDC_PHASE);

	reg = aspeed_sdc_set_phase_tap(&desc->in, taps->in, taps->valid, reg);
	reg = aspeed_sdc_set_phase_tap(&desc->out, taps->out, taps->valid, reg);

	writel(reg, sdc->regs + ASPEED_SDC_PHASE);
	spin_unlock(&sdc->lock);
}

#define PICOSECONDS_PER_SECOND		1000000000000ULL
#define ASPEED_SDHCI_NR_TAPS		15
/* Measured value with *handwave* environmentals and static loading */
#define ASPEED_SDHCI_MAX_TAP_DELAY_PS	1253
static int aspeed_sdhci_phase_to_tap(struct device *dev, unsigned long rate_hz,
				     int phase_deg)
{
	u64 phase_period_ps;
	u64 prop_delay_ps;
	u64 clk_period_ps;
	unsigned int tap;
	u8 inverted;

	phase_deg %= 360;

	if (phase_deg >= 180) {
		inverted = ASPEED_SDHCI_TAP_PARAM_INVERT_CLK;
		phase_deg -= 180;
		dev_dbg(dev,
			"Inverting clock to reduce phase correction from %d to %d degrees\n",
			phase_deg + 180, phase_deg);
	} else {
		inverted = 0;
	}

	prop_delay_ps = ASPEED_SDHCI_MAX_TAP_DELAY_PS / ASPEED_SDHCI_NR_TAPS;
	clk_period_ps = div_u64(PICOSECONDS_PER_SECOND, (u64)rate_hz);
	phase_period_ps = div_u64((u64)phase_deg * clk_period_ps, 360ULL);

	tap = div_u64(phase_period_ps, prop_delay_ps);
	if (tap > ASPEED_SDHCI_NR_TAPS) {
		dev_warn(dev,
			 "Requested out of range phase tap %d for %d degrees of phase compensation at %luHz, clamping to tap %d\n",
			 tap, phase_deg, rate_hz, ASPEED_SDHCI_NR_TAPS);
		tap = ASPEED_SDHCI_NR_TAPS;
	}

	return inverted | tap;
}

static void
aspeed_sdhci_phases_to_taps(struct device *dev, unsigned long rate,
			    const struct mmc_clk_phase *phases,
			    struct aspeed_sdhci_tap_param *taps)
{
	taps->valid = phases->valid;

	if (!phases->valid)
		return;

	taps->in = aspeed_sdhci_phase_to_tap(dev, rate, phases->in_deg);
	taps->out = aspeed_sdhci_phase_to_tap(dev, rate, phases->out_deg);
}

static void
aspeed_sdhci_configure_phase(struct sdhci_host *host, unsigned long rate)
{
	struct aspeed_sdhci_tap_param _taps = {0}, *taps = &_taps;
	struct mmc_clk_phase *params;
	struct aspeed_sdhci *sdhci;
	struct device *dev;

	dev = host->mmc->parent;
	sdhci = sdhci_pltfm_priv(sdhci_priv(host));

	if (!sdhci->phase_desc)
		return;

	params = &sdhci->phase_map.phase[host->timing];
	aspeed_sdhci_phases_to_taps(dev, rate, params, taps);
	aspeed_sdc_set_phase_taps(sdhci->parent, sdhci->phase_desc, taps);
	dev_dbg(dev,
		"Using taps [%d, %d] for [%d, %d] degrees of phase correction at %luHz (%d)\n",
		taps->in & ASPEED_SDHCI_NR_TAPS,
		taps->out & ASPEED_SDHCI_NR_TAPS,
		params->in_deg, params->out_deg, rate, host->timing);
}

static void aspeed_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host;
	unsigned long parent, bus;
	struct aspeed_sdhci *sdhci;
	int div;
	u16 clk;

	pltfm_host = sdhci_priv(host);
	sdhci = sdhci_pltfm_priv(pltfm_host);

	parent = clk_get_rate(pltfm_host->clk);
	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	if (WARN_ON(clock > host->max_clk))
		clock = host->max_clk;

	/*
	 * Regarding the AST2600:
	 *
	 * If (EMMC12C[7:6], EMMC12C[15:8] == 0) then
	 *   period of SDCLK = period of SDMCLK.
	 *
	 * If (EMMC12C[7:6], EMMC12C[15:8] != 0) then
	 *   period of SDCLK = period of SDMCLK * 2 * (EMMC12C[7:6], EMMC[15:8])
	 *
	 * If you keep EMMC12C[7:6] = 0 and EMMC12C[15:8] as one-hot,
	 * 0x1/0x2/0x4/etc, you will find it is compatible to AST2400 or AST2500
	 *
	 * Keep the one-hot behaviour for backwards compatibility except for
	 * supporting the value 0 in (EMMC12C[7:6], EMMC12C[15:8]), and capture
	 * the 0-value capability in clk_div_start.
	 */
	for (div = sdhci->data->clk_div_start; div < 256; div *= 2) {
		bus = parent / div;
		if (bus <= clock)
			break;
	}

	div >>= 1;
	clk = div << SDHCI_DIVIDER_SHIFT;

	aspeed_sdhci_configure_phase(host, bus);

	sdhci_enable_clk(host, clk);
}

static unsigned int aspeed_sdhci_get_max_clock(struct sdhci_host *host)
{
	if (host->mmc->f_max)
		return host->mmc->f_max;

	return sdhci_pltfm_clk_get_max_clock(host);
}

static void aspeed_sdhci_set_bus_width(struct sdhci_host *host, int width)
{
	struct sdhci_pltfm_host *pltfm_priv;
	struct aspeed_sdhci *aspeed_sdhci;
	struct aspeed_sdc *aspeed_sdc;
	u8 ctrl;

	pltfm_priv = sdhci_priv(host);
	aspeed_sdhci = sdhci_pltfm_priv(pltfm_priv);
	aspeed_sdc = aspeed_sdhci->parent;

	/* Set/clear 8-bit mode */
	aspeed_sdc_configure_8bit_mode(aspeed_sdc, aspeed_sdhci,
				       width == MMC_BUS_WIDTH_8);

	/* Set/clear 1 or 4 bit mode */
	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	if (width == MMC_BUS_WIDTH_4)
		ctrl |= SDHCI_CTRL_4BITBUS;
	else
		ctrl &= ~SDHCI_CTRL_4BITBUS;
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static u32 aspeed_sdhci_readl(struct sdhci_host *host, int reg)
{
	u32 val = readl(host->ioaddr + reg);

	if (unlikely(reg == SDHCI_PRESENT_STATE) &&
	    (host->mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH))
		val ^= SDHCI_CARD_PRESENT;

	return val;
}

static void sdhci_aspeed_set_power(struct sdhci_host *host, unsigned char mode,
				   unsigned short vdd)
{
	struct sdhci_pltfm_host *pltfm_priv = sdhci_priv(host);
	struct aspeed_sdhci *dev = sdhci_pltfm_priv(pltfm_priv);
	u8 pwr = 0;

	if (!dev->pwr_pin)
		return sdhci_set_power(host, mode, vdd);

	if (mode != MMC_POWER_OFF) {
		switch (1 << vdd) {
		case MMC_VDD_165_195:
		/*
		 * Without a regulator, SDHCI does not support 2.0v
		 * so we only get here if the driver deliberately
		 * added the 2.0v range to ocr_avail. Map it to 1.8v
		 * for the purpose of turning on the power.
		 */
		case MMC_VDD_20_21:
				pwr = SDHCI_POWER_180;
				break;
		case MMC_VDD_29_30:
		case MMC_VDD_30_31:
				pwr = SDHCI_POWER_300;
				break;
		case MMC_VDD_32_33:
		case MMC_VDD_33_34:
				pwr = SDHCI_POWER_330;
				break;
		default:
				WARN(1, "%s: Invalid vdd %#x\n",
				     mmc_hostname(host->mmc), vdd);
				break;
		}
	}

	if (host->pwr == pwr)
		return;

	host->pwr = pwr;

	if (pwr == 0) {
		gpiod_set_value(dev->pwr_pin, 0);
		sdhci_writeb(host, 0, SDHCI_POWER_CONTROL);
	} else {
		gpiod_set_value(dev->pwr_pin, 1);

		if (dev->pwr_sw_pin) {
			if (pwr & SDHCI_POWER_330)
				gpiod_set_value(dev->pwr_sw_pin, 1);
			else if (pwr & SDHCI_POWER_180)
				gpiod_set_value(dev->pwr_sw_pin, 0);
		}
		pwr |= SDHCI_POWER_ON;
		sdhci_writeb(host, pwr, SDHCI_POWER_CONTROL);
	}
}

static void aspeed_sdhci_voltage_switch(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_priv = sdhci_priv(host);
	struct aspeed_sdhci *dev = sdhci_pltfm_priv(pltfm_priv);

	if (dev->pwr_sw_pin)
		gpiod_set_value(dev->pwr_sw_pin, 0);
}

static const struct sdhci_ops aspeed_sdhci_ops = {
	.read_l = aspeed_sdhci_readl,
	.set_power = sdhci_aspeed_set_power,
	.voltage_switch = aspeed_sdhci_voltage_switch,
	.set_clock = aspeed_sdhci_set_clock,
	.get_max_clock = aspeed_sdhci_get_max_clock,
	.set_bus_width = aspeed_sdhci_set_bus_width,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data ast2400_sdhci_pdata = {
	.ops = &aspeed_sdhci_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN | SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_pltfm_data ast2600_sdhci_pdata = {
	.ops = &aspeed_sdhci_ops,
};

static inline int aspeed_sdhci_calculate_slot(struct aspeed_sdhci *dev,
					      struct resource *res)
{
	resource_size_t delta;

	if (!res || resource_type(res) != IORESOURCE_MEM)
		return -EINVAL;

	if (res->start < dev->parent->res->start)
		return -EINVAL;

	delta = res->start - dev->parent->res->start;
	if (delta & (0x100 - 1))
		return -EINVAL;

	return (delta / 0x100) - 1;
}

static int aspeed_sdhci_probe(struct platform_device *pdev)
{
	const struct aspeed_sdhci_data *aspeed_data;
	struct sdhci_pltfm_host *pltfm_host;
	struct aspeed_sdhci *dev;
	struct sdhci_host *host;
	struct resource *res;
	u32 reg_val;
	int slot;
	int ret;

	aspeed_data = of_device_get_match_data(&pdev->dev);
	if (!aspeed_data) {
		dev_err(&pdev->dev, "Missing platform configuration data\n");
		return -EINVAL;
	}

	host = sdhci_pltfm_init(pdev, aspeed_data->pdata, sizeof(*dev));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	dev = sdhci_pltfm_priv(pltfm_host);
	dev->data = aspeed_data;
	dev->parent = dev_get_drvdata(pdev->dev.parent);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	slot = aspeed_sdhci_calculate_slot(dev, res);

	if (slot < 0)
		return slot;
	else if (slot >= 2)
		return -EINVAL;

	if (slot < dev->data->nr_phase_descs) {
		dev->phase_desc = &dev->data->phase_desc[slot];
	} else {
		dev_info(&pdev->dev,
			 "Phase control not supported for slot %d\n", slot);
		dev->phase_desc = NULL;
	}

	dev->width_mask = !slot ? ASPEED_SDC_S0_MMC8 : ASPEED_SDC_S1_MMC8;

	dev_info(&pdev->dev, "Configured for slot %d\n", slot);

	sdhci_get_of_property(pdev);

	if (of_property_read_bool(pdev->dev.parent->of_node, "mmc-hs200-1_8v") ||
	    of_property_read_bool(pdev->dev.parent->of_node, "sd-uhs-sdr104")) {
		reg_val = readl(host->ioaddr + 0x40);
		/* support 1.8V */
		reg_val |= BIT(26);
		/* write to sdhci140 or sdhci240 mirror register */
		writel(reg_val, dev->parent->regs + (0x10 * (slot + 1)));
	}

	if (of_property_read_bool(pdev->dev.parent->of_node, "sd-uhs-sdr104")) {
		reg_val = readl(host->ioaddr + 0x44);
		/* SDR104 */
		reg_val |= BIT(1);
		/* write to sdhci144 or sdhci244 mirror register */
		writel(reg_val, dev->parent->regs + (0x04 + (slot + 1) * 0x10));
	}

	pltfm_host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pltfm_host->clk))
		return PTR_ERR(pltfm_host->clk);

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable SDIO clock\n");
		goto err_pltfm_free;
	}

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err_sdhci_add;

	if (dev->phase_desc)
		mmc_of_parse_clk_phase(host->mmc, &dev->phase_map);

	dev->pwr_pin = devm_gpiod_get(&pdev->dev, "power", GPIOD_OUT_HIGH);
	if (!IS_ERR(dev->pwr_pin)) {
		gpiod_set_consumer_name(dev->pwr_pin, "mmc_pwr");
		gpiod_direction_output(dev->pwr_pin, 1);
	} else {
		dev->pwr_pin = NULL;
	}

	dev->pwr_sw_pin = devm_gpiod_get(&pdev->dev, "power-switch", GPIOD_OUT_HIGH);
	if (!IS_ERR(dev->pwr_sw_pin)) {
		gpiod_set_consumer_name(dev->pwr_sw_pin, "mmc_pwr_sw");
		gpiod_direction_output(dev->pwr_sw_pin, 0);
	} else {
		dev->pwr_sw_pin = NULL;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	return 0;

err_sdhci_add:
	clk_disable_unprepare(pltfm_host->clk);
err_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int aspeed_sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	int dead = 0;

	host = platform_get_drvdata(pdev);
	pltfm_host = sdhci_priv(host);

	sdhci_remove_host(host, dead);

	clk_disable_unprepare(pltfm_host->clk);

	sdhci_pltfm_free(pdev);

	return 0;
}

static const struct aspeed_sdhci_data ast2400_sdhci_data = {
	.clk_div_start = 2,
	.pdata = &ast2400_sdhci_pdata,
};

static const struct aspeed_sdhci_phase_desc ast2600_sdhci_phase[] = {
	/* SDHCI/Slot 0 */
	[0] = {
		.in = {
			.tap_mask = ASPEED_SDC_S0_PHASE_IN,
			.enable_mask = ASPEED_SDC_S0_PHASE_IN_EN,
			.enable_value = 1,
		},
		.out = {
			.tap_mask = ASPEED_SDC_S0_PHASE_OUT,
			.enable_mask = ASPEED_SDC_S0_PHASE_OUT_EN,
			.enable_value = 3,
		},
	},
	/* SDHCI/Slot 1 */
	[1] = {
		.in = {
			.tap_mask = ASPEED_SDC_S1_PHASE_IN,
			.enable_mask = ASPEED_SDC_S1_PHASE_IN_EN,
			.enable_value = 1,
		},
		.out = {
			.tap_mask = ASPEED_SDC_S1_PHASE_OUT,
			.enable_mask = ASPEED_SDC_S1_PHASE_OUT_EN,
			.enable_value = 3,
		},
	},
};

static const struct aspeed_sdhci_data ast2600_sdhci_data = {
	.clk_div_start = 1,
	.phase_desc = ast2600_sdhci_phase,
	.nr_phase_descs = ARRAY_SIZE(ast2600_sdhci_phase),
	.pdata = &ast2600_sdhci_pdata,
};

static const struct of_device_id aspeed_sdhci_of_match[] = {
	{ .compatible = "aspeed,ast2400-sdhci", .data = &ast2400_sdhci_data, },
	{ .compatible = "aspeed,ast2500-sdhci", .data = &ast2400_sdhci_data, },
	{ .compatible = "aspeed,ast2600-sdhci", .data = &ast2600_sdhci_data, },
	{ }
};

static struct platform_driver aspeed_sdhci_driver = {
	.driver		= {
		.name	= "sdhci-aspeed",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = aspeed_sdhci_of_match,
	},
	.probe		= aspeed_sdhci_probe,
	.remove		= aspeed_sdhci_remove,
};

static int aspeed_sdc_probe(struct platform_device *pdev)

{
	struct device_node *parent, *child;
	struct aspeed_sdc *sdc;
	int ret;
	u32 timing_phase;

	sdc = devm_kzalloc(&pdev->dev, sizeof(*sdc), GFP_KERNEL);
	if (!sdc)
		return -ENOMEM;

	spin_lock_init(&sdc->lock);

	sdc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sdc->clk))
		return PTR_ERR(sdc->clk);

	ret = clk_prepare_enable(sdc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable SDCLK\n");
		return ret;
	}

	sdc->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdc->regs = devm_ioremap_resource(&pdev->dev, sdc->res);
	if (IS_ERR(sdc->regs)) {
		ret = PTR_ERR(sdc->regs);
		goto err_clk;
	}

	if (!of_property_read_u32(pdev->dev.of_node,
				  "timing-phase", &timing_phase))
		writel(timing_phase, sdc->regs + ASPEED_CLOCK_PHASE);

	dev_set_drvdata(&pdev->dev, sdc);

	parent = pdev->dev.of_node;
	for_each_available_child_of_node(parent, child) {
		struct platform_device *cpdev;

		cpdev = of_platform_device_create(child, NULL, &pdev->dev);
		if (!cpdev) {
			of_node_put(child);
			ret = -ENODEV;
			goto err_clk;
		}
	}

	return 0;

err_clk:
	clk_disable_unprepare(sdc->clk);
	return ret;
}

static int aspeed_sdc_remove(struct platform_device *pdev)
{
	struct aspeed_sdc *sdc = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(sdc->clk);

	return 0;
}

static const struct of_device_id aspeed_sdc_of_match[] = {
	{ .compatible = "aspeed,ast2400-sd-controller", },
	{ .compatible = "aspeed,ast2500-sd-controller", },
	{ .compatible = "aspeed,ast2600-sd-controller", },
	{ }
};

MODULE_DEVICE_TABLE(of, aspeed_sdc_of_match);

static struct platform_driver aspeed_sdc_driver = {
	.driver		= {
		.name	= "sd-controller-aspeed",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm	= &sdhci_pltfm_pmops,
		.of_match_table = aspeed_sdc_of_match,
	},
	.probe		= aspeed_sdc_probe,
	.remove		= aspeed_sdc_remove,
};

#if defined(CONFIG_MMC_SDHCI_OF_ASPEED_TEST)
#include "sdhci-of-aspeed-test.c"

static inline int aspeed_sdc_tests_init(void)
{
	return __kunit_test_suites_init(aspeed_sdc_test_suites);
}

static inline void aspeed_sdc_tests_exit(void)
{
	__kunit_test_suites_exit(aspeed_sdc_test_suites);
}
#else
static inline int aspeed_sdc_tests_init(void)
{
	return 0;
}

static inline void aspeed_sdc_tests_exit(void)
{
}
#endif

static int __init aspeed_sdc_init(void)
{
	int rc;

	rc = platform_driver_register(&aspeed_sdhci_driver);
	if (rc < 0)
		return rc;

	rc = platform_driver_register(&aspeed_sdc_driver);
	if (rc < 0)
		goto cleanup_sdhci;

	rc = aspeed_sdc_tests_init();
	if (rc < 0) {
		platform_driver_unregister(&aspeed_sdc_driver);
		goto cleanup_sdhci;
	}

	return 0;

cleanup_sdhci:
	platform_driver_unregister(&aspeed_sdhci_driver);

	return rc;
}
module_init(aspeed_sdc_init);

static void __exit aspeed_sdc_exit(void)
{
	aspeed_sdc_tests_exit();

	platform_driver_unregister(&aspeed_sdc_driver);
	platform_driver_unregister(&aspeed_sdhci_driver);
}
module_exit(aspeed_sdc_exit);

MODULE_DESCRIPTION("Driver for the ASPEED SD/SDIO/SDHCI Controllers");
MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_LICENSE("GPL");
