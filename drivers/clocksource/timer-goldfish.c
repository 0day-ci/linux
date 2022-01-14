// SPDX-License-Identifier: GPL-2.0

#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/module.h>
#include <linux/slab.h>
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

/* goldfish endianness depends on CPU endianness */
#ifdef CONFIG_CPU_BIG_ENDIAN
#define goldfish_ioread32 ioread32be
#define goldfish_iowrite32 iowrite32be
#else
#define goldfish_ioread32 ioread32
#define goldfish_iowrite32 iowrite32
#endif

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

	goldfish_iowrite32(0, base + TIMER_ALARM_HIGH);
	goldfish_iowrite32(0, base + TIMER_ALARM_LOW);
	goldfish_iowrite32(1, base + TIMER_IRQ_ENABLED);

	return 0;
}

static int goldfish_timer_shutdown(struct clock_event_device *evt)
{
	struct goldfish_timer *timerdrv = ced_to_gf(evt);
	void __iomem *base = timerdrv->base;

	goldfish_iowrite32(0, base + TIMER_IRQ_ENABLED);

	return 0;
}

static int goldfish_timer_next_event(unsigned long delta,
				     struct clock_event_device *evt)
{
	struct goldfish_timer *timerdrv = ced_to_gf(evt);
	void __iomem *base = timerdrv->base;
	u64 now;

	goldfish_iowrite32(1, base + TIMER_CLEAR_INTERRUPT);

	/*
	 * time_low: get low bits of current time and update time_high
	 * time_high: get high bits of time at last time_low read
	 */
	now = goldfish_ioread32(base + TIMER_TIME_LOW);
	now += (u64)goldfish_ioread32(base + TIMER_TIME_HIGH) << 32;

	now += delta;

	goldfish_iowrite32(upper_32_bits(now), base + TIMER_ALARM_HIGH);
	goldfish_iowrite32(lower_32_bits(now), base + TIMER_ALARM_LOW);

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
