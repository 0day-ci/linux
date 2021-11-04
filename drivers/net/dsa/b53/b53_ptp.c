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

	b53_read32(dev, B53_BROADSYNC_PAGE, B53_BROADSYNC_TIMEBASE1, &ts);

	return ts;
}

static int b53_ptp_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static void b53_ptp_overflow_check(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct b53_device *dev =
		container_of(dw, struct b53_device, overflow_work);

	mutex_lock(&dev->ptp_mutex);
	timecounter_read(&dev->tc);
	mutex_unlock(&dev->ptp_mutex);

	schedule_delayed_work(&dev->overflow_work, B53_PTP_OVERFLOW_PERIOD);
}

int b53_ptp_init(struct b53_device *dev)
{
	mutex_init(&dev->ptp_mutex);

	INIT_DELAYED_WORK(&dev->overflow_work, b53_ptp_overflow_check);

	/* Enable BroadSync HD for all ports */
	b53_write16(dev, B53_BROADSYNC_PAGE, B53_BROADSYNC_EN_CTRL1, 0x00ff);

	/* Enable BroadSync HD Time Stamping Reporting (Egress) */
	b53_write8(dev, B53_BROADSYNC_PAGE, B53_BROADSYNC_TS_REPORT_CTRL, 0x01);

	/* Enable BroadSync HD Time Stamping for PTPv2 ingress */

	/* MPORT_CTRL0 | MPORT0_TS_EN */
	b53_write16(dev, B53_ARLCTRL_PAGE, 0x0e, (1 << 15) | 0x01);
	/* Forward to IMP port 8 */
	b53_write64(dev, B53_ARLCTRL_PAGE, 0x18, (1 << 8));
	/* PTPv2 Ether Type */
	b53_write64(dev, B53_ARLCTRL_PAGE, 0x10, (u64)0x88f7 << 48);

	/* Setup PTP clock */
	dev->ptp_clock_info.owner = THIS_MODULE;
	snprintf(dev->ptp_clock_info.name, sizeof(dev->ptp_clock_info.name),
		 dev_name(dev->dev));

	dev->ptp_clock_info.max_adj = 1000000000ULL;
	dev->ptp_clock_info.n_alarm = 0;
	dev->ptp_clock_info.n_pins = 0;
	dev->ptp_clock_info.n_ext_ts = 0;
	dev->ptp_clock_info.n_per_out = 0;
	dev->ptp_clock_info.pps = 0;
	dev->ptp_clock_info.adjfine = b53_ptp_adjfine;
	dev->ptp_clock_info.adjtime = b53_ptp_adjtime;
	dev->ptp_clock_info.gettime64 = b53_ptp_gettime;
	dev->ptp_clock_info.settime64 = b53_ptp_settime;
	dev->ptp_clock_info.enable = b53_ptp_enable;

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

	b53_write32(dev, B53_BROADSYNC_PAGE, B53_BROADSYNC_TIMEBASE_ADJ1, 40);

	timecounter_init(&dev->tc, &dev->cc, ktime_to_ns(ktime_get_real()));

	schedule_delayed_work(&dev->overflow_work, B53_PTP_OVERFLOW_PERIOD);

	return 0;
}

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

void b53_ptp_exit(struct b53_device *dev)
{
	cancel_delayed_work_sync(&dev->overflow_work);
	if (dev->ptp_clock)
		ptp_clock_unregister(dev->ptp_clock);
	dev->ptp_clock = NULL;
}
