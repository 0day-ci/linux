// SPDX-License-Identifier: GPL-2.0
/*
 * Realtek Otto platform timer/counter
 *
 * Copyright
 *	Sander Vanheule - 2022
 *
 * Up-counting 28-bit timer that can operate in oneshot or repeat mode,
 * providing interrupts at roll-over. The maximum value is written to the DATA
 * register, while the current timer value can be read from the CNT register.
 *
 * A divided clock is used to drive the timer, derived from the bus clock. The
 * clock divisor is configurable from 2 to 65535. Divisor values of 0 and 1
 * disable the timer clock. The timer can also be enabled independently from
 * the clock selection with the ENABLE flag.
 *
 * The SoC's interrupt controller can configure the CPU affinity of the timer
 * interrupts to any subset of the available CPUs.
 */

#include <linux/bitfield.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include "timer-of.h"

struct otto_tc_ctrl {
	struct timer_of to;
	spinlock_t lock;
};

#define OTTO_TC_REG_OFFSET(to, offset)	(timer_of_base(to) + offset)

#define OTTO_TC_REG_DATA(to)		OTTO_TC_REG_OFFSET(to, 0x0)
#define OTTO_TC_REG_CNT(to)		OTTO_TC_REG_OFFSET(to, 0x4)
#define OTTO_TC_WIDTH			28
#define OTTO_TC_MAX_PERIOD		BIT(OTTO_TC_WIDTH)

#define OTTO_TC_REG_CTL(to)		OTTO_TC_REG_OFFSET(to, 0x8)
#define OTTO_TC_CTL_ENABLE		BIT(28)
#define OTTO_TC_CTL_MODE		BIT(24)
#define OTTO_TC_MODE_ONESHOT		FIELD_PREP(OTTO_TC_CTL_MODE, 0)
#define OTTO_TC_MODE_REPEAT		FIELD_PREP(OTTO_TC_CTL_MODE, 1)
#define OTTO_TC_CTL_DIVISOR		GENMASK(15, 0)

#define OTTO_TC_MIN_DIVISOR		2
#define OTTO_TC_MAX_DIVISOR		OTTO_TC_CTL_DIVISOR

#define OTTO_TC_REG_ICTL(to)		OTTO_TC_REG_OFFSET(to, 0xC)
#define OTTO_TC_ICTL_ENABLE		BIT(20)
#define OTTO_TC_ICTL_STATUS		BIT(16)

static inline void otto_tc_irq_enable_clear(struct timer_of *to)
{
	writel(OTTO_TC_ICTL_ENABLE | OTTO_TC_ICTL_STATUS, OTTO_TC_REG_ICTL(to));
}

static inline struct otto_tc_ctrl *otto_tc_timer_to_ctrl(struct timer_of *to)
{
	return container_of(to, struct otto_tc_ctrl, to);
}

static irqreturn_t otto_tc_handler(int irq, void *dev_id)
{
	struct clock_event_device *ced = dev_id;
	struct timer_of *to = to_timer_of(ced);

	otto_tc_irq_enable_clear(to);
	ced->event_handler(ced);

	return IRQ_HANDLED;
}

static void otto_tc_set_divisor(struct otto_tc_ctrl *ctrl, u16 divisor)
{
	struct timer_of *to = &ctrl->to;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ctrl->lock, flags);
	val = readl(OTTO_TC_REG_CTL(to)) & ~OTTO_TC_CTL_DIVISOR;
	val |= FIELD_PREP(OTTO_TC_CTL_DIVISOR, divisor);
	writel(val, OTTO_TC_REG_CTL(to));
	spin_unlock_irqrestore(&ctrl->lock, flags);
}

static void otto_tc_set_timeout(struct clock_event_device *ced, u32 value)
{
	struct timer_of *to = to_timer_of(ced);
	struct otto_tc_ctrl *ctrl = otto_tc_timer_to_ctrl(to);
	unsigned long flags;

	spin_lock_irqsave(&ctrl->lock, flags);
	writel(value, OTTO_TC_REG_DATA(to));
	spin_unlock_irqrestore(&ctrl->lock, flags);
}

/*
 * Timers can only be (re)started from the disabled state. The clkevt state
 * machine is expected perform the necessary disables (shutdown) before moving
 * a timer into an enabled state through .set_oneshot() or .set_periodic().
 */
static void otto_tc_set_mode(struct clock_event_device *ced, bool started, u32 mode)
{
	struct timer_of *to = to_timer_of(ced);
	struct otto_tc_ctrl *ctrl = otto_tc_timer_to_ctrl(to);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ctrl->lock, flags);
	val = readl(OTTO_TC_REG_CTL(to));
	val = (val & ~OTTO_TC_CTL_MODE) | mode;
	if (started)
		val |= OTTO_TC_CTL_ENABLE;
	else
		val &= ~OTTO_TC_CTL_ENABLE;
	writel(val, OTTO_TC_REG_CTL(to));
	spin_unlock_irqrestore(&ctrl->lock, flags);
}

static int otto_tc_set_next_event(unsigned long evt, struct clock_event_device *ced)
{
	otto_tc_set_timeout(ced, evt);

	return 0;
}

static int otto_tc_set_periodic(struct clock_event_device *ced)
{
	struct timer_of *to = to_timer_of(ced);

	otto_tc_set_timeout(ced, timer_of_period(to));
	otto_tc_set_mode(ced, true, OTTO_TC_MODE_REPEAT);

	return 0;
}

static int otto_tc_set_oneshot(struct clock_event_device *ced)
{
	otto_tc_set_mode(ced, true, OTTO_TC_MODE_ONESHOT);

	return 0;
}

static int otto_tc_set_oneshot_stopped(struct clock_event_device *ced)
{
	otto_tc_set_mode(ced, false, OTTO_TC_MODE_ONESHOT);

	return 0;
}

static int otto_tc_set_shutdown(struct clock_event_device *ced)
{
	otto_tc_set_mode(ced, false, 0);

	return 0;
}

static void __init otto_tc_init_clkevt(struct timer_of *to)
{
	struct clock_event_device *ced = &to->clkevt;

	ced->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC |
		CLOCK_EVT_FEAT_DYNIRQ;
	ced->set_next_event = otto_tc_set_next_event;
	ced->set_state_periodic = otto_tc_set_periodic;
	ced->set_state_oneshot = otto_tc_set_oneshot;
	ced->set_state_oneshot_stopped = otto_tc_set_oneshot_stopped;
	ced->set_state_shutdown = otto_tc_set_shutdown;
	ced->cpumask = cpumask_of(0);
	ced->rating = 300;

	clockevents_config_and_register(ced, timer_of_rate(to), 1, OTTO_TC_MAX_PERIOD);
}

static int __init otto_tc_init(struct device_node *node)
{
	struct otto_tc_ctrl *ctrl;
	int err;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	spin_lock_init(&ctrl->lock);

	ctrl->to.flags = TIMER_OF_BASE | TIMER_OF_IRQ | TIMER_OF_CLOCK;
	ctrl->to.of_clk.name = "bus";
	ctrl->to.of_irq.flags = IRQF_TIMER;
	ctrl->to.of_irq.handler = otto_tc_handler;

	err = timer_of_init(node, &ctrl->to);
	if (err)
		goto err_out;

	/* Reset timer state */
	writel(0, OTTO_TC_REG_CTL(&ctrl->to));
	writel(0, OTTO_TC_REG_DATA(&ctrl->to));

	/* TODO Replace by a real derived clock */
	otto_tc_set_divisor(ctrl, OTTO_TC_MIN_DIVISOR);
	ctrl->to.of_clk.rate /= OTTO_TC_MIN_DIVISOR;
	ctrl->to.of_clk.period /= OTTO_TC_MIN_DIVISOR;

	otto_tc_irq_enable_clear(&ctrl->to);
	otto_tc_init_clkevt(&ctrl->to);

	return 0;

err_out:
	kfree(ctrl);

	return err;
}
TIMER_OF_DECLARE(otto_tc, "realtek,otto-tc", otto_tc_init);
