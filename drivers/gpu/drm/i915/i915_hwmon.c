// SPDX-License-Identifier: MIT

/*
 * Copyright © 2020 Intel Corporation
 */

/*
 * Power-related hwmon entries.
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include "i915_drv.h"
#include "gt/intel_gt.h"
#include "i915_hwmon.h"

/**
 * SF_* - scale factors for particular quantities.
 * The hwmon standard says that quantities of the given types are specified
 * in the given units:
 * - time   - milliseconds
 * - power  - microwatts
 * - energy - microjoules
 */

#define SF_TIME		   1000
#define SF_POWER	1000000
#define SF_ENERGY	1000000

static void
_locked_with_pm_intel_uncore_rmw(struct intel_uncore *uncore,
				 i915_reg_t reg, u32 clear, u32 set)
{
	struct drm_i915_private *i915 = uncore->i915;
	struct i915_hwmon *hwmon = &i915->hwmon;
	intel_wakeref_t wakeref;

	mutex_lock(&hwmon->hwmon_lock);

	with_intel_runtime_pm(uncore->rpm, wakeref)
		intel_uncore_rmw(uncore, reg, clear, set);

	mutex_unlock(&hwmon->hwmon_lock);
}

/*
 * _field_read_and_scale()
 * Return type of u64 allows for the case where the scaling might cause a
 * result exceeding 32 bits.
 */
static __always_inline u64
_field_read_and_scale(struct intel_uncore *uncore, i915_reg_t rgadr,
		      u32 field_mask, int nshift, unsigned int scale_factor)
{
	intel_wakeref_t wakeref;
	u32 reg_value;
	u64 scaled_val;

	with_intel_runtime_pm(uncore->rpm, wakeref)
		reg_value = intel_uncore_read(uncore, rgadr);

	reg_value = le32_get_bits(reg_value, field_mask);
	scaled_val = mul_u32_u32(scale_factor, reg_value);

	/* Shift, rounding to nearest */
	if (nshift > 0)
		scaled_val = (scaled_val + (1 << (nshift - 1))) >> nshift;

	return scaled_val;
}

/*
 * _field_read64_and_scale() - read a 64-bit register and scale.
 */
static __always_inline u64
_field_read64_and_scale(struct intel_uncore *uncore, i915_reg_t rgadr,
			u64 field_mask, int nshift, unsigned int scale_factor)
{
	intel_wakeref_t wakeref;
	u64 reg_value;
	u64 scaled_val;

	with_intel_runtime_pm(uncore->rpm, wakeref)
		reg_value = intel_uncore_read64(uncore, rgadr);

	reg_value = le64_get_bits(reg_value, field_mask);
	scaled_val = scale_factor * reg_value;

	/* Shift, rounding to nearest */
	if (nshift > 0)
		scaled_val = (scaled_val + (1 << (nshift - 1))) >> nshift;

	return scaled_val;
}

/*
 * _field_scale_and_write()
 */
static __always_inline void
_field_scale_and_write(struct intel_uncore *uncore,
		       i915_reg_t rgadr,
		       u32 field_mask, int nshift,
		       unsigned int scale_factor, long lval)
{
	u32 nval;
	u32 bits_to_clear;
	u32 bits_to_set;

	/* Computation in 64-bits to avoid overflow. Round to nearest. */
	nval = DIV_ROUND_CLOSEST_ULL((u64)lval << nshift, scale_factor);

	bits_to_clear = field_mask;
	bits_to_set = le32_encode_bits(nval, field_mask);

	_locked_with_pm_intel_uncore_rmw(uncore, rgadr,
					 bits_to_clear, bits_to_set);
}

/*
 * i915_energy1_input_show - A custom function to obtain energy1_input.
 * Use a custom function instead of the usual hwmon helpers in order to
 * guarantee 64-bits of result to user-space.
 * Units are microjoules.
 *
 * The underlying hardware register is 32-bits and is subject to overflow.
 * This function compensates for overflow of the 32-bit register by detecting
 * wrap-around and incrementing an overflow counter.
 * This only works if the register is sampled often enough to avoid
 * missing an instance of overflow - achieved either by repeated
 * queries through the API, or via a possible timer (future - TBD) that
 * ensures values are read often enough to catch all overflows.
 *
 * How long before overflow?  For example, with an example scaling bit
 * shift of 14 bits (see register *PACKAGE_POWER_SKU_UNIT) and a power draw of
 * 1000 watts, the 32-bit counter will overflow in approximately 4.36 minutes.
 *
 * Examples:
 *    1 watt:  (2^32 >> 14) /    1 W / (60 * 60 * 24) secs/day -> 3 days
 * 1000 watts: (2^32 >> 14) / 1000 W / 60             secs/min -> 4.36 minutes
 */
static ssize_t
i915_energy1_input_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	int nshift = hwmon->scl_shift_energy;
	ssize_t ret;
	intel_wakeref_t wakeref;
	u32 reg_value;
	u64 vlo;
	u64 vhi;

	mutex_lock(&hwmon->hwmon_lock);

	with_intel_runtime_pm(uncore->rpm, wakeref)
		reg_value = intel_uncore_read(uncore,
					      hwmon->rg.reg_energy_status);

	/*
	 * The u32 register concatenated with the u32 overflow counter
	 * gives an effective energy counter size of 64-bits.  However, the
	 * computations below are done modulo 2^96 to avoid overflow during
	 * scaling in the conversion to microjoules.
	 *
	 * The low-order 64-bits of the resulting quantity are returned to
	 * the caller in units of microjoules, encoded into a decimal string.
	 *
	 * For a power of 1000 watts, 64 bits in units of microjoules will
	 * overflow after 584 years.
	 */

	if (hwmon->energy_counter_prev > reg_value)
		hwmon->energy_counter_overflow++;

	hwmon->energy_counter_prev = reg_value;

	/*
	 * 64-bit variables vlo and vhi are used for the scaling process.
	 * The 96-bit counter value is composed from the two 64-bit variables
	 * vhi and vlo thusly:  counter == vhi << 32 + vlo .
	 * The 32-bits of overlap between the two variables is convenient for
	 * handling overflows out of vlo.
	 */

	vlo = reg_value;
	vhi = hwmon->energy_counter_overflow;

	mutex_unlock(&hwmon->hwmon_lock);

	vlo = SF_ENERGY * vlo;

	/* Prepare to round to nearest */
	if (nshift > 0)
		vlo += 1 << (nshift - 1);

	/*
	 * Anything in the upper-32 bits of vlo gets added into vhi here,
	 * and then cleared from vlo.
	 */
	vhi = (SF_ENERGY * vhi) + (vlo >> 32);
	vlo &= 0xffffffffULL;

	/*
	 * Apply the right shift.
	 * - vlo shifted by itself.
	 * - vlo receiving what's shifted out of vhi.
	 * - vhi shifted by itself
	 */
	vlo = vlo >> nshift;
	vlo |= (vhi << (32 - nshift)) & 0xffffffffULL;
	vhi = vhi >> nshift;

	/* Combined to get a 64-bit result in vlo. */
	vlo |= (vhi << 32);

	ret = scnprintf(buf, PAGE_SIZE, "%llu\n", vlo);

	return ret;
}

static ssize_t
i915_power1_max_enable_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	intel_wakeref_t wakeref;
	ssize_t ret;
	u32 reg_value;
	bool is_enabled;

	with_intel_runtime_pm(uncore->rpm, wakeref)
		reg_value = intel_uncore_read(uncore,
					      i915->hwmon.rg.pkg_rapl_limit);

	is_enabled = !!(reg_value & PKG_PWR_LIM_1_EN);

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", is_enabled);

	return ret;
}

static ssize_t
i915_power1_max_enable_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	ssize_t ret;
	u32 val;
	u32 bits_to_clear;
	u32 bits_to_set;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;

	bits_to_clear = PKG_PWR_LIM_1_EN;
	if (!val)
		bits_to_set = 0;
	else
		bits_to_set = PKG_PWR_LIM_1_EN;

	_locked_with_pm_intel_uncore_rmw(uncore, hwmon->rg.pkg_rapl_limit,
					 bits_to_clear, bits_to_set);

	return count;
}

static ssize_t
i915_power1_max_interval_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	ssize_t ret;
	u64 ullval;

	ullval = _field_read_and_scale(uncore, hwmon->rg.pkg_rapl_limit,
				       PKG_PWR_LIM_1_TIME,
				       hwmon->scl_shift_time, SF_TIME);

	ret = scnprintf(buf, PAGE_SIZE, "%llu\n", ullval);

	return ret;
}

static ssize_t
i915_power1_max_interval_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	ssize_t ret;
	long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	_field_scale_and_write(uncore, hwmon->rg.pkg_rapl_limit,
			       PKG_PWR_LIM_2_TIME,
			       hwmon->scl_shift_time, SF_TIME, val);

	return count;
}

static ssize_t
i915_power1_cap_enable_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	intel_wakeref_t wakeref;
	ssize_t ret;
	u32 reg_value;
	bool is_enabled;

	with_intel_runtime_pm(uncore->rpm, wakeref)
		reg_value = intel_uncore_read(uncore,
					      hwmon->rg.pkg_rapl_limit_udw);

	is_enabled = !!(reg_value & PKG_PWR_LIM_2_EN);

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", is_enabled);

	return ret;
}

static ssize_t
i915_power1_cap_enable_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	ssize_t ret;
	u32 val;
	u32 bits_to_clear;
	u32 bits_to_set;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;

	bits_to_clear = PKG_PWR_LIM_2_EN;
	if (!val)
		bits_to_set = 0;
	else
		bits_to_set = PKG_PWR_LIM_2_EN;

	_locked_with_pm_intel_uncore_rmw(uncore, hwmon->rg.pkg_rapl_limit_udw,
					 bits_to_clear, bits_to_set);

	return count;
}

static ssize_t
i915_power_default_limit_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct i915_hwmon *hwmon = &i915->hwmon;
	ssize_t ret;

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", hwmon->power_max_initial_value);

	return ret;
}

static ssize_t
i915_power_min_limit_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	ssize_t ret;
	u32 uval;

	/*
	 * This is a 64-bit register but the individual fields are under 32 bits
	 * in size even after scaling.
	 * The UAPI specifies a size of 32 bits.
	 * The UAPI specifies that 0 should be returned if unsupported.
	 * So, using u32 and %u is sufficient.
	 */
	if (i915_mmio_reg_valid(hwmon->rg.pkg_power_sku))
		uval = (u32)_field_read64_and_scale(uncore,
						    hwmon->rg.pkg_power_sku,
						    PKG_MIN_PWR,
						    hwmon->scl_shift_power,
						    SF_POWER);
	else
		uval = 0;

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", uval);

	return ret;
}

static ssize_t
i915_power_max_limit_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	ssize_t ret;
	u32 uval;

	/*
	 * This is a 64-bit register but the individual fields are under 32 bits
	 * in size even after scaling.
	 * The UAPI specifies a size of 32 bits.
	 * The UAPI specifies that UINT_MAX should be returned if unsupported.
	 * So, using u32 and %u is sufficient.
	 */
	if (i915_mmio_reg_valid(hwmon->rg.pkg_power_sku))
		uval = (u32)_field_read64_and_scale(uncore,
						    hwmon->rg.pkg_power_sku,
						    PKG_MAX_PWR,
						    hwmon->scl_shift_power,
						    SF_POWER);
	else
		uval = UINT_MAX;

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", uval);

	return ret;
}

static SENSOR_DEVICE_ATTR(power1_max_enable, 0664,
			  i915_power1_max_enable_show,
			  i915_power1_max_enable_store, 0);
static SENSOR_DEVICE_ATTR(power1_max_interval, 0664,
			  i915_power1_max_interval_show,
			  i915_power1_max_interval_store, 0);
static SENSOR_DEVICE_ATTR(power1_cap_enable, 0664,
			  i915_power1_cap_enable_show,
			  i915_power1_cap_enable_store, 0);
static SENSOR_DEVICE_ATTR(power_default_limit, 0444,
			  i915_power_default_limit_show, NULL, 0);
static SENSOR_DEVICE_ATTR(power_min_limit, 0444,
			  i915_power_min_limit_show, NULL, 0);
static SENSOR_DEVICE_ATTR(power_max_limit, 0444,
			  i915_power_max_limit_show, NULL, 0);
static SENSOR_DEVICE_ATTR(energy1_input, 0444,
			  i915_energy1_input_show, NULL, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_power1_max_enable.dev_attr.attr,
	&sensor_dev_attr_power1_max_interval.dev_attr.attr,
	&sensor_dev_attr_power1_cap_enable.dev_attr.attr,
	&sensor_dev_attr_power_default_limit.dev_attr.attr,
	&sensor_dev_attr_power_min_limit.dev_attr.attr,
	&sensor_dev_attr_power_max_limit.dev_attr.attr,
	&sensor_dev_attr_energy1_input.dev_attr.attr,
	NULL
};

static umode_t hwmon_attributes_visible(struct kobject *kobj,
					struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct drm_i915_private *i915 = dev_get_drvdata(dev);
	struct i915_hwmon *hwmon = &i915->hwmon;
	i915_reg_t rgadr;

	if (attr == &sensor_dev_attr_energy1_input.dev_attr.attr)
		rgadr = hwmon->rg.reg_energy_status;
	else if (attr == &sensor_dev_attr_power1_max_enable.dev_attr.attr)
		rgadr = hwmon->rg.pkg_rapl_limit;
	else if (attr == &sensor_dev_attr_power1_max_interval.dev_attr.attr)
		rgadr = hwmon->rg.pkg_rapl_limit;
	else if (attr == &sensor_dev_attr_power1_cap_enable.dev_attr.attr)
		rgadr = hwmon->rg.pkg_rapl_limit_udw;
	else if (attr == &sensor_dev_attr_power_default_limit.dev_attr.attr)
		rgadr = hwmon->rg.pkg_rapl_limit;
	else if (attr == &sensor_dev_attr_power_min_limit.dev_attr.attr)
		return attr->mode;
	else if (attr == &sensor_dev_attr_power_max_limit.dev_attr.attr)
		return attr->mode;
	else
		return 0;

	if (!i915_mmio_reg_valid(rgadr))
		return 0;

	return attr->mode;
}

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
	.is_visible = hwmon_attributes_visible,
};

static const struct attribute_group *hwmon_groups[] = {
	&hwmon_attrgroup,
	NULL
};

/*
 * HWMON SENSOR TYPE = hwmon_power
 *  - Sustained Power (power1_max)
 *  - Burst power     (power1_cap)
 *  - Peak power      (power1_crit)
 */
static const u32 i915_config_power[] = {
	HWMON_P_CAP | HWMON_P_MAX,
	0
};

static const struct hwmon_channel_info i915_power = {
	.type = hwmon_power,
	.config = i915_config_power,
};

static const struct hwmon_channel_info *i915_info[] = {
	&i915_power,
	NULL
};

static umode_t
i915_power_is_visible(const struct drm_i915_private *i915, u32 attr, int chan)
{
	i915_reg_t rgadr;

	switch (attr) {
	case hwmon_power_max:
		rgadr = i915->hwmon.rg.pkg_rapl_limit;
		break;
	case hwmon_power_cap:
		rgadr = i915->hwmon.rg.pkg_rapl_limit_udw;
		break;
	default:
		return 0;
	}

	if (!i915_mmio_reg_valid(rgadr))
		return 0;

	return 0664;
}

static int
i915_power_read(struct drm_i915_private *i915, u32 attr, int chan, long *val)
{
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	int ret = 0;

	switch (attr) {
	case hwmon_power_max:
		*val = (long)_field_read_and_scale(uncore,
						   hwmon->rg.pkg_rapl_limit,
						   PKG_PWR_LIM_1,
						   hwmon->scl_shift_power,
						   SF_POWER);
		break;
	case hwmon_power_cap:
		*val = (long)_field_read_and_scale(uncore,
						   hwmon->rg.pkg_rapl_limit_udw,
						   PKG_PWR_LIM_2,
						   hwmon->scl_shift_power,
						   SF_POWER);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int
i915_power_write(struct drm_i915_private *i915, u32 attr, int chan, long val)
{
	struct intel_uncore *uncore = &i915->uncore;
	struct i915_hwmon *hwmon = &i915->hwmon;
	int ret = 0;

	switch (attr) {
	case hwmon_power_max:
		_field_scale_and_write(uncore,
				       hwmon->rg.pkg_rapl_limit,
				       PKG_PWR_LIM_1,
				       hwmon->scl_shift_power,
				       SF_POWER, val);
		break;
	case hwmon_power_cap:
		_field_scale_and_write(uncore,
				       hwmon->rg.pkg_rapl_limit_udw,
				       PKG_PWR_LIM_2,
				       hwmon->scl_shift_power,
				       SF_POWER, val);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static umode_t
i915_is_visible(const void *data, enum hwmon_sensor_types type,
		u32 attr, int channel)
{
	struct drm_i915_private *i915 = (struct drm_i915_private *)data;

	switch (type) {
	case hwmon_power:
		return i915_power_is_visible(i915, attr, channel);
	default:
		return 0;
	}
}

static int
i915_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	  int channel, long *val)
{
	struct drm_i915_private *i915 = kdev_to_i915(dev);

	switch (type) {
	case hwmon_power:
		return i915_power_read(i915, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int
i915_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	   int channel, long val)
{
	struct drm_i915_private *i915 = kdev_to_i915(dev);

	switch (type) {
	case hwmon_power:
		return i915_power_write(i915, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops i915_hwmon_ops = {
	.is_visible = i915_is_visible,
	.read = i915_read,
	.write = i915_write,
};

static const struct hwmon_chip_info i915_chip_info = {
	.ops = &i915_hwmon_ops,
	.info = i915_info,
};

static void
i915_hwmon_get_preregistration_info(struct drm_i915_private *i915)
{
	struct i915_hwmon *hwmon = &i915->hwmon;
	struct intel_uncore *uncore = &i915->uncore;
	intel_wakeref_t wakeref;
	u32 val_sku_unit;

	if (IS_DG1(i915)) {
		hwmon->rg.pkg_power_sku_unit = PCU_PACKAGE_POWER_SKU_UNIT;
		hwmon->rg.pkg_power_sku = PCU_PACKAGE_POWER_SKU;
		hwmon->rg.pkg_energy_status = PCU_PACKAGE_ENERGY_STATUS;
		hwmon->rg.pkg_rapl_limit = PCU_PACKAGE_RAPL_LIMIT;
		hwmon->rg.pkg_rapl_limit_udw = PCU_PACKAGE_RAPL_LIMIT_UDW;
		hwmon->rg.plt_energy_status = PCU_PLATFORM_ENERGY_STATUS;
	} else {
		hwmon->rg.pkg_power_sku_unit = INVALID_MMIO_REG;
		hwmon->rg.pkg_power_sku = INVALID_MMIO_REG;
		hwmon->rg.pkg_energy_status = INVALID_MMIO_REG;
		hwmon->rg.pkg_rapl_limit = INVALID_MMIO_REG;
		hwmon->rg.pkg_rapl_limit_udw = INVALID_MMIO_REG;
		hwmon->rg.plt_energy_status = INVALID_MMIO_REG;
	}

	/*
	 * If a platform does not support *_PLATFORM_ENERGY_STATUS,
	 * try *PACKAGE_ENERGY_STATUS.
	 */
	if (i915_mmio_reg_valid(hwmon->rg.plt_energy_status))
		hwmon->rg.reg_energy_status = hwmon->rg.plt_energy_status;
	else
		hwmon->rg.reg_energy_status = hwmon->rg.pkg_energy_status;

	wakeref = intel_runtime_pm_get(uncore->rpm);

	/*
	 * The contents of register hwmon->rg.pkg_power_sku_unit do not change,
	 * so read it once and store the shift values.
	 */
	if (i915_mmio_reg_valid(hwmon->rg.pkg_power_sku_unit))
		val_sku_unit = intel_uncore_read(uncore,
						 hwmon->rg.pkg_power_sku_unit);
	else
		val_sku_unit = 0;

	hwmon->energy_counter_overflow = 0;

	if (i915_mmio_reg_valid(hwmon->rg.reg_energy_status))
		hwmon->energy_counter_prev =
			intel_uncore_read(uncore, hwmon->rg.reg_energy_status);
	else
		hwmon->energy_counter_prev = 0;

	intel_runtime_pm_put(uncore->rpm, wakeref);

	hwmon->scl_shift_power = le32_get_bits(val_sku_unit, PKG_PWR_UNIT);
	hwmon->scl_shift_energy = le32_get_bits(val_sku_unit, PKG_ENERGY_UNIT);
	hwmon->scl_shift_time = le32_get_bits(val_sku_unit, PKG_TIME_UNIT);

	/*
	 * There is no direct way to obtain the power default_limit.
	 * The best known workaround is to use the initial value of power1_max.
	 *
	 * The value of power1_max is reset to the default on reboot, but is
	 * not reset by a module unload/load sequence.  To allow proper
	 * functioning after a module reload, the value for power1_max is
	 * restored to its original value at module unload time in
	 * i915_hwmon_fini().
	 */
	hwmon->power_max_initial_value =
		(u32)_field_read_and_scale(uncore,
					   hwmon->rg.pkg_rapl_limit,
					   PKG_PWR_LIM_1,
					   hwmon->scl_shift_power, SF_POWER);
}

int i915_hwmon_init(struct drm_device *drm_dev)
{
	struct drm_i915_private *i915 = to_i915(drm_dev);
	struct i915_hwmon *hwmon = &i915->hwmon;
	struct device *hwmon_dev;

	mutex_init(&hwmon->hwmon_lock);

	i915_hwmon_get_preregistration_info(i915);

	hwmon_dev = hwmon_device_register_with_info(drm_dev->dev, "i915",
						    drm_dev,
						    &i915_chip_info,
						    hwmon_groups);

	if (IS_ERR(hwmon_dev)) {
		mutex_destroy(&hwmon->hwmon_lock);
		return PTR_ERR(hwmon_dev);
	}

	hwmon->dev = hwmon_dev;

	return 0;
}

void i915_hwmon_fini(struct drm_device *drm_dev)
{
	struct drm_i915_private *i915 = to_i915(drm_dev);
	struct i915_hwmon *hwmon = &i915->hwmon;

	if (hwmon->power_max_initial_value) {
		/* Restore power1_max. */
		_field_scale_and_write(&i915->uncore, hwmon->rg.pkg_rapl_limit,
				       PKG_PWR_LIM_1, hwmon->scl_shift_power,
				       SF_POWER,
				       hwmon->power_max_initial_value);
	}

	if (hwmon->dev)
		hwmon_device_unregister(hwmon->dev);

	mutex_destroy(&hwmon->hwmon_lock);

	memset(hwmon, 0, sizeof(*hwmon));
}
