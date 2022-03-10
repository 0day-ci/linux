/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * mv88e6xxx_switchdev.h
 *
 *	Authors:
 *	Hans J. Schultz		<hans.schultz@westermo.com>
 *
 */

#ifndef DRIVERS_NET_DSA_MV88E6XXX_MV88E6XXX_SWITCHDEV_H_
#define DRIVERS_NET_DSA_MV88E6XXX_MV88E6XXX_SWITCHDEV_H_

#include <net/switchdev.h>

int mv88e6xxx_switchdev_handle_atu_miss_violation(struct mv88e6xxx_chip *chip,
						  int port,
						  struct mv88e6xxx_atu_entry *entry,
						  u16 fid);

#endif /* DRIVERS_NET_DSA_MV88E6XXX_MV88E6XXX_SWITCHDEV_H_ */
