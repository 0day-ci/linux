// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Hewlett-Packard Enterprise Development Company, L.P.*/

#include <linux/bitops.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sched_clock.h>

#include <asm/irq.h>

#define TIMER0_FREQ	1000000

/*TCS Stands for Timer Control/Status: these are masks to be used in*/
/* the Timer Count Registers */
#define MASK_TCS_ENABLE	0x01
#define MASK_TCS_PERIOD	0x02
#define MASK_TCS_RELOAD	0x04
#define MASK_TCS_TC	0x80

struct gxp_timer {
	void __iomem *counter;
	void __iomem *control;
	struct clock_event_device evt;
};

static void __iomem *system_clock __read_mostly;

static inline struct gxp_timer *to_gxp_timer(struct clock_event_device *evt_dev)
{
	return container_of(evt_dev, struct gxp_timer, evt);
}

static u64 notrace gxp_sched_read(void)
{
	return readl_relaxed(system_clock);
}

static int gxp_time_set_next_event(unsigned long event,
								struct clock_event_device *evt_dev)
{
	struct gxp_timer *timer = to_gxp_timer(evt_dev);
	/* Stop counting and disable interrupt before updating */
	writeb_relaxed(MASK_TCS_TC, timer->control);
	writel_relaxed(event, timer->counter);
	writeb_relaxed(MASK_TCS_TC | MASK_TCS_ENABLE, timer->control);

	return 0;
}

static irqreturn_t gxp_time_interrupt(int irq, void *dev_id)
{
	struct gxp_timer *timer = dev_id;
	void (*event_handler)(struct clock_event_device *timer);

	if (!(readb_relaxed(timer->control) & MASK_TCS_TC))
		return IRQ_NONE;

	writeb_relaxed(MASK_TCS_TC, timer->control);

	event_handler = READ_ONCE(timer->evt.event_handler);
	event_handler(&timer->evt);

	return IRQ_HANDLED;
}

static int __init gxp_timer_init(struct device_node *node)
{
	void __iomem *base_counter;
	void __iomem *base_control;
	u32 freq;
	int ret, irq;
	struct gxp_timer *gxp_timer;

	base_counter = of_iomap(node, 0);
	if (!base_counter) {
		pr_err("Can't remap counter registers");
		ret = -ENXIO;
		return ret;
	}

	base_control = of_iomap(node, 1);
	if (!base_control) {
		pr_err("Can't remap control registers");
		ret = -ENXIO;
		goto err_iounmap;
	}

	system_clock = of_iomap(node, 2);
	if (!system_clock) {
		pr_err("Can't remap control registers");
		ret = -ENXIO;
		goto err_iounmap;
	}

	if (of_property_read_u32(node, "clock-frequency", &freq)) {
		pr_err("Can't read clock-frequency\n");
		ret = -EIO;
		goto err_iounmap;
	}

	sched_clock_register(gxp_sched_read, 32, freq);
	clocksource_mmio_init(system_clock, node->name, freq,
				300, 32, clocksource_mmio_readl_up);

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		ret = -EINVAL;
		pr_err("GXP Timer Can't parse IRQ %d", irq);
		goto err_iounmap;
	}

	gxp_timer = kzalloc(sizeof(*gxp_timer), GFP_KERNEL);
	if (!gxp_timer) {
		ret = -ENOMEM;
		goto err_iounmap;
	}

	gxp_timer->counter = base_counter;
	gxp_timer->control = base_control;
	gxp_timer->evt.name = node->name;
	gxp_timer->evt.rating = 300;
	gxp_timer->evt.features = CLOCK_EVT_FEAT_ONESHOT;
	gxp_timer->evt.set_next_event = gxp_time_set_next_event;
	gxp_timer->evt.cpumask = cpumask_of(0);

	if (request_irq(irq, gxp_time_interrupt, IRQF_TIMER | IRQF_SHARED,
			node->name, gxp_timer)) {
		ret = -EIO;
		pr_err("%s: request_irq() failed\n", "GXP Timer Tick");
		goto err_timer_free;
	}

	clockevents_config_and_register(&gxp_timer->evt, TIMER0_FREQ,
					0xf, 0xffffffff);

	pr_info("gxp: system timer (irq = %d)\n", irq);
	return 0;


err_timer_free:
	kfree(gxp_timer);

err_iounmap:
	iounmap(system_clock);
	iounmap(base_control);
	iounmap(base_counter);
	return ret;
}

TIMER_OF_DECLARE(gxp, "hpe,gxp-timer", gxp_timer_init);
