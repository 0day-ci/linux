// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Sean Anderson <sean.anderson@seco.com>
 *
 * For Xilinx LogiCORE IP AXI Timer documentation, refer to DS764:
 * https://www.xilinx.com/support/documentation/ip_documentation/axi_timer/v1_03_a/axi_timer_ds764.pdf
 *
 * Hardware limitations:
 * - When in cascade mode we cannot read the full 64-bit counter in one go
 * - When changing both duty cycle and period, we may end up with one cycle
 *   with the old duty cycle and the new period.
 * - Cannot produce 100% duty cycle.
 * - Only produces "normal" output.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/sched_clock.h>
#include <asm/io.h>
#if IS_ENABLED(CONFIG_MICROBLAZE)
#include <asm/cpuinfo.h>
#endif

/* A replacement for dev_err_probe, since we don't always have a device */
#define xilinx_timer_err(np, err, fmt, ...) ({ \
	pr_err("%pOF: error %d: " fmt, (np), (int)(err), ##__VA_ARGS__); \
	err; \
})

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
#define TCSR_PWM_SET (TCSR_GENT | TCSR_ARHT | TCSR_ENT | TCSR_PWMA)
#define TCSR_PWM_CLEAR (TCSR_MDT | TCSR_LOAD)
#define TCSR_PWM_MASK (TCSR_PWM_SET | TCSR_PWM_CLEAR)

/**
 * struct xilinx_timer_priv - Private data for Xilinx AXI timer driver
 * @cs: Clocksource device
 * @ce: Clockevent device
 * @pwm: PWM controller chip
 * @clk: Parent clock
 * @regs: Base address of this device
 * @width: Width of the counters, in bits
 * @XILINX_TIMER_ONE: We have only one timer.
 * @XILINX_TIMER_PWM: Configured as a PWM.
 * @XILINX_TIMER_CLK: We were missing a device tree clock and created our own
 * @flags: Flags for what type of device we are
 */
struct xilinx_timer_priv {
	union {
		struct {
			struct clocksource cs;
			struct clock_event_device ce;
		};
		struct pwm_chip pwm;
	};
	struct clk *clk;
	void __iomem *regs;
	u32 (*read)(const volatile void __iomem *addr);
	void (*write)(u32 value, volatile void __iomem *addr);
	unsigned int width;
	enum {
		XILINX_TIMER_ONE = BIT(0),
		XILINX_TIMER_PWM = BIT(1),
		XILINX_TIMER_CLK = BIT(2),
	} flags;
};

static inline struct xilinx_timer_priv
*xilinx_pwm_chip_to_priv(struct pwm_chip *chip)
{
	return container_of(chip, struct xilinx_timer_priv, pwm);
}

static inline struct xilinx_timer_priv
*xilinx_clocksource_to_priv(struct clocksource *cs)
{
	return container_of(cs, struct xilinx_timer_priv, cs);
}

static inline struct xilinx_timer_priv
*xilinx_clockevent_to_priv(struct clock_event_device *ce)
{
	return container_of(ce, struct xilinx_timer_priv, ce);
}

static u32 xilinx_ioread32be(const volatile void __iomem *addr)
{
	return ioread32be(addr);
}

static void xilinx_iowrite32be(u32 value, volatile void __iomem *addr)
{
	iowrite32be(value, addr);
}

static inline u32 xilinx_timer_read(struct xilinx_timer_priv *priv,
				    int offset)
{
	return priv->read(priv->regs + offset);
}

static inline void xilinx_timer_write(struct xilinx_timer_priv *priv,
				      u32 value, int offset)
{
	priv->write(value, priv->regs + offset);
}

static inline u64 xilinx_timer_max(struct xilinx_timer_priv *priv)
{
	return BIT_ULL(priv->width) - 1;
}

static int xilinx_timer_tlr_cycles(struct xilinx_timer_priv *priv, u32 *tlr,
				   u32 tcsr, u64 cycles)
{
	u64 max_count = xilinx_timer_max(priv);

	if (cycles < 2 || cycles > max_count + 2)
		return -ERANGE;

	if (tcsr & TCSR_UDT)
		*tlr = cycles - 2;
	else
		*tlr = max_count - cycles + 2;

	return 0;
}

static bool xilinx_timer_pwm_enabled(u32 tcsr0, u32 tcsr1)
{
	return ((TCSR_PWM_MASK | TCSR_CASC) & tcsr0) == TCSR_PWM_SET &&
		(TCSR_PWM_MASK & tcsr1) == TCSR_PWM_SET;
}

static int xilinx_timer_tlr_period(struct xilinx_timer_priv *priv, u32 *tlr,
				   u32 tcsr, unsigned int period)
{
	u64 cycles = DIV_ROUND_DOWN_ULL((u64)period * clk_get_rate(priv->clk),
					NSEC_PER_SEC);

	return xilinx_timer_tlr_cycles(priv, tlr, tcsr, cycles);
}

static unsigned int xilinx_timer_get_period(struct xilinx_timer_priv *priv,
					    u32 tlr, u32 tcsr)
{
	u64 cycles;

	if (tcsr & TCSR_UDT)
		cycles = tlr + 2;
	else
		cycles = xilinx_timer_max(priv) - tlr + 2;

	return DIV_ROUND_UP_ULL(cycles * NSEC_PER_SEC,
				clk_get_rate(priv->clk));
}

static int xilinx_pwm_apply(struct pwm_chip *chip, struct pwm_device *unused,
			    const struct pwm_state *state)
{
	int ret;
	struct xilinx_timer_priv *priv = xilinx_pwm_chip_to_priv(chip);
	u32 tlr0, tlr1;
	u32 tcsr0 = xilinx_timer_read(priv, TCSR0);
	u32 tcsr1 = xilinx_timer_read(priv, TCSR1);
	bool enabled = xilinx_timer_pwm_enabled(tcsr0, tcsr1);

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	ret = xilinx_timer_tlr_period(priv, &tlr0, tcsr0, state->period);
	if (ret)
		return ret;

	ret = xilinx_timer_tlr_period(priv, &tlr1, tcsr1, state->duty_cycle);
	if (ret)
		return ret;

	xilinx_timer_write(priv, tlr0, TLR0);
	xilinx_timer_write(priv, tlr1, TLR1);

	if (state->enabled) {
		/* Only touch the TCSRs if we aren't already running */
		if (!enabled) {
			/* Load TLR into TCR */
			xilinx_timer_write(priv, tcsr0 | TCSR_LOAD, TCSR0);
			xilinx_timer_write(priv, tcsr1 | TCSR_LOAD, TCSR1);
			/* Enable timers all at once with ENALL */
			tcsr0 = (TCSR_PWM_SET & ~TCSR_ENT) | (tcsr0 & TCSR_UDT);
			tcsr1 = TCSR_PWM_SET | TCSR_ENALL | (tcsr1 & TCSR_UDT);
			xilinx_timer_write(priv, tcsr0, TCSR0);
			xilinx_timer_write(priv, tcsr1, TCSR1);
		}
	} else {
		xilinx_timer_write(priv, 0, TCSR0);
		xilinx_timer_write(priv, 0, TCSR1);
	}

	return 0;
}

static void xilinx_pwm_get_state(struct pwm_chip *chip,
				 struct pwm_device *unused,
				 struct pwm_state *state)
{
	struct xilinx_timer_priv *priv = xilinx_pwm_chip_to_priv(chip);
	u32 tlr0 = xilinx_timer_read(priv, TLR0);
	u32 tlr1 = xilinx_timer_read(priv, TLR1);
	u32 tcsr0 = xilinx_timer_read(priv, TCSR0);
	u32 tcsr1 = xilinx_timer_read(priv, TCSR1);

	state->period = xilinx_timer_get_period(priv, tlr0, tcsr0);
	state->duty_cycle = xilinx_timer_get_period(priv, tlr1, tcsr1);
	state->enabled = xilinx_timer_pwm_enabled(tcsr0, tcsr1);
	state->polarity = PWM_POLARITY_NORMAL;
}

static const struct pwm_ops xilinx_pwm_ops = {
	.apply = xilinx_pwm_apply,
	.get_state = xilinx_pwm_get_state,
	.owner = THIS_MODULE,
};

static int xilinx_pwm_init(struct device *dev,
			   struct xilinx_timer_priv *priv)
{
	int ret;

	if (!dev)
		return -EPROBE_DEFER;

	priv->pwm.dev = dev;
	priv->pwm.ops = &xilinx_pwm_ops;
	priv->pwm.npwm = 1;
	ret = pwmchip_add(&priv->pwm);
	if (ret)
		xilinx_timer_err(dev->of_node, ret,
				 "could not register pwm chip\n");
	return ret;
}

static irqreturn_t xilinx_timer_handler(int irq, void *dev)
{
	struct xilinx_timer_priv *priv = dev;
	u32 tcsr1 = xilinx_timer_read(priv, TCSR1);

	/* Acknowledge interrupt */
	xilinx_timer_write(priv, tcsr1 | TCSR_TINT, TCSR1);
	priv->ce.event_handler(&priv->ce);
	return IRQ_HANDLED;
}

static int xilinx_clockevent_next_event(unsigned long evt,
					struct clock_event_device *ce)
{
	struct xilinx_timer_priv *priv = xilinx_clockevent_to_priv(ce);

	xilinx_timer_write(priv, evt, TLR1);
	xilinx_timer_write(priv, TCSR_LOAD, TCSR1);
	xilinx_timer_write(priv, TCSR_ENIT | TCSR_ENT, TCSR1);
	return 0;
}

static int xilinx_clockevent_state_periodic(struct clock_event_device *ce)
{
	int ret;
	u32 tlr1;
	struct xilinx_timer_priv *priv = xilinx_clockevent_to_priv(ce);

	ret = xilinx_timer_tlr_cycles(priv, &tlr1, 0,
				      clk_get_rate(priv->clk) / HZ);
	if (ret)
		return ret;

	xilinx_timer_write(priv, tlr1, TLR1);
	xilinx_timer_write(priv, TCSR_LOAD, TCSR1);
	xilinx_timer_write(priv, TCSR_ARHT | TCSR_ENIT | TCSR_ENT, TCSR1);
	return 0;
}

static int xilinx_clockevent_shutdown(struct clock_event_device *ce)
{
	xilinx_timer_write(xilinx_clockevent_to_priv(ce), 0, TCSR1);
	return 0;
}

static const struct clock_event_device xilinx_clockevent_base = {
	.name = "xilinx_clockevent",
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event = xilinx_clockevent_next_event,
	.set_state_periodic = xilinx_clockevent_state_periodic,
	.set_state_shutdown = xilinx_clockevent_shutdown,
	.rating = 300,
	.cpumask = cpu_possible_mask,
	.owner = THIS_MODULE,
};

static int xilinx_clockevent_init(struct device_node *np,
				  struct xilinx_timer_priv *priv)
{
	int ret = of_irq_get(np, 0);

	if (ret < 0)
		return xilinx_timer_err(np, ret, "could not get irq\n");

	ret = request_irq(ret, xilinx_timer_handler, IRQF_TIMER,
			  np->full_name, priv);
	if (ret)
		return xilinx_timer_err(np, ret, "could not request irq\n");

	memcpy(&priv->ce, &xilinx_clockevent_base, sizeof(priv->ce));
	clockevents_config_and_register(&priv->ce,
					clk_get_rate(priv->clk), 2,
					min_t(u64,
					      xilinx_timer_max(priv) + 2,
					      ULONG_MAX));
	return 0;
}

static u64 xilinx_clocksource_read(struct clocksource *cs)
{
	return xilinx_timer_read(xilinx_clocksource_to_priv(cs), TCR0);
}

static const struct clocksource xilinx_clocksource_base = {
	.read = xilinx_clocksource_read,
	.name = "xilinx_clocksource",
	.rating = 300,
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.owner = THIS_MODULE,
};

static int xilinx_clocksource_init(struct xilinx_timer_priv *priv)
{
	xilinx_timer_write(priv, 0, TLR0);
	/* Load TLR and clear any interrupts */
	xilinx_timer_write(priv, TCSR_LOAD | TCSR_TINT, TCSR0);
	/* Start the timer counting up with auto-reload */
	xilinx_timer_write(priv, TCSR_ARHT | TCSR_ENT, TCSR0);

	memcpy(&priv->cs, &xilinx_clocksource_base, sizeof(priv->cs));
	priv->cs.mask = xilinx_timer_max(priv);
	return clocksource_register_hz(&priv->cs, clk_get_rate(priv->clk));
}

static struct clk *xilinx_timer_clock_init(struct device_node *np,
					   struct xilinx_timer_priv *priv)
{
	int ret;
	u32 freq;
	struct clk_hw *hw;
	struct clk *clk = of_clk_get_by_name(np, "s_axi_aclk");

	if (!IS_ERR(clk) || PTR_ERR(clk) == -EPROBE_DEFER)
		return clk;

	pr_warn("%pOF: missing s_axi_aclk, falling back to clock-frequency\n",
		np);
	ret = of_property_read_u32(np, "clock-frequency", &freq);
	if (ret) {
#if IS_ENABLED(CONFIG_MICROBLAZE)
		pr_warn("%pOF: missing clock-frequency, falling back to /cpus/timebase-frequency\n",
			np);
		freq = cpuinfo.cpu_clock_freq;
#else
		return ERR_PTR(ret);
#endif
	}

	priv->flags |= XILINX_TIMER_CLK;
	hw = __clk_hw_register_fixed_rate(NULL, np, "s_axi_aclk", NULL, NULL,
					  NULL, 0, freq, 0, 0);
	if (IS_ERR(hw))
		return ERR_CAST(hw);
	return hw->clk;
}

static struct xilinx_timer_priv *xilinx_timer_init(struct device *dev,
						   struct device_node *np)
{
	bool pwm;
	int i, ret;
	struct xilinx_timer_priv *priv;
	u32 one_timer, tcsr0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->regs = of_iomap(np, 0);
	if (!priv->regs) {
		ret = -ENXIO;
		goto err_priv;
	} else if (IS_ERR(priv->regs)) {
		ret = PTR_ERR(priv->regs);
		goto err_priv;
	}

	priv->read = ioread32;
	priv->write = iowrite32;
	/*
	 * We aren't using the interrupts yet, so use ENIT to detect endianness
	 */
	tcsr0 = xilinx_timer_read(priv, TCSR0);
	if (swab32(tcsr0) & TCSR_ENIT) {
		ret = xilinx_timer_err(np, -EOPNOTSUPP,
				       "cannot determine endianness\n");
		goto err_priv;
	}

	xilinx_timer_write(priv, tcsr0 | TCSR_ENIT, TCSR0);
	if (!(xilinx_timer_read(priv, TCSR0) & TCSR_ENIT)) {
		priv->read = xilinx_ioread32be;
		priv->write = xilinx_iowrite32be;
	}

	/*
	 * For backwards compatibility, allow xlnx,one-timer-only = <bool>;
	 * However, the preferred way is to use the xlnx,single-timer flag.
	 */
	one_timer = of_property_read_bool(np, "xlnx,single-timer");
	if (!one_timer) {
		ret = of_property_read_u32(np, "xlnx,one-timer-only", &one_timer);
		if (ret) {
			ret = xilinx_timer_err(np, ret, "xlnx,one-timer-only");
			goto err_priv;
		}
	}

	pwm = of_property_read_bool(np, "xlnx,pwm");
	if (one_timer && pwm) {
		ret = xilinx_timer_err(np, -EINVAL,
				       "pwm mode not possible with one timer\n");
		goto err_priv;
	}

	priv->flags = FIELD_PREP(XILINX_TIMER_ONE, one_timer) |
		      FIELD_PREP(XILINX_TIMER_PWM, pwm);

	for (i = 0; pwm && i < 2; i++) {
		char int_fmt[] = "xlnx,gen%u-assert";
		char bool_fmt[] = "xlnx,gen%u-active-low";
		char buf[max(sizeof(int_fmt), sizeof(bool_fmt))];
		u32 gen;

		/*
		 * Allow xlnx,gen?-assert = <bool>; for backwards
		 * compatibility. However, the preferred way is to use the
		 * xlnx,gen?-active-low flag.
		 */
		snprintf(buf, sizeof(buf), bool_fmt, i);
		gen = !of_property_read_bool(np, buf);
		if (gen) {
			snprintf(buf, sizeof(buf), int_fmt, i);
			ret = of_property_read_u32(np, buf, &gen);
			if (ret && ret != -EINVAL) {
				xilinx_timer_err(np, ret, "%s\n", buf);
				goto err_priv;
			}
		}

		if (!gen) {
			ret = xilinx_timer_err(np, -EINVAL,
					       "generateout%u must be active high\n",
					       i);
			goto err_priv;
		}
	}

	ret = of_property_read_u32(np, "xlnx,count-width", &priv->width);
	if (ret) {
		xilinx_timer_err(np, ret, "xlnx,count-width\n");
		goto err_priv;
	} else if (priv->width < 8 || priv->width > 32) {
		ret = xilinx_timer_err(np, -EINVAL, "invalid counter width\n");
		goto err_priv;
	}

	priv->clk = xilinx_timer_clock_init(np, priv);
	if (IS_ERR(priv->clk)) {
		ret = xilinx_timer_err(np, PTR_ERR(priv->clk), "clock\n");
		goto err_priv;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		xilinx_timer_err(np, ret, "clock enable failed\n");
		goto err_clk;
	}
	clk_rate_exclusive_get(priv->clk);

	if (pwm) {
		ret = xilinx_pwm_init(dev, priv);
	} else {
		ret = xilinx_clocksource_init(priv);
		if (!ret && !one_timer) {
			ret = xilinx_clockevent_init(np, priv);
			if (ret)
				priv->flags |= XILINX_TIMER_ONE;
		}
	}

	if (!ret)
		return priv;

	clk_rate_exclusive_put(priv->clk);
	clk_disable_unprepare(priv->clk);
err_clk:
	if (priv->flags & XILINX_TIMER_CLK)
		clk_unregister_fixed_rate(priv->clk);
	else
		clk_put(priv->clk);
err_priv:
	kfree(priv);
	return ERR_PTR(ret);
}

static int xilinx_timer_probe(struct platform_device *pdev)
{
	struct xilinx_timer_priv *priv =
		xilinx_timer_init(&pdev->dev, pdev->dev.of_node);

	if (IS_ERR(priv))
		return PTR_ERR(priv);

	platform_set_drvdata(pdev, priv);
	return 0;
}

static int xilinx_timer_remove(struct platform_device *pdev)
{
	struct xilinx_timer_priv *priv = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_XILINX_PWM) && priv->flags & XILINX_TIMER_PWM) {
		pwmchip_remove(&priv->pwm);
	} else {
		if (!(priv->flags & XILINX_TIMER_ONE)) {
			int cpu;

			for_each_cpu(cpu, priv->ce.cpumask)
				clockevents_unbind_device(&priv->ce, cpu);
		}
		clocksource_unregister(&priv->cs);
	}

	clk_rate_exclusive_put(priv->clk);
	clk_disable_unprepare(priv->clk);
	if (priv->flags & XILINX_TIMER_CLK)
		clk_unregister_fixed_rate(priv->clk);
	else
		clk_put(priv->clk);
	return 0;
}

static const struct of_device_id xilinx_timer_of_match[] = {
	{ .compatible = "xlnx,xps-timer-1.00.a", },
	{ .compatible = "xlnx,axi-timer-2.0" },
	{},
};
MODULE_DEVICE_TABLE(of, xilinx_timer_of_match);

static struct platform_driver xilinx_timer_driver = {
	.probe = xilinx_timer_probe,
	.remove = xilinx_timer_remove,
	.driver = {
		.name = "xilinx-timer",
		.of_match_table = of_match_ptr(xilinx_timer_of_match),
	},
};
module_platform_driver(xilinx_timer_driver);

static struct xilinx_timer_priv *xilinx_sched = (void *)-EAGAIN;

static u64 xilinx_sched_read(void)
{
	return xilinx_timer_read(xilinx_sched, TCSR0);
}

static int __init xilinx_timer_register(struct device_node *np)
{
	struct xilinx_timer_priv *priv;

	if (xilinx_sched != ERR_PTR(-EAGAIN))
		return -EPROBE_DEFER;

	priv = xilinx_timer_init(NULL, np);
	if (IS_ERR(priv))
		return PTR_ERR(priv);
	of_node_set_flag(np, OF_POPULATED);

	xilinx_sched = priv;
	sched_clock_register(xilinx_sched_read, priv->width,
			     clk_get_rate(priv->clk));
	return 0;
}

TIMER_OF_DECLARE(xilinx_xps_timer, "xlnx,xps-timer-1.00.a", xilinx_timer_register);
TIMER_OF_DECLARE(xilinx_axi_timer, "xlnx,axi-timer-2.0", xilinx_timer_register);

MODULE_ALIAS("platform:xilinx-timer");
MODULE_DESCRIPTION("Xilinx LogiCORE IP AXI Timer driver");
MODULE_LICENSE("GPL v2");
