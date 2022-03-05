// SPDX-License-Identifier: GPL-2.0-only
/*
 *	backpack.c (c) 2001 Micro Solutions Inc.
 *		Released under the terms of the GNU General Public license
 *
 *	backpack.c is a low-level protocol driver for the Micro Solutions
 *		"BACKPACK" parallel port IDE adapter
 *		(Works on Series 6 drives)
 *
 *	Written by: Ken Hahn     (linux-dev@micro-solutions.com)
 *		    Clive Turvey (linux-dev@micro-solutions.com)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/parport.h>

#include "ppc6lnx.c"
#include "pata_parport.h"

#define PPCSTRUCT(pi) ((struct ppc_storage *)(pi->private))

#define ATAPI_DATA       0      /* data port */

static int bpck6_read_regr(struct pi_adapter *pi, int cont, int reg)
{
	unsigned int out;

	/* check for bad settings */
	if (reg < 0 || reg > 7 || cont < 0 || cont > 2)
		return -1;
	out = ppc6_rd_port(PPCSTRUCT(pi), cont ? reg | 8 : reg);
	return out;
}

static void bpck6_write_regr(struct pi_adapter *pi, int cont, int reg, int val)
{
	/* check for bad settings */
	if (reg >= 0 && reg <= 7 && cont >= 0 && cont <= 1)
		ppc6_wr_port(PPCSTRUCT(pi), cont ? reg | 8 : reg, (u8)val);
}

static void bpck6_write_block(struct pi_adapter *pi, char *buf, int len)
{
	ppc6_wr_port16_blk(PPCSTRUCT(pi), ATAPI_DATA, buf, (u32)len >> 1);
}

static void bpck6_read_block(struct pi_adapter *pi, char *buf, int len)
{
	ppc6_rd_port16_blk(PPCSTRUCT(pi), ATAPI_DATA, buf, (u32) len >> 1);
}

static void bpck6_connect(struct pi_adapter *pi)
{
	if (pi->mode >= 2)
		PPCSTRUCT(pi)->mode = 4 + pi->mode - 2;
	else if (pi->mode == 1)
		PPCSTRUCT(pi)->mode = 3;
	else
		PPCSTRUCT(pi)->mode = 1;

	ppc6_open(PPCSTRUCT(pi));
	ppc6_wr_extout(PPCSTRUCT(pi), 0x3);
}

static void bpck6_disconnect(struct pi_adapter *pi)
{
	ppc6_wr_extout(PPCSTRUCT(pi), 0x0);
	ppc6_close(PPCSTRUCT(pi));
}

static int bpck6_test_port(struct pi_adapter *pi)   /* check for 8-bit port */
{
	/* copy over duplicate stuff.. initialize state info */
	PPCSTRUCT(pi)->ppc_id = pi->unit;
	PPCSTRUCT(pi)->lpt_addr = pi->port;

	/* look at the parport device to see if what modes we can use */
	if (((struct pardevice *)(pi->pardev))->port->modes &
	    (PARPORT_MODE_EPP))
		return 5; /* Can do EPP*/
	else if (((struct pardevice *)(pi->pardev))->port->modes &
		 (PARPORT_MODE_TRISTATE))
		return 2;
	else /* Just flat SPP */
		return 1;
}

static int bpck6_probe_unit(struct pi_adapter *pi)
{
	int out;

	/* SET PPC UNIT NUMBER */
	PPCSTRUCT(pi)->ppc_id = pi->unit;

	/* LOWER DOWN TO UNIDIRECTIONAL */
	PPCSTRUCT(pi)->mode = 1;

	out = ppc6_open(PPCSTRUCT(pi));

	if (out) {
		ppc6_close(PPCSTRUCT(pi));
		return 1;
	}

	return 0;
}

static void bpck6_log_adapter(struct pi_adapter *pi, char *scratch, int verbose)
{
	static char * const mode_string[] = {
		"4-bit", "8-bit", "EPP-8", "EPP-16", "EPP-32" };

	dev_info(&pi->dev, "bpck6, Micro Solutions BACKPACK Drive at 0x%x\n",
		pi->port);
	dev_info(&pi->dev, "Unit: %d Mode:%d (%s) Delay %d\n",
		pi->unit, pi->mode, mode_string[pi->mode], pi->delay);
}

static int bpck6_init_proto(struct pi_adapter *pi)
{
	struct ppc_storage *p = kzalloc(sizeof(struct ppc_storage), GFP_KERNEL);

	if (p) {
		pi->private = (unsigned long)p;
		return 0;
	}

	return -ENOMEM;
}

static void bpck6_release_proto(struct pi_adapter *pi)
{
	kfree((void *)(pi->private));
}

static struct pi_protocol bpck6 = {
	.owner		= THIS_MODULE,
	.name		= "bpck6",
	.max_mode	= 5,
	.epp_first	= 2, /* 2-5 use epp (need 8 ports) */
	.max_units	= 255,
	.write_regr	= bpck6_write_regr,
	.read_regr	= bpck6_read_regr,
	.write_block	= bpck6_write_block,
	.read_block	= bpck6_read_block,
	.connect	= bpck6_connect,
	.disconnect	= bpck6_disconnect,
	.test_port	= bpck6_test_port,
	.probe_unit	= bpck6_probe_unit,
	.log_adapter	= bpck6_log_adapter,
	.init_proto	= bpck6_init_proto,
	.release_proto	= bpck6_release_proto,
	.sht		= { PATA_PARPORT_SHT("pata_parport-bpck6") },
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Micro Solutions Inc.");
MODULE_DESCRIPTION("BACKPACK Protocol module, compatible with PARIDE");
module_pata_parport_driver(bpck6);
