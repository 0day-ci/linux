/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MV88E6XXX_DST_H
#define _MV88E6XXX_DST_H

struct net_device *mv88e6xxx_dst_bridge_from_dsa(struct dsa_switch_tree *dst,
						 u8 dev, u8 port);
int mv88e6xxx_dst_bridge_join(struct dsa_switch_tree *dst,
			      struct net_device *brdev);
void mv88e6xxx_dst_bridge_leave(struct dsa_switch_tree *dst,
				struct net_device *brdev);
int mv88e6xxx_dst_add_chip(struct mv88e6xxx_chip *chip);

#endif /* _MV88E6XXX_DST_H */
