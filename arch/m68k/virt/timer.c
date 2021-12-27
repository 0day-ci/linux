// SPDX-License-Identifier: GPL-2.0

#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clocksource.h>
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

static irqreturn_t golfish_timer_handler(int irq, void *dev_id)
{
	u64 now;

	gf_timer->clear_interrupt = 1;

	now = goldfish_timer_read(NULL);

	legacy_timer_tick(1);

	now += NSEC_PER_SEC / HZ;
	gf_timer->alarm_high = upper_32_bits(now);
	gf_timer->alarm_low = lower_32_bits(now);

	return IRQ_HANDLED;
}

void __init virt_sched_init(void)
{
	static struct resource sched_res;
	u64 now;

	sched_res.name  = "goldfish_timer";
	sched_res.start = virt_bi_data.rtc.mmio;
	sched_res.end   = virt_bi_data.rtc.mmio + 0xfff;

	if (request_resource(&iomem_resource, &sched_res)) {
		pr_err("Cannot allocate goldfish-timer resource\n");
		return;
	}

	if (request_irq(virt_bi_data.rtc.irq, golfish_timer_handler, IRQF_TIMER,
			"timer", NULL)) {
		pr_err("Couldn't register timer interrupt\n");
		return;
	}

	now = goldfish_timer_read(NULL);
	now += NSEC_PER_SEC / HZ;

	gf_timer->clear_interrupt = 1;
	gf_timer->alarm_high = upper_32_bits(now);
	gf_timer->alarm_low = lower_32_bits(now);
	gf_timer->irq_enabled = 1;

	clocksource_register_hz(&goldfish_timer, NSEC_PER_SEC);
}
