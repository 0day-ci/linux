// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Sean Anderson <sean.anderson@seco.com>
 */
#include <asm/io.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define TCSR0	0x00
#define TLR0	0x04
#define TCR0	0x08
#define TCSR1	0x10
#define TLR1	0x14
#define TCR1	0x18

#define TCSR_MDT	BIT(0)
#define TCSR_UDT	BIT(1)
#define TCSR_GENT	BIT(2)
#define TCSR_CAPT	BIT(3)
#define TCSR_ARHT	BIT(4)
#define TCSR_LOAD	BIT(5)
#define TCSR_ENIT	BIT(6)
#define TCSR_ENT	BIT(7)
#define TCSR_TINT	BIT(8)
#define TCSR_PWMA	BIT(9)
#define TCSR_ENALL	BIT(10)
#define TCSR_CASC	BIT(11)

/* Bits we need to set/clear to enable PWM mode, not including CASC */
#define TCSR_SET (TCSR_GENT | TCSR_ARHT | TCSR_ENT | TCSR_PWMA)
#define TCSR_CLEAR (TCSR_MDT | TCSR_LOAD)

#define NSEC_PER_SEC_ULL 1000000000ULL

/**
 * struct xilinx_pwm_device - Driver data for Xilinx AXI timer PWM driver
 * @chip: PWM controller chip
 * @clk: Parent clock
 * @regs: Base address of this device
 * @width: Width of the counters, in bits
 */
struct xilinx_pwm_device {
	struct pwm_chip chip;
	struct clk *clk;
	void __iomem *regs;
	unsigned int width;
};

static inline struct xilinx_pwm_device *xilinx_pwm_chip_to_device(struct pwm_chip *chip)
{
	return container_of(chip, struct xilinx_pwm_device, chip);
}

static bool xilinx_pwm_is_enabled(u32 tcsr0, u32 tcsr1)
{
	return !(~tcsr0 & TCSR_SET || tcsr0 & (TCSR_CLEAR | TCSR_CASC) ||
		 ~tcsr1 & TCSR_SET || tcsr1 & TCSR_CLEAR);
}

static u32 xilinx_pwm_calc_tlr(struct xilinx_pwm_device *pwm, u32 tcsr,
			       unsigned int period)
{
	u64 cycles = DIV_ROUND_DOWN_ULL(period * clk_get_rate(pwm->clk),
					NSEC_PER_SEC_ULL);

	if (tcsr & TCSR_UDT)
		return cycles - 2;
	else
		return (BIT_ULL(pwm->width) - 1) - cycles + 2;
}

static unsigned int xilinx_pwm_get_period(struct xilinx_pwm_device *pwm,
					  u32 tlr, u32 tcsr)
{
	u64 cycles;

	if (tcsr & TCSR_UDT)
		cycles = tlr + 2;
	else
		cycles = (BIT_ULL(pwm->width) - 1) - tlr + 2;

	return DIV_ROUND_DOWN_ULL(cycles * NSEC_PER_SEC_ULL,
				  clk_get_rate(pwm->clk));
}

static int xilinx_pwm_apply(struct pwm_chip *chip, struct pwm_device *unused,
			    const struct pwm_state *state)
{
	struct xilinx_pwm_device *pwm = xilinx_pwm_chip_to_device(chip);
	u32 tlr0, tlr1;
	u32 tcsr0 = readl(pwm->regs + TCSR0);
	u32 tcsr1 = readl(pwm->regs + TCSR1);
	bool enabled = xilinx_pwm_is_enabled(tcsr0, tcsr1);

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!enabled && state->enabled)
		clk_rate_exclusive_get(pwm->clk);

	tlr0 = xilinx_pwm_calc_tlr(pwm, tcsr0, state->period);
	tlr1 = xilinx_pwm_calc_tlr(pwm, tcsr1, state->duty_cycle);
	writel(tlr0, pwm->regs + TLR0);
	writel(tlr1, pwm->regs + TLR1);

	if (state->enabled) {
		/* Only touch the TCSRs if we aren't already running */
		if (!enabled) {
			/* Load TLR into TCR */
			writel(tcsr0 | TCSR_LOAD, pwm->regs + TCSR0);
			writel(tcsr1 | TCSR_LOAD, pwm->regs + TCSR1);
			/* Enable timers all at once with ENALL */
			tcsr0 = (TCSR_SET & ~TCSR_ENT) | (tcsr0 & TCSR_UDT);
			tcsr1 = TCSR_SET | TCSR_ENALL | (tcsr1 & TCSR_UDT);
			writel(tcsr0, pwm->regs + TCSR0);
			writel(tcsr1, pwm->regs + TCSR1);
		}
	} else {
		writel(tcsr0 & ~TCSR_ENT, pwm->regs + TCSR0);
		writel(tcsr1 & ~TCSR_ENT, pwm->regs + TCSR1);
	}

	if (enabled && !state->enabled)
		clk_rate_exclusive_put(pwm->clk);

	return 0;
}

static void xilinx_pwm_get_state(struct pwm_chip *chip,
				 struct pwm_device *unused,
				 struct pwm_state *state)
{
	struct xilinx_pwm_device *pwm = xilinx_pwm_chip_to_device(chip);
	u32 tlr0, tlr1, tcsr0, tcsr1;

	tlr0 = readl(pwm->regs + TLR0);
	tlr1 = readl(pwm->regs + TLR1);
	tcsr0 = readl(pwm->regs + TCSR0);
	tcsr1 = readl(pwm->regs + TCSR1);

	state->period = xilinx_pwm_get_period(pwm, tlr0, tcsr0);
	state->duty_cycle = xilinx_pwm_get_period(pwm, tlr1, tcsr1);
	state->enabled = xilinx_pwm_is_enabled(tcsr0, tcsr1);
	state->polarity = PWM_POLARITY_NORMAL;
}

static const struct pwm_ops xilinx_pwm_ops = {
	.apply = xilinx_pwm_apply,
	.get_state = xilinx_pwm_get_state,
};

static struct dentry *debug_dir;

#define DEBUG_REG(reg) { .name = #reg, .offset = (reg), }
static struct debugfs_reg32 xilinx_pwm_reg32[] = {
	DEBUG_REG(TCSR0),
	DEBUG_REG(TLR0),
	DEBUG_REG(TCR0),
	DEBUG_REG(TCSR1),
	DEBUG_REG(TLR1),
	DEBUG_REG(TCR1),
};

static int xilinx_pwm_debug_create(struct xilinx_pwm_device *pwm)
{
	struct debugfs_regset32 *regset;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return 0;

	regset = devm_kzalloc(pwm->chip.dev, sizeof(*regset), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	regset->regs = xilinx_pwm_reg32;
	regset->nregs = ARRAY_SIZE(xilinx_pwm_reg32);
	regset->base = pwm->regs;

	debugfs_create_regset32(dev_name(pwm->chip.dev), 0400, debug_dir,
				regset);
	return 0;
}

static int xilinx_pwm_probe(struct platform_device *pdev)
{
	bool enabled;
	int i, ret;
	struct device *dev = &pdev->dev;
	struct xilinx_pwm_device *pwm;
	u32 one_timer;

	ret = of_property_read_u32(dev->of_node, "xlnx,one-timer-only",
				   &one_timer);
	if (ret || one_timer) {
		dev_err(dev, "two timers are needed for PWM mode\n");
		return -EINVAL;
	}

	for (i = 0; i < 2; i++) {
		char fmt[] = "xlnx,gen%u-assert";
		char buf[sizeof(fmt)];
		u32 gen;

		snprintf(buf, sizeof(buf), fmt, i);
		ret = of_property_read_u32(dev->of_node, buf, &gen);
		if (ret || !gen) {
			dev_err(dev, "generateout%u must be active high\n", i);
			return -EINVAL;
		}
	}

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;
	platform_set_drvdata(pdev, pwm);

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &xilinx_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 1;

	pwm->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pwm->regs))
		return PTR_ERR(pwm->regs);

	ret = of_property_read_u32(dev->of_node, "xlnx,count-width", &pwm->width);
	if (ret) {
		dev_err(dev, "missing counter width\n");
		return -EINVAL;
	}

	pwm->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pwm->clk))
		return dev_err_probe(dev, PTR_ERR(pwm->clk), "missing clock\n");

	ret = clk_prepare_enable(pwm->clk);
	if (ret) {
		dev_err(dev, "clock enable failed\n");
		return ret;
	}

	ret = xilinx_pwm_debug_create(pwm);
	if (ret) {
		clk_disable_unprepare(pwm->clk);
		return ret;
	}

	ret = pwmchip_add(&pwm->chip);
	if (ret) {
		clk_disable_unprepare(pwm->clk);
		return ret;
	}

	enabled = xilinx_pwm_is_enabled(readl(pwm->regs + TCSR0),
					readl(pwm->regs + TCSR1));
	if (enabled)
		clk_rate_exclusive_get(pwm->clk);

	return ret;
}

static int xilinx_pwm_remove(struct platform_device *pdev)
{
	struct xilinx_pwm_device *pwm = platform_get_drvdata(pdev);
	bool enabled = xilinx_pwm_is_enabled(readl(pwm->regs + TCSR0),
					     readl(pwm->regs + TCSR1));

	if (enabled)
		clk_rate_exclusive_put(pwm->clk);
	clk_disable_unprepare(pwm->clk);
	return pwmchip_remove(&pwm->chip);
}

static const struct of_device_id xilinx_pwm_of_match[] = {
	{ .compatible = "xlnx,xps-timer-1.00.a" },
	{ .compatible = "xlnx,axi-timer-2.0" },
	{},
};
MODULE_DEVICE_TABLE(of, xilinx_pwm_of_match);

static struct platform_driver xilinx_pwm_driver = {
	.probe = xilinx_pwm_probe,
	.remove = xilinx_pwm_remove,
	.driver = {
		.name = "xilinx-pwm",
		.of_match_table = of_match_ptr(xilinx_pwm_of_match),
	},
};

static int __init xilinx_pwm_init(void)
{
	int ret = platform_driver_register(&xilinx_pwm_driver);

	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		debug_dir = debugfs_create_dir(xilinx_pwm_driver.driver.name, NULL);
		if (IS_ERR(debug_dir)) {
			platform_driver_unregister(&xilinx_pwm_driver);
			return PTR_ERR(debug_dir);
		}
	}
	return 0;
}
module_init(xilinx_pwm_init);

static void __exit xilinx_pwm_exit(void)
{
	platform_driver_unregister(&xilinx_pwm_driver);
	if (IS_ENABLED(CONFIG_DEBUG_FS))
		debugfs_remove_recursive(debug_dir);
}
module_exit(xilinx_pwm_exit);

MODULE_ALIAS("platform:xilinx-pwm");
MODULE_DESCRIPTION("Xilinx LogiCORE IP AXI Timer PWM driver");
MODULE_LICENSE("GPL v2");
