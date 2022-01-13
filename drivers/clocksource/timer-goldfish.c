// SPDX-License-Identifier: GPL-2.0

#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/module.h>
#include <clocksource/timer-goldfish.h>

#define TIMER_TIME_LOW		0x00	/* get low bits of current time  */
					/*   and update TIMER_TIME_HIGH  */
#define TIMER_TIME_HIGH		0x04	/* get high bits of time at last */
					/*   TIMER_TIME_LOW read         */
#define TIMER_ALARM_LOW		0x08	/* set low bits of alarm and     */
					/*   activate it                 */
#define TIMER_ALARM_HIGH	0x0c	/* set high bits of next alarm   */
#define TIMER_IRQ_ENABLED	0x10
#define TIMER_CLEAR_ALARM	0x14
#define TIMER_ALARM_STATUS	0x18
#define TIMER_CLEAR_INTERRUPT	0x1c

struct goldfish_timer {
	struct clock_event_device ced;
	struct resource res;
	void __iomem *base;
	int irq;
};

static struct goldfish_timer *ced_to_gf(struct clock_event_device *ced)
{
	return container_of(ced, struct goldfish_timer, ced);
}

static int goldfish_timer_set_oneshot(struct clock_event_device *evt)
{
	struct goldfish_timer *timerdrv = ced_to_gf(evt);
	void __iomem *base = timerdrv->base;

	__raw_writel(0, base + TIMER_ALARM_HIGH);
	__raw_writel(0, base + TIMER_ALARM_LOW);
	__raw_writel(1, base + TIMER_IRQ_ENABLED);

	return 0;
}

static int goldfish_timer_shutdown(struct clock_event_device *evt)
{
	struct goldfish_timer *timerdrv = ced_to_gf(evt);
	void __iomem *base = timerdrv->base;

	__raw_writel(0, base + TIMER_IRQ_ENABLED);

	return 0;
}

static int goldfish_timer_next_event(unsigned long delta,
				     struct clock_event_device *evt)
{
	struct goldfish_timer *timerdrv = ced_to_gf(evt);
	void __iomem *base = timerdrv->base;
	u64 now;

	__raw_writel(1, base + TIMER_CLEAR_INTERRUPT);

	/*
	 * time_low: get low bits of current time and update time_high
	 * time_high: get high bits of time at last time_low read
	 */
	now = __raw_readl(base + TIMER_TIME_LOW);
	now += (u64)__raw_readl(base + TIMER_TIME_HIGH) << 32;

	now += delta;

	__raw_writel(upper_32_bits(now), base + TIMER_ALARM_HIGH);
	__raw_writel(lower_32_bits(now), base + TIMER_ALARM_LOW);

	return 0;
}

static irqreturn_t golfish_timer_tick(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

void __init goldfish_timer_init(int irq, void __iomem *base)
{
	struct goldfish_timer *timerdrv;
	int ret;

	timerdrv = kzalloc(sizeof(*timerdrv), GFP_KERNEL);
	if (!timerdrv)
		return;

	timerdrv->base = base;
	timerdrv->irq = irq;

	timerdrv->ced = (struct clock_event_device){
		.name			= "goldfish_timer",
		.features		= CLOCK_EVT_FEAT_ONESHOT,
		.set_state_shutdown	= goldfish_timer_shutdown,
		.set_state_oneshot      = goldfish_timer_set_oneshot,
		.set_next_event		= goldfish_timer_next_event,
		.shift			= 32,
	};
	timerdrv->res = (struct resource){
		.name  = "goldfish_timer",
		.start = (unsigned long)base,
		.end   = (unsigned long)base + 0xfff,
	};

	if (request_resource(&iomem_resource, &timerdrv->res)) {
		pr_err("Cannot allocate goldfish-timer resource\n");
		return;
	}

	ret = request_irq(timerdrv->irq, golfish_timer_tick, IRQF_TIMER,
			  "goldfish_timer", &timerdrv->ced);
	if (ret) {
		pr_err("Couldn't register goldfish-timer interrupt\n");
		return;
	}

	clockevents_config_and_register(&timerdrv->ced, NSEC_PER_SEC,
					1, 0xffffffff);
}
EXPORT_SYMBOL_GPL(goldfish_timer_init);
