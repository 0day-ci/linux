// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_device.h>
#include <linux/sysfs.h>
#include <linux/printk.h>

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_rc6.h"
#include "intel_rps.h"
#include "sysfs_gt.h"
#include "sysfs_gt_pm.h"

#ifdef CONFIG_PM
static u32 get_residency(struct intel_gt *gt, i915_reg_t reg)
{
	intel_wakeref_t wakeref;
	u64 res = 0;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		res = intel_rc6_residency_us(&gt->rc6, reg);

	return DIV_ROUND_CLOSEST_ULL(res, 1000);
}

static ssize_t rc6_enable_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	u8 mask = 0;

	if (HAS_RC6(gt->i915))
		mask |= BIT(0);
	if (HAS_RC6p(gt->i915))
		mask |= BIT(1);
	if (HAS_RC6pp(gt->i915))
		mask |= BIT(2);

	return scnprintf(buff, PAGE_SIZE, "%x\n", mask);
}

static ssize_t rc6_residency_ms_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	u32 rc6_residency = get_residency(gt, GEN6_GT_GFX_RC6);

	return scnprintf(buff, PAGE_SIZE, "%u\n", rc6_residency);
}

static ssize_t rc6p_residency_ms_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	u32 rc6p_residency = get_residency(gt, GEN6_GT_GFX_RC6p);

	return scnprintf(buff, PAGE_SIZE, "%u\n", rc6p_residency);
}

static ssize_t rc6pp_residency_ms_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	u32 rc6pp_residency = get_residency(gt, GEN6_GT_GFX_RC6pp);

	return scnprintf(buff, PAGE_SIZE, "%u\n", rc6pp_residency);
}

static ssize_t media_rc6_residency_ms_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	u32 rc6_residency = get_residency(gt, VLV_GT_MEDIA_RC6);

	return scnprintf(buff, PAGE_SIZE, "%u\n", rc6_residency);
}

static DEVICE_ATTR_RO(rc6_enable);
static DEVICE_ATTR_RO(rc6_residency_ms);
static DEVICE_ATTR_RO(rc6p_residency_ms);
static DEVICE_ATTR_RO(rc6pp_residency_ms);
static DEVICE_ATTR_RO(media_rc6_residency_ms);

static struct attribute *rc6_attrs[] = {
	&dev_attr_rc6_enable.attr,
	&dev_attr_rc6_residency_ms.attr,
	NULL
};

static struct attribute *rc6p_attrs[] = {
	&dev_attr_rc6p_residency_ms.attr,
	&dev_attr_rc6pp_residency_ms.attr,
	NULL
};

static struct attribute *media_rc6_attrs[] = {
	&dev_attr_media_rc6_residency_ms.attr,
	NULL
};

static const struct attribute_group rc6_attr_group[] = {
	{ .name = power_group_name, .attrs = rc6_attrs },
	{ .attrs = rc6_attrs }
};

static const struct attribute_group rc6p_attr_group[] = {
	{ .name = power_group_name, .attrs = rc6p_attrs },
	{ .attrs = rc6p_attrs }
};

static const struct attribute_group media_rc6_attr_group[] = {
	{ .name = power_group_name, .attrs = media_rc6_attrs },
	{ .attrs = media_rc6_attrs }
};

static int __intel_gt_sysfs_create_group(struct kobject *kobj,
					 const struct attribute_group *grp)
{
	int i = is_object_gt(kobj);

	return i ? sysfs_create_group(kobj, &grp[i]) :
		   sysfs_merge_group(kobj, &grp[i]);
}

static void intel_sysfs_rc6_init(struct intel_gt *gt, struct kobject *kobj)
{
	int ret;

	if (!HAS_RC6(gt->i915))
		return;

	ret = __intel_gt_sysfs_create_group(kobj, rc6_attr_group);
	if (ret)
		drm_err(&gt->i915->drm,
			"failed to create gt%u RC6 sysfs files\n", gt->info.id);

	if (HAS_RC6p(gt->i915)) {
		ret = __intel_gt_sysfs_create_group(kobj, rc6p_attr_group);
		if (ret)
			drm_err(&gt->i915->drm,
				"failed to create gt%u RC6p sysfs files\n",
				gt->info.id);
	}

	if (IS_VALLEYVIEW(gt->i915) || IS_CHERRYVIEW(gt->i915)) {
		ret = __intel_gt_sysfs_create_group(kobj, media_rc6_attr_group);
		if (ret)
			drm_err(&gt->i915->drm,
				"failed to create media %u RC6 sysfs files\n",
				gt->info.id);
	}
}
#else
static void intel_sysfs_rc6_init(struct intel_gt *gt, struct kobject *kobj)
{
}
#endif /* CONFIG_PM */

static ssize_t act_freq_mhz_show(struct device *dev,
				 struct device_attribute *attr, char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);

	return scnprintf(buff, PAGE_SIZE, "%d\n",
			intel_rps_read_actual_frequency(&gt->rps));
}

static ssize_t cur_freq_mhz_show(struct device *dev,
				 struct device_attribute *attr, char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;

	return scnprintf(buff, PAGE_SIZE, "%d\n",
			 intel_rps_get_requested_frequency(rps));
}

static ssize_t boost_freq_mhz_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;

	return scnprintf(buff, PAGE_SIZE, "%d\n",
			 intel_rps_get_boost_frequency(rps));
}

static ssize_t boost_freq_mhz_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buff, size_t count)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;
	bool boost = false;
	ssize_t ret;
	u32 val;

	ret = kstrtou32(buff, 0, &val);
	if (ret)
		return ret;

	/* Validate against (static) hardware limits */
	val = intel_freq_opcode(rps, val);
	if (val < rps->min_freq || val > rps->max_freq)
		return -EINVAL;

	mutex_lock(&rps->lock);
	if (val != rps->boost_freq) {
		rps->boost_freq = val;
		boost = atomic_read(&rps->num_waiters);
	}
	mutex_unlock(&rps->lock);
	if (boost)
		schedule_work(&rps->work);

	return count;
}

static ssize_t vlv_rpe_freq_mhz_show(struct device *dev,
				     struct device_attribute *attr, char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;

	return scnprintf(buff, PAGE_SIZE, "%d\n",
			intel_gpu_freq(rps, rps->efficient_freq));
}

static ssize_t max_freq_mhz_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;

	return sysfs_emit(buff, "%d\n", intel_rps_get_max_frequency(rps));
}

static ssize_t max_freq_mhz_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buff, size_t count)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;
	ssize_t ret;
	u32 val;

	ret = kstrtou32(buff, 0, &val);
	if (ret)
		return ret;

	ret = intel_rps_set_max_frequency(rps, val);

	return ret ?: count;
}

static ssize_t min_freq_mhz_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;

	return sysfs_emit(buff, "%d\n", intel_rps_get_min_frequency(rps));
}

static ssize_t min_freq_mhz_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buff, size_t count)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;
	ssize_t ret;
	u32 val;

	ret = kstrtou32(buff, 0, &val);
	if (ret)
		return ret;

	ret = intel_rps_set_min_frequency(rps, val);

	return ret ?: count;
}

#define INTEL_GT_RPS_SYSFS_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_gt_##_name = __ATTR(gt_##_name, _mode, _show, _store); \
	struct device_attribute dev_attr_rps_##_name = __ATTR(rps_##_name, _mode, _show, _store)

#define INTEL_GT_RPS_SYSFS_ATTR_RO(_name)				\
		INTEL_GT_RPS_SYSFS_ATTR(_name, 0444, _name##_show, NULL)
#define INTEL_GT_RPS_SYSFS_ATTR_RW(_name)				\
		INTEL_GT_RPS_SYSFS_ATTR(_name, 0644, _name##_show, _name##_store)

static INTEL_GT_RPS_SYSFS_ATTR_RO(act_freq_mhz);
static INTEL_GT_RPS_SYSFS_ATTR_RO(cur_freq_mhz);
static INTEL_GT_RPS_SYSFS_ATTR_RW(boost_freq_mhz);
static INTEL_GT_RPS_SYSFS_ATTR_RW(max_freq_mhz);
static INTEL_GT_RPS_SYSFS_ATTR_RW(min_freq_mhz);

static DEVICE_ATTR_RO(vlv_rpe_freq_mhz);

static ssize_t rps_rp_mhz_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buff);

static INTEL_GT_RPS_SYSFS_ATTR(RP0_freq_mhz, 0444, rps_rp_mhz_show, NULL);
static INTEL_GT_RPS_SYSFS_ATTR(RP1_freq_mhz, 0444, rps_rp_mhz_show, NULL);
static INTEL_GT_RPS_SYSFS_ATTR(RPn_freq_mhz, 0444, rps_rp_mhz_show, NULL);

#define GEN6_ATTR(s) { \
		&dev_attr_##s##_act_freq_mhz.attr, \
		&dev_attr_##s##_cur_freq_mhz.attr, \
		&dev_attr_##s##_boost_freq_mhz.attr, \
		&dev_attr_##s##_max_freq_mhz.attr, \
		&dev_attr_##s##_min_freq_mhz.attr, \
		&dev_attr_##s##_RP0_freq_mhz.attr, \
		&dev_attr_##s##_RP1_freq_mhz.attr, \
		&dev_attr_##s##_RPn_freq_mhz.attr, \
		NULL, \
	}

#define GEN6_RPS_ATTR GEN6_ATTR(rps)
#define GEN6_GT_ATTR  GEN6_ATTR(gt)

/* For now we have a static number of RP states */
static ssize_t rps_rp_mhz_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buff)
{
	struct intel_gt *gt = intel_gt_sysfs_get_drvdata(dev, attr->attr.name);
	struct intel_rps *rps = &gt->rps;
	u32 val;

	if (attr == &dev_attr_gt_RP0_freq_mhz ||
	    attr == &dev_attr_rps_RP0_freq_mhz) {
		val = intel_rps_get_rp0_frequency(rps);
	} else if (attr == &dev_attr_gt_RP1_freq_mhz ||
		   attr == &dev_attr_rps_RP1_freq_mhz) {
		   val = intel_rps_get_rp1_frequency(rps);
	} else if (attr == &dev_attr_gt_RPn_freq_mhz ||
		   attr == &dev_attr_rps_RPn_freq_mhz) {
		   val = intel_rps_get_rpn_frequency(rps);
	} else {
		GEM_WARN_ON(1);
		return -ENODEV;
	}

	return scnprintf(buff, PAGE_SIZE, "%d\n", val);
}

static const struct attribute * const gen6_rps_attrs[] = GEN6_RPS_ATTR;
static const struct attribute * const gen6_gt_attrs[]  = GEN6_GT_ATTR;

static int intel_sysfs_rps_init(struct intel_gt *gt, struct kobject *kobj,
				const struct attribute * const *attrs)
{
	int ret;

	if (GRAPHICS_VER(gt->i915) < 6)
		return 0;

	ret = sysfs_create_files(kobj, attrs);
	if (ret)
		return ret;

	if (IS_VALLEYVIEW(gt->i915) || IS_CHERRYVIEW(gt->i915))
		ret = sysfs_create_file(kobj, &dev_attr_vlv_rpe_freq_mhz.attr);

	return ret;
}

void intel_gt_sysfs_pm_init(struct intel_gt *gt, struct kobject *kobj)
{
	int ret;

	intel_sysfs_rc6_init(gt, kobj);

	ret = is_object_gt(kobj) ?
	      intel_sysfs_rps_init(gt, kobj, gen6_rps_attrs) :
	      intel_sysfs_rps_init(gt, kobj, gen6_gt_attrs);
	if (ret)
		drm_err(&gt->i915->drm,
			"failed to create gt%u RPS sysfs files", gt->info.id);
}
