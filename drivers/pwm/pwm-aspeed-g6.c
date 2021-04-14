// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 ASPEED Technology Inc.
 *
 * PWM controller driver for Aspeed ast26xx SoCs.
 * This drivers doesn't rollback to previous version of aspeed SoCs.
 *
 * Hardware Features:
 * 1. Support up to 16 channels
 * 2. Support PWM frequency range from 24Hz to 780KHz
 * 3. Duty cycle from 0 to 100% with 1/256 resolution incremental
 * 4. Support wdt reset tolerance (Driver not ready)
 *
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
#include <linux/bitfield.h>
#include <linux/slab.h>
#include <linux/pwm.h>
/* The channel number of Aspeed pwm controller */
#define PWM_ASPEED_NR_PWMS 16

/* PWM Control Register */
#define PWM_ASPEED_CTRL_CH(ch) (((ch * 0x10) + 0x00))
#define PWM_LOAD_SEL_RISING_AS_WDT BIT(19)
#define PWM_DUTY_LOAD_AS_WDT_ENABLE BIT(18)
#define PWM_DUTY_SYNC_DISABLE BIT(17)
#define PWM_CLK_ENABLE BIT(16)
#define PWM_LEVEL_OUTPUT BIT(15)
#define PWM_INVERSE BIT(14)
#define PWM_OPEN_DRAIN_ENABLE BIT(13)
#define PWM_PIN_ENABLE BIT(12)
#define PWM_CLK_DIV_H GENMASK(11, 8)
#define PWM_CLK_DIV_L GENMASK(7, 0)

/* PWM Duty Cycle Register */
#define PWM_ASPEED_DUTY_CYCLE_CH(ch) (((ch * 0x10) + 0x04))
#define PWM_PERIOD GENMASK(31, 24)
#define PWM_POINT_AS_WDT GENMASK(23, 16)
#define PWM_FALLING_POINT GENMASK(15, 8)
#define PWM_RISING_POINT GENMASK(7, 0)

/* PWM fixed value */
#define PWM_FIXED_PERIOD 0xff

struct aspeed_pwm_data {
	struct pwm_chip chip;
	struct clk *clk;
	struct regmap *regmap;
	struct reset_control *reset;
};

static void aspeed_set_pwm_channel_enable(struct regmap *regmap, u8 pwm_channel,
					  bool enable)
{
	regmap_update_bits(regmap, PWM_ASPEED_CTRL_CH(pwm_channel),
			   (PWM_CLK_ENABLE | PWM_PIN_ENABLE),
			   enable ? (PWM_CLK_ENABLE | PWM_PIN_ENABLE) : 0);
}
/*
 * The PWM frequency = HCLK(200Mhz) / (clock division L bit *
 * clock division H bit * (period bit + 1))
 */
static void aspeed_set_pwm_freq(struct aspeed_pwm_data *priv,
				struct pwm_device *pwm, u32 freq)
{
	u32 target_div, freq_a_fix_div, out_freq;
	u32 tmp_div_h, tmp_div_l, diff, min_diff = INT_MAX;
	u32 div_h = BIT(5) - 1, div_l = BIT(8) - 1;
	u8 div_found;
	u32 index = pwm->hwpwm;
	/* Frequency after fixed divide */
	freq_a_fix_div = clk_get_rate(priv->clk) / (PWM_FIXED_PERIOD + 1);
	/*
	 * Use round up to avoid 0 case.
	 * After that the only scenario which can't find divide pair is too slow
	 */
	target_div = DIV_ROUND_UP(freq_a_fix_div, freq);
	div_found = 0;
	/* calculate for target frequency */
	for (tmp_div_h = 0; tmp_div_h < 0x10; tmp_div_h++) {
		tmp_div_l = target_div / BIT(tmp_div_h) - 1;

		if (tmp_div_l < 0 || tmp_div_l > 255)
			continue;

		diff = freq - ((freq_a_fix_div >> tmp_div_h) / (tmp_div_l + 1));
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
	out_freq = freq_a_fix_div / (BIT(div_h) * (div_l + 1));
	pr_debug("div h %x, l : %x\n", div_h, div_l);
	pr_debug("hclk %ld, target pwm freq %d, real pwm freq %d\n",
		 clk_get_rate(priv->clk), freq, out_freq);

	regmap_update_bits(priv->regmap, PWM_ASPEED_CTRL_CH(index),
			   (PWM_CLK_DIV_H | PWM_CLK_DIV_L),
			   FIELD_PREP(PWM_CLK_DIV_H, div_h) |
				   FIELD_PREP(PWM_CLK_DIV_L, div_l));
}

static void aspeed_set_pwm_duty(struct aspeed_pwm_data *priv,
				struct pwm_device *pwm, u32 duty_pt)
{
	u32 index = pwm->hwpwm;

	if (duty_pt == 0) {
		aspeed_set_pwm_channel_enable(priv->regmap, index, false);
	} else {
		regmap_update_bits(priv->regmap,
				   PWM_ASPEED_DUTY_CYCLE_CH(index),
				   PWM_FALLING_POINT,
				   FIELD_PREP(PWM_FALLING_POINT, duty_pt));
		aspeed_set_pwm_channel_enable(priv->regmap, index, true);
	}
}

static void aspeed_set_pwm_polarity(struct aspeed_pwm_data *priv,
				    struct pwm_device *pwm, u8 polarity)
{
	u32 index = pwm->hwpwm;

	regmap_update_bits(priv->regmap, PWM_ASPEED_CTRL_CH(index), PWM_INVERSE,
			   (polarity) ? PWM_INVERSE : 0);
}

static int aspeed_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct device *dev = chip->dev;
	struct aspeed_pwm_data *priv = dev_get_drvdata(dev);
	struct pwm_state *channel;
	u32 index = pwm->hwpwm;
	/*
	 * Fixed the period to the max value and rising point to 0
	 * for high resolution and simplified frequency calculation.
	 */
	regmap_update_bits(priv->regmap, PWM_ASPEED_DUTY_CYCLE_CH(index),
			   PWM_PERIOD,
			   FIELD_PREP(PWM_PERIOD, PWM_FIXED_PERIOD));

	regmap_update_bits(priv->regmap, PWM_ASPEED_DUTY_CYCLE_CH(index),
			   PWM_RISING_POINT, 0);

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	return pwm_set_chip_data(pwm, channel);
}

static void aspeed_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_state *channel = pwm_get_chip_data(pwm);

	kfree(channel);
}

static inline struct aspeed_pwm_data *
aspeed_pwm_chip_to_data(struct pwm_chip *c)
{
	return container_of(c, struct aspeed_pwm_data, chip);
}

static int aspeed_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct device *dev = chip->dev;
	struct aspeed_pwm_data *priv = aspeed_pwm_chip_to_data(chip);
	struct pwm_state *channel = pwm_get_chip_data(pwm);
	/* compute the ns to Hz */
	u32 freq = DIV_ROUND_UP_ULL(1000000000, state->period);
	u32 duty_pt = DIV_ROUND_UP_ULL(
		state->duty_cycle * (PWM_FIXED_PERIOD + 1), state->period);
	dev_dbg(dev, "freq: %d, duty_pt: %d", freq, duty_pt);
	if (state->enabled) {
		aspeed_set_pwm_freq(priv, pwm, freq);
		aspeed_set_pwm_duty(priv, pwm, duty_pt);
		aspeed_set_pwm_polarity(priv, pwm, state->polarity);
	} else {
		aspeed_set_pwm_duty(priv, pwm, 0);
	}
	channel->period = state->period;
	channel->duty_cycle = state->duty_cycle;
	channel->polarity = state->polarity;
	channel->enabled = state->enabled;

	return 0;
}

static void aspeed_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				 struct pwm_state *state)
{
	struct pwm_state *channel = pwm_get_chip_data(pwm);

	state->period = channel->period;
	state->duty_cycle = channel->duty_cycle;
	state->polarity = channel->polarity;
	state->enabled = channel->enabled;
}

static const struct pwm_ops aspeed_pwm_ops = {
	.request = aspeed_pwm_request,
	.free = aspeed_pwm_free,
	.apply = aspeed_pwm_apply,
	.get_state = aspeed_pwm_get_state,
	.owner = THIS_MODULE,
};

static int aspeed_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
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

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return -ENODEV;

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "couldn't enable clock\n");
		return ret;
	}

	priv->reset = reset_control_get_shared(dev, NULL);
	if (IS_ERR(priv->reset)) {
		dev_err(dev, "can't get aspeed_pwm_tacho reset: %pe\n",
			ERR_PTR((long)priv->reset));
		return PTR_ERR(priv->reset);
	}

	ret = reset_control_deassert(priv->reset);
	if (ret) {
		dev_err(&pdev->dev, "cannot deassert reset control: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	priv->chip.dev = dev;
	priv->chip.ops = &aspeed_pwm_ops;
	priv->chip.npwm = PWM_ASPEED_NR_PWMS;
	priv->chip.of_xlate = of_pwm_xlate_with_flags;
	priv->chip.of_pwm_n_cells = 3;

	ret = pwmchip_add(&priv->chip);
	if (ret < 0) {
		dev_err(dev, "failed to add PWM chip: %pe\n", ERR_PTR(ret));
		return ret;
	}
	dev_set_drvdata(dev, priv);
	return ret;
}

static int aspeed_pwm_remove(struct platform_device *dev)
{
	struct aspeed_pwm_data *priv = platform_get_drvdata(dev);

	reset_control_assert(priv->reset);
	clk_disable_unprepare(priv->clk);

	return pwmchip_remove(&priv->chip);
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
	.remove		= aspeed_pwm_remove,
	.driver		= {
		.name	= "aspeed_pwm",
		.of_match_table = of_pwm_match_table,
	},
};

module_platform_driver(aspeed_pwm_driver);

MODULE_AUTHOR("Billy Tsai <billy_tsai@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED PWM device driver");
MODULE_LICENSE("GPL v2");
