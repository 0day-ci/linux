// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Copyright (c) 2016-2017, 2020 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/math64.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

/* The frequency range supported is 1 Hz to clock rate */
#define IPQ_PWM_MAX_PERIOD_NS	((u64)NSEC_PER_SEC)

/*
 * The max value specified for each field is based on the number of bits
 * in the pwm control register for that field
 */
#define IPQ_PWM_MAX_DIV		0xFFFF

/*
 * Two 32-bit registers for each PWM: REG0, and REG1.
 * Base offset for PWM #i is at 8 * #i.
 */
#define IPQ_PWM_CFG_REG0 0 /*PWM_DIV PWM_HI*/
#define IPQ_PWM_REG0_PWM_DIV		GENMASK(15, 0)
#define IPQ_PWM_REG0_HI_DURATION	GENMASK(31, 16)

#define IPQ_PWM_CFG_REG1 4 /*ENABLE UPDATE PWM_PRE_DIV*/
#define IPQ_PWM_REG1_PRE_DIV		GENMASK(15, 0)
/*
 * Enable bit is set to enable output toggling in pwm device.
 * Update bit is set to reflect the changed divider and high duration
 * values in register.
 */
#define IPQ_PWM_REG1_UPDATE		BIT(30)
#define IPQ_PWM_REG1_ENABLE		BIT(31)


struct ipq_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	struct regmap *regmap;
	u32 regmap_off;
};

static struct ipq_pwm_chip *to_ipq_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct ipq_pwm_chip, chip);
}

static unsigned int ipq_pwm_reg_read(struct pwm_device *pwm, unsigned reg)
{
	struct ipq_pwm_chip *ipq_chip = to_ipq_pwm_chip(pwm->chip);
	unsigned int off = ipq_chip->regmap_off + 8 * pwm->hwpwm + reg;
	unsigned int val;

	regmap_read(ipq_chip->regmap, off, &val);

	return val;
}

static void ipq_pwm_reg_write(struct pwm_device *pwm, unsigned reg,
		unsigned val)
{
	struct ipq_pwm_chip *ipq_chip = to_ipq_pwm_chip(pwm->chip);
	unsigned int off = ipq_chip->regmap_off + 8 * pwm->hwpwm + reg;

	regmap_write(ipq_chip->regmap, off, val);
}

static void config_div_and_duty(struct pwm_device *pwm, unsigned int pre_div,
			unsigned int pwm_div, unsigned long rate, u64 duty_ns,
			bool enable)
{
	unsigned long hi_dur;
	unsigned long val = 0;

	/*
	 * high duration = pwm duty * (pwm div + 1)
	 * pwm duty = duty_ns / period_ns
	 */
	hi_dur = div64_u64(duty_ns * rate, (pre_div + 1) * NSEC_PER_SEC);

	val = FIELD_PREP(IPQ_PWM_REG0_HI_DURATION, hi_dur) |
		FIELD_PREP(IPQ_PWM_REG0_PWM_DIV, pwm_div);
	ipq_pwm_reg_write(pwm, IPQ_PWM_CFG_REG0, val);

	val = FIELD_PREP(IPQ_PWM_REG1_PRE_DIV, pre_div);
	ipq_pwm_reg_write(pwm, IPQ_PWM_CFG_REG1, val);

	/* Enable needs a separate write to REG1 */
	val |= IPQ_PWM_REG1_UPDATE;
	if (enable)
		val |= IPQ_PWM_REG1_ENABLE;
	ipq_pwm_reg_write(pwm, IPQ_PWM_CFG_REG1, val);
}

static int ipq_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct ipq_pwm_chip *ipq_chip = to_ipq_pwm_chip(chip);
	unsigned long freq;
	unsigned int pre_div, pwm_div, best_pre_div, best_pwm_div;
	long long diff;
	unsigned long rate = clk_get_rate(ipq_chip->clk);
	unsigned long min_diff = rate;
	u64 period_ns, duty_ns;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (state->period < div64_u64(NSEC_PER_SEC, rate))
		return -ERANGE;

	period_ns = min(state->period, IPQ_PWM_MAX_PERIOD_NS);
	duty_ns = min(state->duty_cycle, period_ns);

	/* freq in Hz for period in nano second */
	freq = div64_u64(NSEC_PER_SEC, period_ns);
	best_pre_div = IPQ_PWM_MAX_DIV;
	best_pwm_div = IPQ_PWM_MAX_DIV;
	/* Initial pre_div value such that pwm_div < IPQ_PWM_MAX_DIV */
	pre_div = DIV64_U64_ROUND_UP(period_ns * rate,
			(u64)NSEC_PER_SEC * (IPQ_PWM_MAX_DIV + 1));

	for (; pre_div <= IPQ_PWM_MAX_DIV; pre_div++) {
		pwm_div = DIV64_U64_ROUND_UP(period_ns * rate,
				(u64)NSEC_PER_SEC * (pre_div + 1));
		pwm_div--;

		if (pre_div > pwm_div)
			break;

		/*
		 * Make sure we can do 100% duty cycle where
		 * hi_dur == pwm_div + 1
		 */
		if (pwm_div > IPQ_PWM_MAX_DIV - 1)
			continue;

		diff = ((uint64_t)freq * (pre_div + 1) * (pwm_div + 1))
			- (uint64_t)rate;

		if (diff < 0) /* period larger than requested */
			continue;
		if (diff == 0) { /* bingo */
			best_pre_div = pre_div;
			best_pwm_div = pwm_div;
			break;
		}
		if (diff < min_diff) {
			min_diff = diff;
			best_pre_div = pre_div;
			best_pwm_div = pwm_div;
		}
	}

	/* config divider values for the closest possible frequency */
	config_div_and_duty(pwm, best_pre_div, best_pwm_div,
			    rate, duty_ns, state->enabled);

	return 0;
}

static void ipq_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			      struct pwm_state *state)
{
	struct ipq_pwm_chip *ipq_chip = to_ipq_pwm_chip(chip);
	unsigned long rate = clk_get_rate(ipq_chip->clk);
	unsigned int pre_div, pwm_div, hi_dur;
	u64 effective_div, hi_div;
	u32 reg0, reg1;

	reg0 = ipq_pwm_reg_read(pwm, IPQ_PWM_CFG_REG0);
	reg1 = ipq_pwm_reg_read(pwm, IPQ_PWM_CFG_REG1);

	state->polarity = PWM_POLARITY_NORMAL;
	state->enabled = reg1 & IPQ_PWM_REG1_ENABLE;

	pwm_div = FIELD_GET(IPQ_PWM_REG0_PWM_DIV, reg0);
	hi_dur = FIELD_GET(IPQ_PWM_REG0_HI_DURATION, reg0);
	pre_div = FIELD_GET(IPQ_PWM_REG1_PRE_DIV, reg1);

	/* No overflow here, both pre_div and pwm_div <= 0xffff */
	effective_div = (u64)(pre_div + 1) * (pwm_div + 1);
	state->period = div64_u64(effective_div * NSEC_PER_SEC, rate);

	hi_div = hi_dur * (pre_div + 1);
	state->duty_cycle = div64_u64(hi_div * NSEC_PER_SEC, rate);
}

static const struct pwm_ops ipq_pwm_ops = {
	.apply = ipq_pwm_apply,
	.get_state = ipq_pwm_get_state,
	.owner = THIS_MODULE,
};

static int ipq_pwm_probe(struct platform_device *pdev)
{
	struct ipq_pwm_chip *pwm;
	struct device *dev = &pdev->dev;
	int ret;

	pwm = devm_kzalloc(dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	platform_set_drvdata(pdev, pwm);

	pwm->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(pwm->regmap))
		return dev_err_probe(dev, PTR_ERR(pwm->regmap),
				"regs map failed");

	ret = of_property_read_u32(dev->of_node, "reg", &pwm->regmap_off);
	if (ret)
		return dev_err_probe(dev, ret, "error reading 'reg'");

	pwm->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pwm->clk))
		return dev_err_probe(dev, PTR_ERR(pwm->clk),
				"failed to get clock");

	ret = clk_prepare_enable(pwm->clk);
	if (ret)
		return dev_err_probe(dev, ret, "clock enable failed");

	pwm->chip.dev = dev;
	pwm->chip.ops = &ipq_pwm_ops;
	pwm->chip.npwm = 4;

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err_probe(dev, ret, "pwmchip_add() failed\n");
		clk_disable_unprepare(pwm->clk);
	}

	return ret;
}

static int ipq_pwm_remove(struct platform_device *pdev)
{
	struct ipq_pwm_chip *pwm = platform_get_drvdata(pdev);

	pwmchip_remove(&pwm->chip);
	clk_disable_unprepare(pwm->clk);

	return 0;
}

static const struct of_device_id pwm_ipq_dt_match[] = {
	{ .compatible = "qcom,ipq6018-pwm", },
	{}
};
MODULE_DEVICE_TABLE(of, pwm_ipq_dt_match);

static struct platform_driver ipq_pwm_driver = {
	.driver = {
		.name = "ipq-pwm",
		.of_match_table = pwm_ipq_dt_match,
	},
	.probe = ipq_pwm_probe,
	.remove = ipq_pwm_remove,
};

module_platform_driver(ipq_pwm_driver);

MODULE_LICENSE("Dual BSD/GPL");
