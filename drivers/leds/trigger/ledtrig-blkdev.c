// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED trigger - modular components
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/leds.h>
#include <linux/module.h>

#include "ledtrig-blkdev.h"

MODULE_DESCRIPTION("Block device LED trigger");
MODULE_AUTHOR("Ian Pilcher <arequipeno@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(LEDTRIG_BLKDEV);

/* Default blink time & check interval (milliseconds) */
#define LEDTRIG_BLKDEV_BLINK_MSEC	75
#define LEDTRIG_BLKDEV_INTERVAL		100

/* Minimum blink time & check interval (milliseconds) */
#define LEDTRIG_BLKDEV_MIN_BLINK	10
#define LEDTRIG_BLKDEV_MIN_INT		25

enum ledtrig_blkdev_mode {
	LEDTRIG_BLKDEV_MODE_RO	= 0,	/* blink for reads */
	LEDTRIG_BLKDEV_MODE_WO	= 1,	/* blink for writes */
	LEDTRIG_BLKDEV_MODE_RW	= 2	/* blink for reads and writes */
};

/* Per-block device information */
struct ledtrig_blkdev_disk {
	struct gendisk		*gd;
	struct kobject		*dir;		/* linked_leds dir */
	struct hlist_head	leds;
	unsigned long		read_ios;
	unsigned long		write_ios;
	unsigned int		generation;
	bool			read_act;
	bool			write_act;
};

/* For many-to-many relationships between "disks" (block devices) and LEDs */
struct ledtrig_blkdev_link {
	struct hlist_node		disk_leds_node;
	struct hlist_node		led_disks_node;
	struct ledtrig_blkdev_disk	*disk;
	struct ledtrig_blkdev_led	*led;
};

/* Every LED associated with the blkdev trigger gets one of these */
struct ledtrig_blkdev_led {
	struct kobject			*dir;		/* linked_devices dir */
	struct led_classdev		*led_dev;
	unsigned int			blink_msec;
	struct hlist_head		disks;		/* linked block devs */
	struct hlist_node		leds_node;
	enum ledtrig_blkdev_mode	mode;
};
