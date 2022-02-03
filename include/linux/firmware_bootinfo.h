/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2022 IBM Corp. */

#include <linux/sysfs.h>
#include <linux/init.h>

#define BOOTINFO_SET(b, n, v) b.n.en = true; b.n.val = v

struct bootinfo_entry {
	bool en;
	bool val;
};

struct bootinfo {
	struct bootinfo_entry abr_image;
	struct bootinfo_entry low_security_key;
	struct bootinfo_entry otp_protected;
	struct bootinfo_entry secure_boot;
	struct bootinfo_entry uart_boot;
};

int __init firmware_bootinfo_init(struct bootinfo *bootinfo_init);
