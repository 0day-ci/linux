// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mv88e6xxx_switchdev.c
 *
 *	Authors:
 *	Hans J. Schultz		<hans.schultz@westermo.com>
 *
 */

#include <net/switchdev.h>
#include "chip.h"
#include "global1.h"

struct mv88e6xxx_fid_search_ctx {
	u16 fid_search;
	u16 vid_found;
};

static int mv88e6xxx_find_vid_on_matching_fid(struct mv88e6xxx_chip *chip,
					      const struct mv88e6xxx_vtu_entry *entry,
					      void *priv)
{
	struct mv88e6xxx_fid_search_ctx *ctx = priv;

	if (ctx->fid_search == entry->fid) {
		ctx->vid_found = entry->vid;
		return 1;
	}
	return 0;
}

int mv88e6xxx_switchdev_handle_atu_miss_violation(struct mv88e6xxx_chip *chip,
						  int port,
						  struct mv88e6xxx_atu_entry *entry,
						  u16 fid)
{
	struct switchdev_notifier_fdb_info info = {
		.addr = entry->mac,
		.vid = 0,
		.added_by_user = false,
		.is_local = false,
		.offloaded = true,
		.locked = true,
	};
	struct mv88e6xxx_fid_search_ctx ctx;
	struct netlink_ext_ack *extack;
	struct net_device *brport;
	struct dsa_port *dp;
	int err;

	ctx.fid_search = fid;
	err = mv88e6xxx_vtu_walk(chip, mv88e6xxx_find_vid_on_matching_fid, &ctx);
	if (err < 0)
		return err;
	if (err == 1)
		info.vid = ctx.vid_found;
	else
		return -ENODATA;

	dp = dsa_to_port(chip->ds, port);
	brport = dsa_port_to_bridge_port(dp);
	err = call_switchdev_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE, brport, &info.info, extack);
	if (err)
		return err;
	entry->portvec = MV88E6XXX_G1_ATU_DATA_PORT_VECTOR_NO_EGRESS;
	return mv88e6xxx_g1_atu_loadpurge(chip, fid, entry);
}
