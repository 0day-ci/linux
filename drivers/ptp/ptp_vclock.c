// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP virtual clock driver
 *
 * Copyright 2021 NXP
 */
#include <linux/slab.h>
#include "ptp_private.h"

static int ptp_vclock_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	unsigned long flags;
	s64 adj;

	adj = (s64)scaled_ppm * vclock->mult_factor;
	adj = div_s64(adj, vclock->div_factor);

	spin_lock_irqsave(&vclock->lock, flags);
	timecounter_read(&vclock->tc);
	vclock->cc.mult = vclock->mult + adj;
	spin_unlock_irqrestore(&vclock->lock, flags);

	return 0;
}

static int ptp_vclock_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	unsigned long flags;

	spin_lock_irqsave(&vclock->lock, flags);
	timecounter_adjtime(&vclock->tc, delta);
	spin_unlock_irqrestore(&vclock->lock, flags);

	return 0;
}

static int ptp_vclock_gettime(struct ptp_clock_info *ptp,
			      struct timespec64 *ts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&vclock->lock, flags);
	ns = timecounter_read(&vclock->tc);
	spin_unlock_irqrestore(&vclock->lock, flags);
	*ts = ns_to_timespec64(ns);

	return 0;
}

static int ptp_vclock_settime(struct ptp_clock_info *ptp,
			      const struct timespec64 *ts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	u64 ns = timespec64_to_ns(ts);
	unsigned long flags;

	spin_lock_irqsave(&vclock->lock, flags);
	timecounter_init(&vclock->tc, &vclock->cc, ns);
	spin_unlock_irqrestore(&vclock->lock, flags);

	return 0;
}

static const struct ptp_clock_info ptp_vclock_info = {
	.owner		= THIS_MODULE,
	.name		= "ptp virtual clock",
	.vclock_flag	= true,
	/* The maximum ppb value that long scaled_ppm can support */
	.max_adj	= 32767999,
	.adjfine	= ptp_vclock_adjfine,
	.adjtime	= ptp_vclock_adjtime,
	.gettime64	= ptp_vclock_gettime,
	.settime64	= ptp_vclock_settime,
};

static void ptp_vclock_refresh(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct ptp_vclock *vclock = dw_to_vclock(dw);
	struct timespec64 ts;

	ptp_vclock_gettime(&vclock->info, &ts);
	schedule_delayed_work(&vclock->refresh_work, vclock->refresh_interval);
}

static int ptp_convert_domain_tstamp(struct device *dev, void *data)
{
	struct ptp_clock *ptp = dev_get_drvdata(dev);
	struct ptp_clock_info *info = ptp->info;
	struct domain_tstamp *domain_ts = data;
	struct ptp_vclock *vclock;
	unsigned long flags;

	/* Convert to domain tstamp if there is a domain matched */
	if (ptp->domain == domain_ts->domain) {
		vclock = info_to_vclock(info);
		spin_lock_irqsave(&vclock->lock, flags);
		domain_ts->tstamp = timecounter_cyc2time(&vclock->tc,
							 domain_ts->tstamp);
		spin_unlock_irqrestore(&vclock->lock, flags);
		return -EINVAL;	/* For break. Not error. */
	}

	return 0;
}

void ptp_clock_domain_tstamp(struct device *ptp_dev, u64 *tstamp, u8 domain)
{
	struct domain_tstamp domain_ts;

	domain_ts.tstamp = *tstamp;
	domain_ts.domain = domain;

	device_for_each_child(ptp_dev, &domain_ts, ptp_convert_domain_tstamp);
	*tstamp = domain_ts.tstamp;
}
EXPORT_SYMBOL(ptp_clock_domain_tstamp);

struct ptp_clock_info *ptp_get_pclock_info(const struct cyclecounter *cc)
{
	struct ptp_vclock *vclock = cc_to_vclock(cc);

	return vclock->pclock->info;
}
EXPORT_SYMBOL(ptp_get_pclock_info);

struct ptp_vclock *ptp_vclock_register(struct ptp_clock *pclock)
{
	struct ptp_vclock_cc *vclock_cc = pclock->info->vclock_cc;
	struct ptp_vclock *vclock;

	vclock = kzalloc(sizeof(*vclock), GFP_KERNEL);
	if (!vclock)
		return NULL;

	vclock->pclock = pclock;

	vclock->info = ptp_vclock_info;
	vclock->info.vclock_cc = vclock_cc;
	snprintf(vclock->info.name, PTP_CLOCK_NAME_LEN,
		 "virtual clock on ptp%d", pclock->index);

	/* Copy members initial values of ptp_vclock_cc to ptp_vclock */
	vclock->cc = vclock_cc->cc;
	vclock->mult = vclock_cc->cc.mult;
	vclock->refresh_interval = vclock_cc->refresh_interval;
	vclock->mult_factor = vclock_cc->mult_factor;
	vclock->div_factor = vclock_cc->div_factor;

	spin_lock_init(&vclock->lock);

	vclock->clock = ptp_clock_register(&vclock->info, pclock->dev.parent);
	if (IS_ERR_OR_NULL(vclock->clock)) {
		kfree(vclock);
		return NULL;
	}

	timecounter_init(&vclock->tc, &vclock->cc,
			 ktime_to_ns(ktime_get_real()));

	INIT_DELAYED_WORK(&vclock->refresh_work, ptp_vclock_refresh);
	schedule_delayed_work(&vclock->refresh_work, vclock->refresh_interval);

	return vclock;
}

void ptp_vclock_unregister(struct ptp_vclock *vclock)
{
	cancel_delayed_work_sync(&vclock->refresh_work);
	ptp_clock_unregister(vclock->clock);
	kfree(vclock);
}
