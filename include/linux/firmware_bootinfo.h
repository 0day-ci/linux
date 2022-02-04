/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2022 IBM Corp. */

#include <linux/sysfs.h>
#include <linux/init.h>

#define BOOTINFO_SET(b, n, v) do {b.n.present = true; b.n.value = v; } while (0)

/**
 * struct bootinfo_entry - A bootinfo sysfs entry
 * @present: true if the file should be present (visible) in sysfs
 * @value: value of the entry, will be printed as 1 or 0
 *
 * Contains the state of a given bootinfo sysfs file, to be filled out by the
 * platform that wishes it to be present.
 *
 * It is used by sysfs. The is_present callback tests .present indicate the
 * attribute should be shown, and by the show callback tests .value to display
 * the value.
 */
struct bootinfo_entry {
	bool present;
	bool value;
};

/**
 * struct bootinfo: A collection of bootinfo entries
 * @abr_image: sysfs property
 * @low_security_key: sysfs property
 * @otp_protected: sysfs property
 * @secure_boot: sysfs property
 *
 * The documented set of bootinfo entries to be displayed in
 * /sys/firmware/bootinfo. Platform code populates a struct bootinfo and
 * passes it to firmware_bootinfo_init, which takes a copy to be used at
 * runtime.
 *
 * See struct bootinfo_entry for a description of the information each entry
 * contiains.
 */
struct bootinfo {
	struct bootinfo_entry abr_image;
	struct bootinfo_entry low_security_key;
	struct bootinfo_entry otp_protected;
	struct bootinfo_entry secure_boot;
};

int firmware_bootinfo_init(struct bootinfo *bootinfo_init);
