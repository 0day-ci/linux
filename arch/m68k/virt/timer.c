// SPDX-License-Identifier: GPL-2.0

#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <asm/virt.h>

struct goldfish_timer {
	u32 time_low;
	u32 time_high;
	u32 alarm_low;
	u32 alarm_high;
	u32 irq_enabled;
	u32 clear_alarm;
	u32 alarm_status;
	u32 clear_interrupt;
};

#define gf_timer ((volatile struct goldfish_timer *)virt_bi_data.rtc.mmio)

static u64 goldfish_timer_read(struct clocksource *cs)
{
	u64 ticks;

	/*
	 * time_low: get low bits of current time and update time_high
	 * time_high: get high bits of time at last time_low read
	 */
	ticks = gf_timer->time_low;
	ticks += (u64)gf_timer->time_high << 32;

	return ticks;
}

static struct clocksource goldfish_timer = {
	.name		= "goldfish_timer",
	.rating		= 400,
	.read		= goldfish_timer_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= 0,
	.max_idle_ns	= LONG_MAX,
};

static int goldfish_timer_set_oneshot(struct clock_event_device *evt)
{
	gf_timer->alarm_high = 0;
	gf_timer->alarm_low = 0;

	gf_timer->irq_enabled = 1;

	return 0;
}

static int goldfish_timer_shutdown(struct clock_event_device *evt)
{
	gf_timer->irq_enabled = 0;

	return 0;
}

static int goldfish_timer_next_event(unsigned long delta,
				     struct clock_event_device *evt)
{
	u64 now;

	gf_timer->clear_interrupt = 1;

	now = goldfish_timer_read(NULL);

	now += delta;

	gf_timer->alarm_high = upper_32_bits(now);
	gf_timer->alarm_low = lower_32_bits(now);

	return 0;
}

struct clock_event_device goldfish_timer_clockevent = {
	.name			= "goldfish_timer",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= goldfish_timer_shutdown,
	.set_state_oneshot      = goldfish_timer_set_oneshot,
	.set_next_event		= goldfish_timer_next_event,
	.shift			= 32,
};

static irqreturn_t golfish_timer_tick(int irq, void *dev_id)
{
	struct clock_event_device *evt = &goldfish_timer_clockevent;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

void __init virt_sched_init(void)
{
	static struct resource sched_res;

	sched_res.name  = "goldfish_timer";
	sched_res.start = virt_bi_data.rtc.mmio;
	sched_res.end   = virt_bi_data.rtc.mmio + 0xfff;

	if (request_resource(&iomem_resource, &sched_res)) {
		pr_err("Cannot allocate goldfish-timer resource\n");
		return;
	}

	clockevents_config_and_register(&goldfish_timer_clockevent, NSEC_PER_SEC,
					1, 0xffffffff);

	if (request_irq(virt_bi_data.rtc.irq, golfish_timer_tick, IRQF_TIMER,
			"timer", NULL)) {
		pr_err("Couldn't register timer interrupt\n");
		return;
	}

	clocksource_register_hz(&goldfish_timer, NSEC_PER_SEC);
}
