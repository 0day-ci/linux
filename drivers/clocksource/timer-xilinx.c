// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Sean Anderson <sean.anderson@seco.com>
 *
 * Limitations:
 * - When in cascade mode we cannot read the full 64-bit counter in one go
 */

#include <clocksource/timer-xilinx.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/log2.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/sched_clock.h>
#if IS_ENABLED(CONFIG_MICROBLAZE)
#include <asm/cpuinfo.h>
#endif

struct xilinx_clocksource_device {
	struct clocksource cs;
	struct xilinx_timer_priv priv;
};

static inline struct xilinx_timer_priv
*xilinx_clocksource_to_priv(struct clocksource *cs)
{
	return &container_of(cs, struct xilinx_clocksource_device, cs)->priv;
}

static u64 xilinx_clocksource_read(struct clocksource *cs)
{
	struct xilinx_timer_priv *priv = xilinx_clocksource_to_priv(cs);
	unsigned int ret;

	regmap_read(priv->map, TCR0, &ret);
	return ret;
}

static struct xilinx_clocksource_device xilinx_clocksource = {
	.cs = {
		.name = "xilinx_clocksource",
		.rating = 300,
		.read = xilinx_clocksource_read,
		.flags = CLOCK_SOURCE_IS_CONTINUOUS,
		.owner = THIS_MODULE,
	},
};

static u64 xilinx_sched_read(void)
{
	return xilinx_clocksource_read(&xilinx_clocksource.cs);
}

static int __init xilinx_clocksource_init(struct device_node *np)
{
	int ret;
	struct xilinx_timer_priv *priv = &xilinx_clocksource.priv;
	static const struct reg_sequence init[] = {
		REG_SEQ0(TLR0, 0),
		REG_SEQ0(TCSR0, TCSR_LOAD | TCSR_TINT),
		REG_SEQ0(TCSR0, TCSR_ARHT | TCSR_ENT),
	};

	ret = regmap_multi_reg_write(priv->map, init, ARRAY_SIZE(init));
	if (ret)
		return ret;

	xilinx_clocksource.cs.mask = priv->max;
	ret = clocksource_register_hz(&xilinx_clocksource.cs,
				      clk_get_rate(priv->clk));
	if (!ret)
		sched_clock_register(xilinx_sched_read,
				     ilog2((u64)priv->max + 1),
				     clk_get_rate(priv->clk));
	else
		pr_err("%pOF: err %d: could not register clocksource\n",
		       np, ret);
	return ret;
}

struct xilinx_clockevent_device {
	struct clock_event_device ce;
	struct xilinx_timer_priv priv;
};

static inline struct xilinx_timer_priv
*xilinx_clockevent_to_priv(struct clock_event_device *ce)
{
	return &container_of(ce, struct xilinx_clockevent_device, ce)->priv;
}

static irqreturn_t xilinx_timer_handler(int irq, void *p)
{
	struct xilinx_clockevent_device *dev = p;

	if (regmap_test_bits(dev->priv.map, TCSR0, TCSR_TINT) <= 0)
		return IRQ_NONE;

	regmap_clear_bits(dev->priv.map, TCSR0, TCSR_TINT);
	dev->ce.event_handler(&dev->ce);
	return IRQ_HANDLED;
}

static int xilinx_clockevent_next_event(unsigned long evt,
					struct clock_event_device *ce)
{
	struct xilinx_timer_priv *priv = xilinx_clockevent_to_priv(ce);
	struct reg_sequence next[] = {
		REG_SEQ0(TLR0, evt - 2),
		REG_SEQ0(TCSR0, TCSR_LOAD),
		REG_SEQ0(TCSR0, TCSR_ENIT | TCSR_ENT),
	};

	return regmap_multi_reg_write(priv->map, next, ARRAY_SIZE(next));
}

static int xilinx_clockevent_state_periodic(struct clock_event_device *ce)
{
	struct xilinx_timer_priv *priv = xilinx_clockevent_to_priv(ce);
	struct reg_sequence periodic[] = {
		REG_SEQ0(TLR0, 0),
		REG_SEQ0(TCSR0, TCSR_LOAD),
		REG_SEQ0(TCSR0, TCSR_ARHT | TCSR_ENIT | TCSR_ENT),
	};
	unsigned long cycles = clk_get_rate(priv->clk) / HZ;

	if (cycles < 2 || cycles - 2 > priv->max)
		return -ERANGE;
	periodic[0].def = xilinx_timer_tlr_cycles(priv, 0, cycles);

	return regmap_multi_reg_write(priv->map, periodic, ARRAY_SIZE(periodic));
}

static int xilinx_clockevent_shutdown(struct clock_event_device *ce)
{
	struct xilinx_timer_priv *priv = xilinx_clockevent_to_priv(ce);

	return regmap_write(priv->map, TCSR0, 0);
}

static const struct clock_event_device xilinx_clockevent_base __initconst = {
	.rating = 300,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event = xilinx_clockevent_next_event,
	.set_state_periodic = xilinx_clockevent_state_periodic,
	.set_state_shutdown = xilinx_clockevent_shutdown,
	.cpumask = cpu_possible_mask,
	.owner = THIS_MODULE,
};

static int __init xilinx_clockevent_init(struct device_node *np,
					 struct xilinx_clockevent_device *dev)
{
	int irq, ret;

	irq = ret = of_irq_get(np, 0);
	if (ret < 0) {
		pr_err("%pOF: err %d: could not get irq\n", np, ret);
		return ret;
	}

	ret = request_irq(irq, xilinx_timer_handler, IRQF_TIMER,
			  of_node_full_name(np), dev);
	if (ret) {
		pr_err("%pOF: err %d: could not request irq\n", np, ret);
		return ret;
	}

	memcpy(&dev->ce, &xilinx_clockevent_base, sizeof(dev->ce));
	dev->ce.name = of_node_full_name(np);
	clockevents_config_and_register(&dev->ce, clk_get_rate(dev->priv.clk), 2,
					min_t(u64, (u64)dev->priv.max + 2,
					      ULONG_MAX));
	return 0;
}

static const struct regmap_config xilinx_timer_regmap_config __initconst = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = TCR0,
};

static bool clocksource_uninitialized __initdata = true;

static int __init xilinx_timer_init(struct device_node *np)
{
	bool artificial_clock = false;
	int ret;
	struct xilinx_timer_priv *priv;
	struct xilinx_clockevent_device *dev;
	u32 one_timer;
	void __iomem *regs;

	if (of_property_read_bool(np, "#pwm-cells"))
		return 0;

	regs = of_iomap(np, 0);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		pr_err("%pOF: err %d: failed to map regs\n", np, ret);
		return ret;
	}

	if (clocksource_uninitialized) {
		priv = &xilinx_clocksource.priv;
	} else {
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev) {
			ret = -ENOMEM;
			goto err_regs;
		}
		priv = &dev->priv;
	}

	priv->map = regmap_init_mmio(NULL, regs, &xilinx_timer_regmap_config);
	if (IS_ERR(priv->map)) {
		ret = PTR_ERR(priv->map);
		goto err_priv;
	}

	ret = xilinx_timer_common_init(np, priv, &one_timer);
	if (ret)
		goto err_regmap;

	priv->clk = of_clk_get_by_name(np, "s_axi_aclk");
	if (IS_ERR(priv->clk)) {
		u32 freq;

		ret = PTR_ERR(priv->clk);
		if (ret == -EPROBE_DEFER)
			goto err_regs;

		pr_warn("%pOF: missing s_axi_aclk, falling back to clock-frequency\n",
			np);
		ret = of_property_read_u32(np, "clock-frequency", &freq);
		if (ret) {
#if IS_ENABLED(CONFIG_MICROBLAZE)
			pr_warn("%pOF: missing clock-frequency, falling back to /cpus/timebase-frequency\n",
				np);
			freq = cpuinfo.cpu_clock_freq;
#else
			goto err_regmap;
#endif
		}

		priv->clk = clk_register_fixed_rate(NULL, of_node_full_name(np),
						    NULL, 0, freq);
		if (IS_ERR(priv->clk)) {
			ret = PTR_ERR(priv->clk);
			goto err_regmap;
		}
		artificial_clock = true;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		pr_err("%pOF: err %d: clock enable failed\n", np, ret);
		goto err_clk_init;
	}
	clk_rate_exclusive_get(priv->clk);

	if (clocksource_uninitialized) {
		ret = xilinx_clocksource_init(np);
		if (ret)
			goto err_clk_enable;
		clocksource_uninitialized = false;
	} else {
		ret = xilinx_clockevent_init(np, dev);
		if (ret)
			goto err_clk_enable;
	}
	of_node_set_flag(np, OF_POPULATED);

	if (!one_timer) {
		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
		if (!dev)
			return -ENOMEM;

		/*
		 * We don't support removal, so don't bother enabling
		 * the clock twice.
		 */
		memcpy(&dev->priv, priv, sizeof(dev->priv));
		dev->priv.map = regmap_init_mmio(NULL, regs + TCSR1,
						 &xilinx_timer_regmap_config);
		if (!IS_ERR(dev->priv.map)) {
			ret = xilinx_clockevent_init(np, dev);
			if (!ret)
				return 0;
			regmap_exit(dev->priv.map);
		} else {
			ret = PTR_ERR(dev->priv.map);
		}
		kfree(dev);
	}
	return ret;

err_clk_enable:
	clk_rate_exclusive_put(priv->clk);
	clk_disable_unprepare(priv->clk);
err_clk_init:
	if (artificial_clock)
		clk_unregister_fixed_rate(priv->clk);
	else
		clk_put(priv->clk);
err_regmap:
	regmap_exit(priv->map);
err_priv:
	if (!clocksource_uninitialized)
		kfree(dev);
err_regs:
	iounmap(regs);
	return ret;
}

TIMER_OF_DECLARE(xilinx_xps_timer, "xlnx,xps-timer-1.00.a", xilinx_timer_init);
