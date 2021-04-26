/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NET_DSA_MV88E6XXX_H
#define _NET_DSA_MV88E6XXX_H

#include <linux/netdevice.h>
#include <net/dsa.h>

int mv88e6xxx_dst_bridge_to_dsa(const struct dsa_switch_tree *dst,
				const struct net_device *brdev,
				u8 *dev, u8 *port);

#endif /* _NET_DSA_MV88E6XXX_H */
