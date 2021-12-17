// SPDX-License-Identifier: GPL-2.0
/*
 * PWM device driver for SUNPLUS SoCs
 *
 * Author: Hammer Hsieh <hammer.hsieh@sunplus.com>
 */
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define PWM_SUP_CONTROL0	0x000
#define PWM_SUP_CONTROL1	0x004
#define PWM_SUP_FREQ_BASE	0x008
#define PWM_SUP_DUTY_BASE	0x018
#define PWM_SUP_FREQ(ch)	(PWM_SUP_FREQ_BASE + 4 * (ch))
#define PWM_SUP_DUTY(ch)	(PWM_SUP_DUTY_BASE + 4 * (ch))
#define PWM_SUP_FREQ_MAX	GENMASK(15, 0)
#define PWM_SUP_DUTY_MAX	GENMASK(7, 0)

#define PWM_SUP_NUM		4
#define PWM_BYPASS_BIT_SHIFT	8
#define PWM_DD_SEL_BIT_SHIFT	8
#define PWM_SUP_FREQ_SCALER	256

struct sunplus_pwm {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
};

static inline struct sunplus_pwm *to_sunplus_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct sunplus_pwm, chip);
}

static void sunplus_reg_init(void __iomem *base)
{
	u32 i, value;

	/* turn off all pwm channel output */
	value = readl(base + PWM_SUP_CONTROL0);
	value &= ~GENMASK((PWM_SUP_NUM - 1), 0);
	writel(value, base + PWM_SUP_CONTROL0);

	/* init all pwm channel clock source */
	value = readl(base + PWM_SUP_CONTROL1);
	value |= GENMASK((PWM_SUP_NUM - 1), 0);
	writel(value, base + PWM_SUP_CONTROL1);

	/* init all freq and duty setting */
	for (i = 0; i < PWM_SUP_NUM; i++) {
		writel(0, base + PWM_SUP_FREQ(i));
		writel(0, base + PWM_SUP_DUTY(i));
	}
}

static int sunplus_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     const struct pwm_state *state)
{
	struct sunplus_pwm *priv = to_sunplus_pwm(chip);
	u32 period_ns, duty_ns, value;
	u32 dd_freq, duty;
	u64 tmp;

	if (!state->enabled) {
		value = readl(priv->base + PWM_SUP_CONTROL0);
		value &= ~BIT(pwm->hwpwm);
		writel(value, priv->base + PWM_SUP_CONTROL0);
		return 0;
	}

	period_ns = state->period;
	duty_ns = state->duty_cycle;

	/* cal pwm freq and check value under range */
	tmp = clk_get_rate(priv->clk) * (u64)period_ns;
	tmp = DIV_ROUND_CLOSEST_ULL(tmp, NSEC_PER_SEC);
	tmp = DIV_ROUND_CLOSEST_ULL(tmp, PWM_SUP_FREQ_SCALER);
	dd_freq = (u32)tmp;

	if (dd_freq == 0)
		return -EINVAL;

	if (dd_freq > PWM_SUP_FREQ_MAX)
		dd_freq = PWM_SUP_FREQ_MAX;

	writel(dd_freq, priv->base + PWM_SUP_FREQ(pwm->hwpwm));

	/* cal and set pwm duty */
	value = readl(priv->base + PWM_SUP_CONTROL0);
	value |= BIT(pwm->hwpwm);
	if (duty_ns == period_ns) {
		value |= BIT(pwm->hwpwm + PWM_BYPASS_BIT_SHIFT);
		duty = PWM_SUP_DUTY_MAX;
	} else {
		value &= ~BIT(pwm->hwpwm + PWM_BYPASS_BIT_SHIFT);
		tmp = (u64)duty_ns * PWM_SUP_FREQ_SCALER + (period_ns >> 1);
		tmp = DIV_ROUND_CLOSEST_ULL(tmp, (u64)period_ns);
		duty = (u32)tmp;
		duty |= (pwm->hwpwm << PWM_DD_SEL_BIT_SHIFT);
	}
	writel(value, priv->base + PWM_SUP_CONTROL0);
	writel(duty, priv->base + PWM_SUP_DUTY(pwm->hwpwm));

	return 0;
}

static void sunplus_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct sunplus_pwm *priv = to_sunplus_pwm(chip);
	u32 value;

	value = readl(priv->base + PWM_SUP_CONTROL0);

	if (value & BIT(pwm->hwpwm))
		state->enabled = true;
	else
		state->enabled = false;
}

static const struct pwm_ops sunplus_pwm_ops = {
	.apply = sunplus_pwm_apply,
	.get_state = sunplus_pwm_get_state,
	.owner = THIS_MODULE,
};

static int sunplus_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sunplus_pwm *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "get pwm clock failed\n");

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev,
				       (void(*)(void *))clk_disable_unprepare,
				       priv->clk);
	if (ret)
		return ret;

	priv->chip.dev = dev;
	priv->chip.ops = &sunplus_pwm_ops;
	priv->chip.npwm = PWM_SUP_NUM;

	sunplus_reg_init(priv->base);

	platform_set_drvdata(pdev, priv);

	ret = devm_pwmchip_add(dev, &priv->chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot register sunplus PWM\n");

	return 0;
}

static const struct of_device_id sunplus_pwm_of_match[] = {
	{ .compatible = "sunplus,sp7021-pwm", },
	{}
};
MODULE_DEVICE_TABLE(of, sunplus_pwm_of_match);

static struct platform_driver sunplus_pwm_driver = {
	.probe		= sunplus_pwm_probe,
	.driver		= {
		.name	= "sunplus-pwm",
		.of_match_table = sunplus_pwm_of_match,
	},
};
module_platform_driver(sunplus_pwm_driver);

MODULE_DESCRIPTION("Sunplus SoC PWM Driver");
MODULE_AUTHOR("Hammer Hsieh <hammer.hsieh@sunplus.com>");
MODULE_LICENSE("GPL v2");
