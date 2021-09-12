// SPDX-License-Identifier: GPL-2.0
/*
 * UFS hardware monitoring support
 * Copyright (c) 2021, Western Digital Corporation
 */

#include <linux/hwmon.h>

#include "ufshcd.h"

struct ufs_hwmon_data {
	struct ufs_hba *hba;
	u8 mask;
};

static bool ufs_temp_enabled(struct ufs_hba *hba, u8 mask)
{
	u32 ee_mask;

	if (ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			      QUERY_ATTR_IDN_EE_CONTROL, 0, 0, &ee_mask))
		return false;

	return (mask & ee_mask & MASK_EE_TOO_HIGH_TEMP) ||
		(mask & ee_mask & MASK_EE_TOO_LOW_TEMP);
}

static bool ufs_temp_valid(struct ufs_hba *hba, u8 mask,
			   enum attr_idn idn, u32 value)
{
	return (idn == QUERY_ATTR_IDN_CASE_ROUGH_TEMP && value >= 1 &&
		value <= 250 && ufs_temp_enabled(hba, mask)) ||
	      (idn == QUERY_ATTR_IDN_HIGH_TEMP_BOUND && value >= 100 &&
	       value <= 250) ||
	      (idn == QUERY_ATTR_IDN_LOW_TEMP_BOUND && value >= 1 &&
	       value <= 80);
}

static int ufs_get_temp(struct ufs_hba *hba, u8 mask, enum attr_idn idn)
{
	u32 value;

	if (ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR, idn, 0, 0,
	    &value))
		return 0;

	if (ufs_temp_valid(hba, mask, idn, value))
		return value - 80;

	return 0;
}

static int ufs_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct ufs_hwmon_data *data = dev_get_drvdata(dev);
	struct ufs_hba *hba = data->hba;
	u8 mask = data->mask;
	int err = 0;
	bool get_temp = true;

	if (type != hwmon_temp)
		return 0;

	down(&hba->host_sem);

	if (!ufshcd_is_user_access_allowed(hba)) {
		up(&hba->host_sem);
		return -EBUSY;
	}

	ufshcd_rpm_get_sync(hba);

	switch (attr) {
	case hwmon_temp_enable:
		*val = ufs_temp_enabled(hba, mask);
		get_temp = false;

		break;
	case hwmon_temp_max_alarm:
		*val = ufs_get_temp(hba, mask, QUERY_ATTR_IDN_HIGH_TEMP_BOUND);

		break;
	case hwmon_temp_min_alarm:
		*val = ufs_get_temp(hba, mask, QUERY_ATTR_IDN_LOW_TEMP_BOUND);

		break;
	case hwmon_temp_input:
		*val = ufs_get_temp(hba, mask, QUERY_ATTR_IDN_CASE_ROUGH_TEMP);

		break;
	default:
		err = -EOPNOTSUPP;

		break;
	}

	ufshcd_rpm_put_sync(hba);

	up(&hba->host_sem);

	if (get_temp && !err && *val == 0)
		err = -EINVAL;

	return err;
}

static umode_t ufs_hwmon_is_visible(const void *_data,
				     enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_enable:
	case hwmon_temp_max_alarm:
	case hwmon_temp_min_alarm:
	case hwmon_temp_input:
		return 0444;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info *ufs_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_ENABLE | HWMON_T_INPUT |
			    HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM),
	NULL
};

static const struct hwmon_ops ufs_hwmon_ops = {
	.is_visible	= ufs_hwmon_is_visible,
	.read		= ufs_hwmon_read,
};

static const struct hwmon_chip_info ufs_hwmon_hba_info = {
	.ops	= &ufs_hwmon_ops,
	.info	= ufs_hwmon_info,
};

void ufs_hwmon_probe(struct ufs_hba *hba, u8 mask)
{
	struct device *dev = hba->dev;
	struct ufs_hwmon_data *data;
	struct device *hwmon;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->hba = hba;
	data->mask = mask;

	hwmon = hwmon_device_register_with_info(dev, "ufs",
						data, &ufs_hwmon_hba_info,
						NULL);
	if (IS_ERR(hwmon)) {
		dev_warn(dev, "Failed to instantiate hwmon device\n");
		kfree(data);
		return;
	}

	hba->hwmon_device = hwmon;
}

void ufs_hwmon_remove(struct ufs_hba *hba)
{
	struct ufs_hwmon_data *data;

	if (!hba->hwmon_device)
		return;

	data = dev_get_drvdata(hba->hwmon_device);
	hwmon_device_unregister(hba->hwmon_device);
	hba->hwmon_device = NULL;
	kfree(data);
}

void ufs_hwmon_notify_event(struct ufs_hba *hba, u8 ee_mask)
{
	if (!hba->hwmon_device)
		return;

	if (ee_mask & MASK_EE_TOO_HIGH_TEMP)
		hwmon_notify_event(hba->hwmon_device, hwmon_temp,
				   hwmon_temp_max_alarm, 0);

	if (ee_mask & MASK_EE_TOO_LOW_TEMP)
		hwmon_notify_event(hba->hwmon_device, hwmon_temp,
				   hwmon_temp_min_alarm, 0);
}
