// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_device.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/printk.h>
#include <linux/sysfs.h>

#include "i915_drv.h"
#include "i915_sysfs.h"
#include "intel_gt.h"
#include "intel_gt_sysfs.h"
#include "intel_gt_types.h"
#include "intel_rc6.h"

bool is_object_gt(struct kobject *kobj)
{
	return !strncmp(kobj->name, "gt", 2);
}

static struct intel_gt *kobj_to_gt(struct kobject *kobj)
{
	return container_of(kobj, struct kobj_gt, base)->gt;
}

struct intel_gt *intel_gt_sysfs_get_drvdata(struct device *dev,
					    const char *name)
{
	struct kobject *kobj = &dev->kobj;

	/*
	 * We are interested at knowing from where the interface
	 * has been called, whether it's called from gt/ or from
	 * the parent directory.
	 * From the interface position it depends also the value of
	 * the private data.
	 * If the interface is called from gt/ then private data is
	 * of the "struct intel_gt *" type, otherwise it's * a
	 * "struct drm_i915_private *" type.
	 */
	if (!is_object_gt(kobj)) {
		struct drm_i915_private *i915 = kdev_minor_to_i915(dev);

		pr_devel_ratelimited(DEPRECATED
			"%s (pid %d) is accessing deprecated %s "
			"sysfs control, please use gt/gt<n>/%s instead\n",
			current->comm, task_pid_nr(current), name, name);
		return to_gt(i915);
	}

	return kobj_to_gt(kobj);
}

static ssize_t id_show(struct device *dev,
		       struct device_attribute *attr,
		       char *buf)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);

	return sysfs_emit(buf, "%u\n", gt->info.id);
}
static DEVICE_ATTR_RO(id);

static struct attribute *id_attrs[] = {
	&dev_attr_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(id);

static void kobj_gt_release(struct kobject *kobj)
{
	kfree(kobj);
}

static struct kobj_type kobj_gt_type = {
	.release = kobj_gt_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = id_groups,
};

struct kobject *
intel_gt_create_kobj(struct intel_gt *gt, struct kobject *dir, const char *name)
{
	struct kobj_gt *kg;

	kg = kzalloc(sizeof(*kg), GFP_KERNEL);
	if (!kg)
		return NULL;

	kobject_init(&kg->base, &kobj_gt_type);
	kg->gt = gt;

	/* xfer ownership to sysfs tree */
	if (kobject_add(&kg->base, dir, "%s", name)) {
		kobject_put(&kg->base);
		return NULL;
	}

	return &kg->base; /* borrowed ref */
}

void intel_gt_sysfs_register(struct intel_gt *gt)
{
	struct kobject *dir;
	char name[80];

	snprintf(name, sizeof(name), "gt%d", gt->info.id);

	dir = intel_gt_create_kobj(gt, gt->i915->sysfs_gt, name);
	if (!dir) {
		drm_warn(&gt->i915->drm,
			 "failed to initialize %s sysfs root\n", name);
		return;
	}
}
