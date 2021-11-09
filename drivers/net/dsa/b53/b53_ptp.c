// SPDX-License-Identifier: ISC
/*
 * B53 switch PTP support
 *
 * Author: Martin Kaistra <martin.kaistra@linutronix.de>
 * Copyright (C) 2021 Linutronix GmbH
 */

#include "b53_priv.h"
#include "b53_ptp.h"

static int b53_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct b53_device *dev =
		container_of(ptp, struct b53_device, ptp_clock_info);
	u64 ns;

	mutex_lock(&dev->ptp_mutex);
	ns = timecounter_read(&dev->tc);
	mutex_unlock(&dev->ptp_mutex);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int b53_ptp_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *ts)
{
	struct b53_device *dev =
		container_of(ptp, struct b53_device, ptp_clock_info);
	u64 ns;

	ns = timespec64_to_ns(ts);

	mutex_lock(&dev->ptp_mutex);
	timecounter_init(&dev->tc, &dev->cc, ns);
	mutex_unlock(&dev->ptp_mutex);

	return 0;
}

static int b53_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct b53_device *dev =
		container_of(ptp, struct b53_device, ptp_clock_info);
	u64 adj, diff;
	u32 mult;
	bool neg_adj = false;

	if (scaled_ppm < 0) {
		neg_adj = true;
		scaled_ppm = -scaled_ppm;
	}

	mult = (1 << 28);
	adj = 64;
	adj *= (u64)scaled_ppm;
	diff = div_u64(adj, 15625ULL);

	mutex_lock(&dev->ptp_mutex);
	timecounter_read(&dev->tc);
	dev->cc.mult = neg_adj ? mult - diff : mult + diff;
	mutex_unlock(&dev->ptp_mutex);

	return 0;
}

static int b53_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct b53_device *dev =
		container_of(ptp, struct b53_device, ptp_clock_info);

	mutex_lock(&dev->ptp_mutex);
	timecounter_adjtime(&dev->tc, delta);
	mutex_unlock(&dev->ptp_mutex);

	return 0;
}

static u64 b53_ptp_read(const struct cyclecounter *cc)
{
	struct b53_device *dev = container_of(cc, struct b53_device, cc);
	u32 ts;

	b53_read32(dev, B53_BROADSYNC_PAGE, B53_BROADSYNC_TIMEBASE, &ts);

	return ts;
}

static int b53_ptp_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static long b53_hwtstamp_work(struct ptp_clock_info *ptp)
{
	struct b53_device *dev =
		container_of(ptp, struct b53_device, ptp_clock_info);

	mutex_lock(&dev->ptp_mutex);
	timecounter_read(&dev->tc);
	mutex_unlock(&dev->ptp_mutex);

	return B53_PTP_OVERFLOW_PERIOD;
}

int b53_ptp_init(struct b53_device *dev)
{
	mutex_init(&dev->ptp_mutex);

	/* Enable BroadSync HD for all ports */
	b53_write16(dev, B53_BROADSYNC_PAGE, B53_BROADSYNC_EN_CTRL,
		    dev->enabled_ports);

	/* Enable BroadSync HD Time Stamping Reporting (Egress) */
	b53_write8(dev, B53_BROADSYNC_PAGE, B53_BROADSYNC_TS_REPORT_CTRL,
		   TSRPT_PKT_EN);

	/* Enable BroadSync HD Time Stamping for PTPv2 ingress */

	/* MPORT_CTRL0 | MPORT0_TS_EN */
	b53_write16(dev, B53_ARLCTRL_PAGE, B53_MPORT_CTRL,
		    MPORT0_TS_EN |
			    (MPORT_CTRL_CMP_ETYPE << MPORT_CTRL_SHIFT(0)));
	/* Forward to IMP port */
	b53_write32(dev, B53_ARLCTRL_PAGE, B53_MPORT_VCTR(0),
		    BIT(dev->imp_port));
	/* PTPv2 Ether Type */
	b53_write64(dev, B53_ARLCTRL_PAGE, B53_MPORT_ADDR(0),
		    MPORT_ETYPE(ETH_P_1588));

	/* Setup PTP clock */
	memset(&dev->ptp_clock_info, 0, sizeof(dev->ptp_clock_info));

	dev->ptp_clock_info.owner = THIS_MODULE;
	snprintf(dev->ptp_clock_info.name, sizeof(dev->ptp_clock_info.name),
		 dev_name(dev->dev));

	dev->ptp_clock_info.max_adj = 1000000000ULL;
	dev->ptp_clock_info.adjfine = b53_ptp_adjfine;
	dev->ptp_clock_info.adjtime = b53_ptp_adjtime;
	dev->ptp_clock_info.gettime64 = b53_ptp_gettime;
	dev->ptp_clock_info.settime64 = b53_ptp_settime;
	dev->ptp_clock_info.enable = b53_ptp_enable;
	dev->ptp_clock_info.do_aux_work = b53_hwtstamp_work;

	dev->ptp_clock = ptp_clock_register(&dev->ptp_clock_info, dev->dev);
	if (IS_ERR(dev->ptp_clock))
		return PTR_ERR(dev->ptp_clock);

	/* The switch provides a 32 bit free running counter. Use the Linux
	 * cycle counter infrastructure which is suited for such scenarios.
	 */
	dev->cc.read = b53_ptp_read;
	dev->cc.mask = CYCLECOUNTER_MASK(30);
	dev->cc.overflow_point = 999999999;
	dev->cc.mult = (1 << 28);
	dev->cc.shift = 28;

	timecounter_init(&dev->tc, &dev->cc, ktime_to_ns(ktime_get_real()));

	ptp_schedule_worker(dev->ptp_clock, 0);

	return 0;
}
EXPORT_SYMBOL(b53_ptp_init);

int b53_get_ts_info(struct dsa_switch *ds, int port,
		    struct ethtool_ts_info *info)
{
	struct b53_device *dev = ds->priv;

	info->phc_index = dev->ptp_clock ? ptp_clock_index(dev->ptp_clock) : -1;
	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE);

	return 0;
}
EXPORT_SYMBOL(b53_get_ts_info);

void b53_ptp_exit(struct b53_device *dev)
{
	if (dev->ptp_clock)
		ptp_clock_unregister(dev->ptp_clock);
	dev->ptp_clock = NULL;
}
EXPORT_SYMBOL(b53_ptp_exit);

MODULE_AUTHOR("Martin Kaistra <martin.kaistra@linutronix.de>");
MODULE_DESCRIPTION("B53 Switch PTP support");
MODULE_LICENSE("Dual BSD/GPL");
