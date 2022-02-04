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
static struct bootinfo bootinfo;

static ssize_t abr_image_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", bootinfo.abr_image.value);
}
static DEVICE_ATTR_RO(abr_image);

static ssize_t low_security_key_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", bootinfo.low_security_key.value);
}
static DEVICE_ATTR_RO(low_security_key);

static ssize_t otp_protected_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", bootinfo.otp_protected.value);
}
static DEVICE_ATTR_RO(otp_protected);

static ssize_t secure_boot_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", bootinfo.secure_boot.value);
}
static DEVICE_ATTR_RO(secure_boot);

#define ATTR_ENABLED(a) ((attr == &dev_attr_##a.attr) && bootinfo.a.present)

static umode_t bootinfo_attr_mode(struct kobject *kobj, struct attribute *attr, int index)
{
	if (ATTR_ENABLED(abr_image))
		return 0444;

	if (ATTR_ENABLED(otp_protected))
		return 0444;

	if (ATTR_ENABLED(low_security_key))
		return 0444;

	if (ATTR_ENABLED(otp_protected))
		return 0444;

	if (ATTR_ENABLED(low_security_key))
		return 0444;

	if (ATTR_ENABLED(secure_boot))
		return 0444;

	return 0;
}

static struct attribute *bootinfo_attrs[] = {
	&dev_attr_abr_image.attr,
	&dev_attr_low_security_key.attr,
	&dev_attr_otp_protected.attr,
	&dev_attr_secure_boot.attr,
	NULL,
};

static const struct attribute_group bootinfo_attr_group = {
	.attrs = bootinfo_attrs,
	.is_visible = bootinfo_attr_mode,
};

int __init firmware_bootinfo_init(struct bootinfo *bootinfo_init)
{
	struct kobject *kobj = kobject_create_and_add("bootinfo", firmware_kobj);
	if (!kobj)
		return -ENOMEM;

	memcpy(&bootinfo, bootinfo_init, sizeof(bootinfo));

	return sysfs_create_group(kobj, &bootinfo_attr_group);
}
EXPORT_SYMBOL_GPL(firmware_bootinfo_init);
