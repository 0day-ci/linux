// SPDX-License-Identifier: GPL-2.0
/*
 * mv88e6xxx global DSA switch tree state
 */

#include <linux/bitmap.h>
#include <linux/dsa/mv88e6xxx.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/dsa.h>

#include "chip.h"
#include "dst.h"
#include "global2.h"

struct mv88e6xxx_br {
	struct list_head list;

	struct net_device *brdev;
	u8 dev;
	u8 port;
};

struct mv88e6xxx_dst {
	struct list_head bridges;

	DECLARE_BITMAP(busy_ports, MV88E6XXX_MAX_PVT_ENTRIES);

#define DEV_PORT_TO_BIT(_dev, _port)			\
	((_dev) * MV88E6XXX_MAX_PVT_PORTS + (_port))
#define DEV_FROM_BIT(_bit) ((_bit) / MV88E6XXX_MAX_PVT_PORTS)
#define PORT_FROM_BIT(_bit) ((_bit) % (MV88E6XXX_MAX_PVT_PORTS))
};

struct net_device *mv88e6xxx_dst_bridge_from_dsa(struct dsa_switch_tree *dst,
						 u8 dev, u8 port)
{
	struct mv88e6xxx_dst *mvdst = dst->priv;
	struct mv88e6xxx_br *mvbr;

	list_for_each_entry(mvbr, &mvdst->bridges, list) {
		if (mvbr->dev == dev && mvbr->port == port)
			return mvbr->brdev;
	}

	return NULL;
}

int mv88e6xxx_dst_bridge_to_dsa(const struct dsa_switch_tree *dst,
				const struct net_device *brdev,
				u8 *dev, u8 *port)
{
	struct mv88e6xxx_dst *mvdst = dst->priv;
	struct mv88e6xxx_br *mvbr;

	list_for_each_entry(mvbr, &mvdst->bridges, list) {
		if (mvbr->brdev == brdev) {
			*dev = mvbr->dev;
			*port = mvbr->port;
			return 0;
		}
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(mv88e6xxx_dst_bridge_to_dsa);

int mv88e6xxx_dst_bridge_join(struct dsa_switch_tree *dst,
			      struct net_device *brdev)
{
	struct mv88e6xxx_dst *mvdst = dst->priv;
	struct mv88e6xxx_br *mvbr;
	unsigned int bit;

	list_for_each_entry(mvbr, &mvdst->bridges, list) {
		if (mvbr->brdev == brdev)
			return 0;
	}

	bit = find_first_zero_bit(mvdst->busy_ports,
				  MV88E6XXX_MAX_PVT_ENTRIES);

	if (bit >= MV88E6XXX_MAX_PVT_ENTRIES) {
		pr_err("Unable to allocate virtual port for %s in DSA tree %d\n",
		       netdev_name(brdev), dst->index);
		return -ENOSPC;
	}

	mvbr = kzalloc(sizeof(*mvbr), GFP_KERNEL);
	if (!mvbr)
		return -ENOMEM;

	mvbr->brdev = brdev;
	mvbr->dev = DEV_FROM_BIT(bit);
	mvbr->port = PORT_FROM_BIT(bit);

	INIT_LIST_HEAD(&mvbr->list);
	list_add_tail(&mvbr->list, &mvdst->bridges);
	set_bit(bit, mvdst->busy_ports);
	return 0;
}

void mv88e6xxx_dst_bridge_leave(struct dsa_switch_tree *dst,
				struct net_device *brdev)
{
	struct mv88e6xxx_dst *mvdst = dst->priv;
	struct mv88e6xxx_br *mvbr;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dp->bridge_dev == brdev)
			return;
	}

	list_for_each_entry(mvbr, &mvdst->bridges, list) {
		if (mvbr->brdev == brdev) {
			clear_bit(DEV_PORT_TO_BIT(mvbr->dev, mvbr->port),
				  mvdst->busy_ports);
			list_del(&mvbr->list);
			kfree(mvbr);
			return;
		}
	}
}

static struct mv88e6xxx_dst *mv88e6xxx_dst_get(struct dsa_switch_tree *dst)
{
	struct mv88e6xxx_dst *mvdst;

	if (dst->priv)
		return dst->priv;

	mvdst = kzalloc(sizeof(*mvdst), GFP_KERNEL);
	if (!mvdst)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&mvdst->bridges);

	bitmap_set(mvdst->busy_ports,
		   DEV_PORT_TO_BIT(MV88E6XXX_G2_PVT_ADDR_DEV_TRUNK, 0),
		   MV88E6XXX_MAX_PVT_PORTS);

	dst->priv = mvdst;
	return mvdst;
}

int mv88e6xxx_dst_add_chip(struct mv88e6xxx_chip *chip)
{
	struct dsa_switch_tree *dst = chip->ds->dst;
	struct mv88e6xxx_dst *mvdst;

	mvdst = mv88e6xxx_dst_get(dst);
	if (IS_ERR(mvdst))
		return PTR_ERR(mvdst);

	bitmap_set(mvdst->busy_ports, DEV_PORT_TO_BIT(chip->ds->index, 0),
		   MV88E6XXX_MAX_PVT_PORTS);
	return 0;
}
