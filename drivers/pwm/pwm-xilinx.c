// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Sean Anderson <sean.anderson@seco.com>
 *
 * For Xilinx LogiCORE IP AXI Timer documentation, refer to DS764:
 * https://www.xilinx.com/support/documentation/ip_documentation/axi_timer/v1_03_a/axi_timer_ds764.pdf
 *
 * Hardware limitations:
 * - When changing both duty cycle and period, we may end up with one cycle
 *   with the old duty cycle and the new period.
 * - Cannot produce 100% duty cycle.
 * - Only produces "normal" output.
 */
#include <asm/io.h>
#include <linux/clk.h>
#include <linux/device.h>
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

/*
 * The idea here is to capture whether the PWM is actually running (e.g.
 * because we or the bootloader set it up) and we need to be careful to ensure
 * we don't cause a glitch. According to the device data sheet, to enable the
 * PWM we need to
 *
 * - Set both timers to generate mode (MDT=1)
 * - Set both timers to PWM mode (PWMA=1)
 * - Enable the generate out signals (GENT=1)
 *
 * In addition,
 *
 * - The timer must be running (ENT=1)
 * - The timer must auto-reload TLR into TCR (ARHT=1)
 * - We must not be in the process of loading TLR into TCR (LOAD=0)
 * - Cascade mode must be disabled (CASC=0)
 *
 * If any of these differ from usual, then the PWM is either disabled, or is
 * running in a mode that this driver does not support.
 */
#define TCSR_RUN_SET (TCSR_GENT | TCSR_ARHT | TCSR_ENT | TCSR_PWMA)
#define TCSR_RUN_CLEAR (TCSR_MDT | TCSR_LOAD)
#define TCSR_RUN_MASK (TCSR_RUN_SET | TCSR_RUN_CLEAR)

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
	return ((TCSR_RUN_MASK | TCSR_CASC) & tcsr0) == TCSR_RUN_SET &&
		(TCSR_RUN_MASK & tcsr1) == TCSR_RUN_SET;
}

static int xilinx_pwm_calc_tlr(struct xilinx_pwm_device *pwm, u32 *tlr, u32 tcsr,
			       unsigned int period)
{
	u64 max_count = BIT_ULL(pwm->width) - 1;
	u64 cycles = DIV_ROUND_DOWN_ULL((u64)period * clk_get_rate(pwm->clk),
					NSEC_PER_SEC);

	if (cycles < 2)
		return -ERANGE;

	if (tcsr & TCSR_UDT) {
		if (cycles - 2 > max_count)
			return -ERANGE;
		*tlr = cycles - 2;
	} else {
		if (cycles > max_count + 2)
			return -ERANGE;
		*tlr = max_count - cycles + 2;
	}

	return 0;
}

static unsigned int xilinx_pwm_get_period(struct xilinx_pwm_device *pwm,
					  u32 tlr, u32 tcsr)
{
	u64 cycles;

	if (tcsr & TCSR_UDT)
		cycles = tlr + 2;
	else
		cycles = (BIT_ULL(pwm->width) - 1) - tlr + 2;

	return DIV_ROUND_UP_ULL(cycles * NSEC_PER_SEC, clk_get_rate(pwm->clk));
}

static int xilinx_pwm_apply(struct pwm_chip *chip, struct pwm_device *unused,
			    const struct pwm_state *state)
{
	int ret;
	struct xilinx_pwm_device *pwm = xilinx_pwm_chip_to_device(chip);
	u32 tlr0, tlr1;
	u32 tcsr0 = readl(pwm->regs + TCSR0);
	u32 tcsr1 = readl(pwm->regs + TCSR1);
	bool enabled = xilinx_pwm_is_enabled(tcsr0, tcsr1);

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	ret = xilinx_pwm_calc_tlr(pwm, &tlr0, tcsr0, state->period);
	if (ret)
		return ret;

	ret = xilinx_pwm_calc_tlr(pwm, &tlr1, tcsr1, state->duty_cycle);
	if (ret)
		return ret;

	if (!enabled && state->enabled)
		clk_rate_exclusive_get(pwm->clk);

	writel(tlr0, pwm->regs + TLR0);
	writel(tlr1, pwm->regs + TLR1);

	if (state->enabled) {
		/* Only touch the TCSRs if we aren't already running */
		if (!enabled) {
			/* Load TLR into TCR */
			writel(tcsr0 | TCSR_LOAD, pwm->regs + TCSR0);
			writel(tcsr1 | TCSR_LOAD, pwm->regs + TCSR1);
			/* Enable timers all at once with ENALL */
			tcsr0 = (TCSR_RUN_SET & ~TCSR_ENT) | (tcsr0 & TCSR_UDT);
			tcsr1 = TCSR_RUN_SET | TCSR_ENALL | (tcsr1 & TCSR_UDT);
			writel(tcsr0, pwm->regs + TCSR0);
			writel(tcsr1, pwm->regs + TCSR1);
		}
	} else {
		writel(tcsr0 & ~TCSR_RUN_SET, pwm->regs + TCSR0);
		writel(tcsr1 & ~TCSR_RUN_SET, pwm->regs + TCSR1);
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
	.owner = THIS_MODULE,
};

static int xilinx_pwm_probe(struct platform_device *pdev)
{
	bool enabled = false;
	int i, ret;
	struct device *dev = &pdev->dev;
	struct xilinx_pwm_device *pwm;
	u32 one_timer;

	ret = of_property_read_u32(dev->of_node, "xlnx,one-timer-only",
				   &one_timer);
	if (ret || one_timer)
		return dev_err_probe(dev, -EINVAL,
				     "two timers are needed for PWM mode\n");

	for (i = 0; i < 2; i++) {
		char fmt[] = "xlnx,gen%u-assert";
		char buf[sizeof(fmt)];
		u32 gen;

		snprintf(buf, sizeof(buf), fmt, i);
		ret = of_property_read_u32(dev->of_node, buf, &gen);
		if (ret || !gen)
			return dev_err_probe(dev, -EINVAL,
					     "generateout%u must be active high\n",
					     i);
	}

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;
	platform_set_drvdata(pdev, pwm);

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &xilinx_pwm_ops;
	pwm->chip.npwm = 1;

	pwm->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pwm->regs))
		return PTR_ERR(pwm->regs);

	ret = of_property_read_u32(dev->of_node, "xlnx,count-width", &pwm->width);
	if (ret || pwm->width < 8 || pwm->width > 32)
		return dev_err_probe(dev, -EINVAL,
				     "missing or invalid counter width\n");

	pwm->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pwm->clk))
		return dev_err_probe(dev, PTR_ERR(pwm->clk), "missing clock\n");

	ret = clk_prepare_enable(pwm->clk);
	if (ret)
		return dev_err_probe(dev, ret, "clock enable failed\n");

	enabled = xilinx_pwm_is_enabled(readl(pwm->regs + TCSR0),
					readl(pwm->regs + TCSR1));
	if (enabled)
		clk_rate_exclusive_get(pwm->clk);

	ret = pwmchip_add(&pwm->chip);
	if (ret) {
		dev_err_probe(dev, ret, "could not register pwm chip\n");
		if (enabled)
			clk_rate_exclusive_put(pwm->clk);
		clk_disable_unprepare(pwm->clk);
	}
	return ret;
}

static int xilinx_pwm_remove(struct platform_device *pdev)
{
	struct xilinx_pwm_device *pwm = platform_get_drvdata(pdev);
	bool enabled = xilinx_pwm_is_enabled(readl(pwm->regs + TCSR0),
					     readl(pwm->regs + TCSR1));

	pwmchip_remove(&pwm->chip);
	if (enabled)
		clk_rate_exclusive_put(pwm->clk);
	clk_disable_unprepare(pwm->clk);

	return 0;
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
module_platform_driver(xilinx_pwm_driver);

MODULE_ALIAS("platform:xilinx-pwm");
MODULE_DESCRIPTION("Xilinx LogiCORE IP AXI Timer PWM driver");
MODULE_LICENSE("GPL v2");
