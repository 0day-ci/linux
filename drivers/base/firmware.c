// SPDX-License-Identifier: GPL-2.0
/*
 * firmware.c - firmware subsystem hoohaw.
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * Copyright (c) 2007 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (c) 2007 Novell Inc.
 */
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/firmware_bootinfo.h>

#include "base.h"

struct kobject *firmware_kobj;
EXPORT_SYMBOL_GPL(firmware_kobj);

int __init firmware_init(void)
{
	firmware_kobj = kobject_create_and_add("firmware", NULL);
	if (!firmware_kobj)
		return -ENOMEM;
	return 0;
}

/*
 * Exposes attributes documented in Documentation/ABI/testing/sysfs-firmware-bootinfo
 */
int __init firmware_bootinfo_init(const struct attribute_group *attr_group)
{
	struct kobject *kobj= kobject_create_and_add("bootinfo", firmware_kobj);
	if (!kobj)
		return -ENOMEM;

	return sysfs_create_group(kobj, attr_group);
}
EXPORT_SYMBOL_GPL(firmware_bootinfo_init);
