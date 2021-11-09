/* SPDX-License-Identifier: ISC */
/*
 * Author: Martin Kaistra <martin.kaistra@linutronix.de>
 * Copyright (C) 2021 Linutronix GmbH
 */

#ifndef _B53_PTP_H
#define _B53_PTP_H

#include "b53_priv.h"

#ifdef CONFIG_B53_PTP
int b53_ptp_init(struct b53_device *dev);
void b53_ptp_exit(struct b53_device *dev);
int b53_get_ts_info(struct dsa_switch *ds, int port,
		    struct ethtool_ts_info *info);
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

#endif
#endif
