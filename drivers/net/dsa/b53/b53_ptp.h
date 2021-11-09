/* SPDX-License-Identifier: ISC */
/*
 * Author: Martin Kaistra <martin.kaistra@linutronix.de>
 * Copyright (C) 2021 Linutronix GmbH
 */

#ifndef _B53_PTP_H
#define _B53_PTP_H

#include "b53_priv.h"

#define SKB_PTP_TYPE(__skb) (*(unsigned int *)((__skb)->cb))
#define TX_TSTAMP_TIMEOUT msecs_to_jiffies(40)

#ifdef CONFIG_B53_PTP
int b53_ptp_init(struct b53_device *dev);
void b53_ptp_exit(struct b53_device *dev);
int b53_get_ts_info(struct dsa_switch *ds, int port,
		    struct ethtool_ts_info *info);
int b53_port_hwtstamp_set(struct dsa_switch *ds, int port, struct ifreq *ifr);
int b53_port_hwtstamp_get(struct dsa_switch *ds, int port, struct ifreq *ifr);
bool b53_port_rxtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb,
		       unsigned int type);
void b53_port_txtstamp(struct dsa_switch *ds, int port, struct sk_buff *skb);

#else /* !CONFIG_B53_PTP */

static inline int b53_ptp_init(struct b53_device *dev)
{
	return 0;
}

static inline void b53_ptp_exit(struct b53_device *dev)
{
}

static inline int b53_get_ts_info(struct dsa_switch *ds, int port,
				  struct ethtool_ts_info *info)
{
	return -EOPNOTSUPP;
}

static inline int b53_port_hwtstamp_set(struct dsa_switch *ds, int port,
					struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int b53_port_hwtstamp_get(struct dsa_switch *ds, int port,
					struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline bool b53_port_rxtstamp(struct dsa_switch *ds, int port,
				     struct sk_buff *skb, unsigned int type)
{
	return false;
}

static inline void b53_port_txtstamp(struct dsa_switch *ds, int port,
				     struct sk_buff *skb)
{
}

#endif
#endif
