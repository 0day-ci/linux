// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ASPEED Technology Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or later as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/pwm.h>
/* The channel number of Aspeed pwm controller */
#define ASPEED_NR_PWMS 16
/* PWM Control Register */
#define ASPEED_PWM_CTRL_CH(ch) ((ch * 0x10) + 0x00)
#define PWM_LOAD_SEL_AS_WDT BIT(19)
#define LOAD_SEL_FALLING 0
#define LOAD_SEL_RIGING 1
#define PWM_DUTY_LOAD_AS_WDT_EN BIT(18)
#define PWM_DUTY_SYNC_DIS BIT(17)
#define PWM_CLK_ENABLE BIT(16)
#define PWM_LEVEL_OUTPUT BIT(15)
#define PWM_INVERSE BIT(14)
#define PWM_OPEN_DRAIN_EN BIT(13)
#define PWM_PIN_EN BIT(12)
#define PWM_CLK_DIV_H_SHIFT 8
#define PWM_CLK_DIV_H_MASK (0xf << PWM_CLK_DIV_H_SHIFT)
#define PWM_CLK_DIV_L_SHIFT 0
#define PWM_CLK_DIV_L_MASK (0xff << PWM_CLK_DIV_L_SHIFT)
/* PWM Duty Cycle Register */
#define ASPEED_PWM_DUTY_CYCLE_CH(ch) ((ch * 0x10) + 0x04)
#define PWM_PERIOD_SHIFT (24)
#define PWM_PERIOD_MASK (0xff << PWM_PERIOD_SHIFT)
#define PWM_POINT_AS_WDT_SHIFT (16)
#define PWM_POINT_AS_WDT_MASK (0xff << PWM_POINT_AS_WDT_SHIFT)
#define PWM_FALLING_POINT_SHIFT (8)
#define PWM_FALLING_POINT_MASK (0xffff << PWM_FALLING_POINT_SHIFT)
#define PWM_RISING_POINT_SHIFT (0)
#define PWM_RISING_POINT_MASK (0xffff << PWM_RISING_POINT_SHIFT)
/* PWM default value */
#define DEFAULT_PWM_PERIOD 0xff
#define DEFAULT_TARGET_PWM_FREQ 25000
#define DEFAULT_DUTY_PT 10
#define DEFAULT_WDT_RELOAD_DUTY_PT 16

struct aspeed_pwm_data {
	struct pwm_chip chip;
	struct regmap *regmap;
	unsigned long clk_freq;
	struct reset_control *reset;
};
/**
 * struct aspeed_pwm - per-PWM driver data
 * @freq: cached pwm freq
 */
struct aspeed_pwm {
	u32 freq;
};

static void aspeed_set_pwm_channel_enable(struct regmap *regmap, u8 pwm_channel,
					  bool enable)
{
	regmap_update_bits(regmap, ASPEED_PWM_CTRL_CH(pwm_channel),
			   (PWM_CLK_ENABLE | PWM_PIN_EN),
			   enable ? (PWM_CLK_ENABLE | PWM_PIN_EN) : 0);
}
/*
 * The PWM frequency = HCLK(200Mhz) / (clock division L bit *
 * clock division H bit * (period bit + 1))
 */
static void aspeed_set_pwm_freq(struct aspeed_pwm_data *priv,
				struct pwm_device *pwm, u32 freq)
{
	u32 target_div, cal_freq;
	u32 tmp_div_h, tmp_div_l, diff, min_diff = INT_MAX;
	u32 div_h = BIT(5) - 1, div_l = BIT(8) - 1;
	u8 div_found;
	u32 index = pwm->hwpwm;
	struct aspeed_pwm *channel = pwm_get_chip_data(pwm);

	cal_freq = priv->clk_freq / (DEFAULT_PWM_PERIOD + 1);
	target_div = DIV_ROUND_UP(cal_freq, freq);
	div_found = 0;
	/* calculate for target frequence */
	for (tmp_div_h = 0; tmp_div_h < 0x10; tmp_div_h++) {
		tmp_div_l = target_div / BIT(tmp_div_h) - 1;

		if (tmp_div_l < 0 || tmp_div_l > 255)
			continue;

		diff = freq - cal_freq / (BIT(tmp_div_h) * (tmp_div_l + 1));
		if (abs(diff) < abs(min_diff)) {
			min_diff = diff;
			div_l = tmp_div_l;
			div_h = tmp_div_h;
			div_found = 1;
			if (diff == 0)
				break;
		}
	}
	if (div_found == 0) {
		pr_debug("target freq: %d too slow set minimal frequency\n",
			 freq);
	}
	channel->freq = cal_freq / (BIT(div_h) * (div_l + 1));
	pr_debug("div h %x, l : %x pwm out clk %d\n", div_h, div_l,
		 channel->freq);
	pr_debug("hclk %ld, target pwm freq %d, real pwm freq %d\n",
		 priv->clk_freq, freq, channel->freq);

	regmap_update_bits(priv->regmap, ASPEED_PWM_CTRL_CH(index),
			   (PWM_CLK_DIV_H_MASK | PWM_CLK_DIV_L_MASK),
			   (div_h << PWM_CLK_DIV_H_SHIFT) |
				   (div_l << PWM_CLK_DIV_L_SHIFT));
}

static void aspeed_set_pwm_duty(struct aspeed_pwm_data *priv,
				struct pwm_device *pwm, u32 duty_pt)
{
	u32 index = pwm->hwpwm;

	if (duty_pt == 0) {
		aspeed_set_pwm_channel_enable(priv->regmap, index, false);
	} else {
		regmap_update_bits(priv->regmap,
				   ASPEED_PWM_DUTY_CYCLE_CH(index),
				   PWM_FALLING_POINT_MASK,
				   duty_pt << PWM_FALLING_POINT_SHIFT);
		aspeed_set_pwm_channel_enable(priv->regmap, index, true);
	}
}

static void aspeed_set_pwm_polarity(struct aspeed_pwm_data *priv,
				    struct pwm_device *pwm, u8 polarity)
{
	u32 index = pwm->hwpwm;

	regmap_update_bits(priv->regmap, ASPEED_PWM_CTRL_CH(index), PWM_INVERSE,
			   (polarity) ? PWM_INVERSE : 0);
}

static int aspeed_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct device *dev = chip->dev;
	struct aspeed_pwm_data *priv = dev_get_drvdata(dev);
	struct aspeed_pwm *channel;
	u32 index = pwm->hwpwm;
	/*
	 * Fixed the period to the max value and rising point to 0
	 * for high resolution and simplified frequency calculation.
	 */
	regmap_update_bits(priv->regmap, ASPEED_PWM_DUTY_CYCLE_CH(index),
			   PWM_PERIOD_MASK,
			   DEFAULT_PWM_PERIOD << PWM_PERIOD_SHIFT);

	regmap_update_bits(priv->regmap, ASPEED_PWM_DUTY_CYCLE_CH(index),
			   PWM_RISING_POINT_MASK, 0);

	channel = devm_kzalloc(dev, sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	pwm_set_chip_data(pwm, channel);

	return 0;
}

static void aspeed_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct device *dev = chip->dev;
	struct aspeed_pwm *channel = pwm_get_chip_data(pwm);

	devm_kfree(dev, channel);
}

static int aspeed_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct device *dev = chip->dev;
	struct aspeed_pwm_data *priv = dev_get_drvdata(dev);
	struct pwm_state *cur_state = &pwm->state;
	u32 freq = DIV_ROUND_UP_ULL(1000000000, state->period);
	u32 duty_pt = DIV_ROUND_UP_ULL(
		state->duty_cycle * (DEFAULT_PWM_PERIOD + 1), state->period);
	dev_dbg(dev, "freq: %d, duty_pt: %d", freq, duty_pt);
	if (state->enabled) {
		aspeed_set_pwm_freq(priv, pwm, freq);
		aspeed_set_pwm_duty(priv, pwm, duty_pt);
		aspeed_set_pwm_polarity(priv, pwm, state->polarity);
	} else {
		aspeed_set_pwm_duty(priv, pwm, 0);
	}
	cur_state->period = state->period;
	cur_state->duty_cycle = state->duty_cycle;
	cur_state->polarity = state->polarity;
	cur_state->enabled = state->enabled;

	return 0;
}
static const struct pwm_ops aspeed_pwm_ops = {
	.request = aspeed_pwm_request,
	.free = aspeed_pwm_free,
	.apply = aspeed_pwm_apply,
	.owner = THIS_MODULE,
};

static int aspeed_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk *clk;
	int ret;
	struct aspeed_pwm_data *priv;
	struct device_node *np;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	np = pdev->dev.parent->of_node;
	if (!of_device_is_compatible(np, "aspeed,ast2600-pwm-tach")) {
		dev_err(dev, "unsupported pwm device binding\n");
		return -ENODEV;
	}

	priv->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "Couldn't get regmap\n");
		return -ENODEV;
	}

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk))
		return -ENODEV;
	priv->clk_freq = clk_get_rate(clk);

	priv->reset = reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(priv->reset)) {
		dev_err(&pdev->dev, "can't get aspeed_pwm_tacho reset\n");
		return PTR_ERR(priv->reset);
	}
	reset_control_deassert(priv->reset);

	priv->chip.dev = dev;
	priv->chip.ops = &aspeed_pwm_ops;
	priv->chip.base = -1;
	priv->chip.npwm = ASPEED_NR_PWMS;
	priv->chip.of_xlate = of_pwm_xlate_with_flags;
	priv->chip.of_pwm_n_cells = 3;

	ret = pwmchip_add(&priv->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		return ret;
	}
	dev_set_drvdata(dev, priv);
	return ret;
}

static const struct of_device_id of_pwm_match_table[] = {
	{
		.compatible = "aspeed,ast2600-pwm",
	},
	{},
};
MODULE_DEVICE_TABLE(of, of_pwm_match_table);

static struct platform_driver aspeed_pwm_driver = {
	.probe		= aspeed_pwm_probe,
	.driver		= {
		.name	= "aspeed_pwm",
		.of_match_table = of_pwm_match_table,
	},
};

module_platform_driver(aspeed_pwm_driver);

MODULE_AUTHOR("Billy Tsai <billy_tsai@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED PWM device driver");
MODULE_LICENSE("GPL v2");
