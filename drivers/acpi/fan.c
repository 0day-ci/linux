// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_fan.c - ACPI Fan Driver ($Revision: 29 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/thermal.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/sort.h>

#include "fan.h"

MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION("ACPI Fan Driver");
MODULE_LICENSE("GPL");

static int acpi_fan_probe(struct platform_device *pdev);
static int acpi_fan_remove(struct platform_device *pdev);

static const struct acpi_device_id fan_device_ids[] = {
	ACPI_FAN_DEVICE_IDS,
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, fan_device_ids);

#ifdef CONFIG_PM_SLEEP
static int acpi_fan_suspend(struct device *dev);
static int acpi_fan_resume(struct device *dev);
static const struct dev_pm_ops acpi_fan_pm = {
	.resume = acpi_fan_resume,
	.freeze = acpi_fan_suspend,
	.thaw = acpi_fan_resume,
	.restore = acpi_fan_resume,
};
#define FAN_PM_OPS_PTR (&acpi_fan_pm)
#else
#define FAN_PM_OPS_PTR NULL
#endif

#define ACPI_FPS_NAME_LEN	20

struct acpi_fan_fps {
	u64 control;
	u64 trip_point;
	u64 speed;
	u64 noise_level;
	u64 power;
	char name[ACPI_FPS_NAME_LEN];
	struct device_attribute dev_attr;
};

struct acpi_fan_fif {
	u64 revision;
	u64 fine_grain_ctrl;
	u64 step_size;
	u64 low_speed_notification;
};

struct acpi_fan_fst {
	u64 revision;
	u64 control;
	u64 speed;
};

struct acpi_fan {
	bool acpi4;
	struct acpi_fan_fif fif;
	struct acpi_fan_fps *fps;
	int fps_count;
	struct thermal_cooling_device *cdev;
	struct device_attribute fst_speed;
};

static struct platform_driver acpi_fan_driver = {
	.probe = acpi_fan_probe,
	.remove = acpi_fan_remove,
	.driver = {
		.name = "acpi-fan",
		.acpi_match_table = fan_device_ids,
		.pm = FAN_PM_OPS_PTR,
	},
};

/* thermal cooling device callbacks */
static int fan_get_max_state(struct thermal_cooling_device *cdev, unsigned long
			     *state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_fan *fan = acpi_driver_data(device);

	if (fan->acpi4) {
		if (fan->fif.fine_grain_ctrl)
			*state = 100 / (int)fan->fif.step_size;
		else
			*state = fan->fps_count - 1;
	} else {
		*state = 1;
	}

	return 0;
}

static int fan_get_fps(struct acpi_device *device, struct acpi_fan_fst *fst)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = acpi_evaluate_object(device->handle, "_FST", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Get fan state failed\n");
		return status;
	}

	obj = buffer.pointer;
	if (!obj || obj->type != ACPI_TYPE_PACKAGE ||
	    obj->package.count != 3 ||
	    obj->package.elements[1].type != ACPI_TYPE_INTEGER) {
		dev_err(&device->dev, "Invalid _FST data\n");
		status = -EINVAL;
		goto err;
	}

	fst->revision = obj->package.elements[0].integer.value;
	fst->control = obj->package.elements[1].integer.value;
	fst->speed = obj->package.elements[2].integer.value;

	status = 0;
err:
	kfree(obj);
	return status;
}

static int fan_get_state_acpi4(struct acpi_device *device, unsigned long *state)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	struct acpi_fan_fst fst;
	int status;
	int control, i;

	status = fan_get_fps(device, &fst);
	if (status)
		return status;

	control = fst.control;

	if (fan->fif.fine_grain_ctrl) {
		/* This control should be same what we set using _FSL by spec */
		if (control > 100) {
			dev_dbg(&device->dev, "Invalid control value returned\n");
			return -EINVAL;
		}

		*state = control / (int)fan->fif.step_size;
		return 0;
	}

	for (i = 0; i < fan->fps_count; i++) {
		if (control == fan->fps[i].control)
			break;
	}
	if (i == fan->fps_count) {
		dev_dbg(&device->dev, "Invalid control value returned\n");
		return -EINVAL;
	}

	*state = i;

	return status;
}

static int fan_get_state(struct acpi_device *device, unsigned long *state)
{
	int result;
	int acpi_state = ACPI_STATE_D0;

	result = acpi_device_update_power(device, &acpi_state);
	if (result)
		return result;

	*state = acpi_state == ACPI_STATE_D3_COLD
			|| acpi_state == ACPI_STATE_D3_HOT ?
		0 : (acpi_state == ACPI_STATE_D0 ? 1 : -1);
	return 0;
}

static int fan_get_cur_state(struct thermal_cooling_device *cdev, unsigned long
			     *state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_fan *fan = acpi_driver_data(device);

	if (fan->acpi4)
		return fan_get_state_acpi4(device, state);
	else
		return fan_get_state(device, state);
}

static int fan_set_state(struct acpi_device *device, unsigned long state)
{
	if (state != 0 && state != 1)
		return -EINVAL;

	return acpi_device_set_power(device,
				     state ? ACPI_STATE_D0 : ACPI_STATE_D3_COLD);
}

static int fan_set_state_acpi4(struct acpi_device *device, unsigned long state)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	acpi_status status;
	u64 value = state;
	int max_state;

	if (fan->fif.fine_grain_ctrl)
		max_state = 100 / (int)fan->fif.step_size;
	else
		max_state = fan->fps_count - 1;

	if (state > max_state)
		return -EINVAL;

	if (fan->fif.fine_grain_ctrl) {
		int rem;

		value *= fan->fif.step_size;

		/*
		 * In the event OSPM’s incremental selections of Level
		 * using the StepSize field value do not sum to 100%,
		 * OSPM may select an appropriate ending Level
		 * increment to reach 100%.
		 */
		rem = 100 - value;
		if (rem && rem < fan->fif.step_size)
			value = 100;
	} else {
		value = fan->fps[state].control;
	}

	status = acpi_execute_simple_method(device->handle, "_FSL", value);
	if (ACPI_FAILURE(status)) {
		dev_dbg(&device->dev, "Failed to set state by _FSL\n");
		return status;
	}

	return 0;
}

static int
fan_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_fan *fan = acpi_driver_data(device);

	if (fan->acpi4)
		return fan_set_state_acpi4(device, state);
	else
		return fan_set_state(device, state);
}

static const struct thermal_cooling_device_ops fan_cooling_ops = {
	.get_max_state = fan_get_max_state,
	.get_cur_state = fan_get_cur_state,
	.set_cur_state = fan_set_cur_state,
};

/* --------------------------------------------------------------------------
 *                               Driver Interface
 * --------------------------------------------------------------------------
*/

static bool acpi_fan_is_acpi4(struct acpi_device *device)
{
	return acpi_has_method(device->handle, "_FIF") &&
	       acpi_has_method(device->handle, "_FPS") &&
	       acpi_has_method(device->handle, "_FSL") &&
	       acpi_has_method(device->handle, "_FST");
}

static int acpi_fan_get_fif(struct acpi_device *device)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_fan *fan = acpi_driver_data(device);
	struct acpi_buffer format = { sizeof("NNNN"), "NNNN" };
	struct acpi_buffer fif = { sizeof(fan->fif), &fan->fif };
	union acpi_object *obj;
	acpi_status status;

	status = acpi_evaluate_object(device->handle, "_FIF", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	obj = buffer.pointer;
	if (!obj || obj->type != ACPI_TYPE_PACKAGE) {
		dev_err(&device->dev, "Invalid _FIF data\n");
		status = -EINVAL;
		goto err;
	}

	status = acpi_extract_package(obj, &format, &fif);
	if (ACPI_FAILURE(status)) {
		dev_err(&device->dev, "Invalid _FIF element\n");
		status = -EINVAL;
	}

	/* If there is a bug in step size and set as 0, change to 1 */
	if (!fan->fif.step_size)
		fan->fif.step_size = 1;
	/* If step size > 9, change to 9 (by spec valid values 1-9) */
	if (fan->fif.step_size > 9)
		fan->fif.step_size = 9;
err:
	kfree(obj);
	return status;
}

static int acpi_fan_speed_cmp(const void *a, const void *b)
{
	const struct acpi_fan_fps *fps1 = a;
	const struct acpi_fan_fps *fps2 = b;
	return fps1->speed - fps2->speed;
}

static ssize_t show_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_fan_fps *fps = container_of(attr, struct acpi_fan_fps, dev_attr);
	int count;

	if (fps->control == 0xFFFFFFFF || fps->control > 100)
		count = scnprintf(buf, PAGE_SIZE, "not-defined:");
	else
		count = scnprintf(buf, PAGE_SIZE, "%lld:", fps->control);

	if (fps->trip_point == 0xFFFFFFFF || fps->trip_point > 9)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined:");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld:", fps->trip_point);

	if (fps->speed == 0xFFFFFFFF)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined:");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld:", fps->speed);

	if (fps->noise_level == 0xFFFFFFFF)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined:");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld:", fps->noise_level * 100);

	if (fps->power == 0xFFFFFFFF)
		count += scnprintf(&buf[count], PAGE_SIZE - count, "not-defined\n");
	else
		count += scnprintf(&buf[count], PAGE_SIZE - count, "%lld\n", fps->power);

	return count;
}

static ssize_t show_fan_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = container_of(dev, struct acpi_device, dev);
	struct acpi_fan_fst fst;
	int status;

	status = fan_get_fps(acpi_dev, &fst);
	if (status)
		return status;

	return sprintf(buf, "%lld\n", fst.speed);
}

static int acpi_fan_get_fps(struct acpi_device *device)
{
	struct acpi_fan *fan = acpi_driver_data(device);
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int i;

	/* _FST is present if we are here */
	sysfs_attr_init(&fan->fst_speed.attr);
	fan->fst_speed.show = show_fan_speed;
	fan->fst_speed.store = NULL;
	fan->fst_speed.attr.name = "fan_speed_rpm";
	fan->fst_speed.attr.mode = 0444;
	status = sysfs_create_file(&device->dev.kobj, &fan->fst_speed.attr);
	if (status)
		return status;

	status = acpi_evaluate_object(device->handle, "_FPS", NULL, &buffer);
	if (ACPI_FAILURE(status))
		goto rem_attr;

	obj = buffer.pointer;
	if (!obj || obj->type != ACPI_TYPE_PACKAGE || obj->package.count < 2) {
		dev_err(&device->dev, "Invalid _FPS data\n");
		status = -EINVAL;
		goto rem_attr;
	}

	fan->fps_count = obj->package.count - 1; /* minus revision field */
	fan->fps = devm_kcalloc(&device->dev,
				fan->fps_count, sizeof(struct acpi_fan_fps),
				GFP_KERNEL);
	if (!fan->fps) {
		dev_err(&device->dev, "Not enough memory\n");
		status = -ENOMEM;
		goto err;
	}
	for (i = 0; i < fan->fps_count; i++) {
		struct acpi_buffer format = { sizeof("NNNNN"), "NNNNN" };
		struct acpi_buffer fps = { offsetof(struct acpi_fan_fps, name),
						&fan->fps[i] };
		status = acpi_extract_package(&obj->package.elements[i + 1],
					      &format, &fps);
		if (ACPI_FAILURE(status)) {
			dev_err(&device->dev, "Invalid _FPS element\n");
			goto err;
		}
	}

	/* sort the state array according to fan speed in increase order */
	sort(fan->fps, fan->fps_count, sizeof(*fan->fps),
	     acpi_fan_speed_cmp, NULL);

	for (i = 0; i < fan->fps_count; ++i) {
		struct acpi_fan_fps *fps = &fan->fps[i];

		snprintf(fps->name, ACPI_FPS_NAME_LEN, "state%d", i);
		sysfs_attr_init(&fps->dev_attr.attr);
		fps->dev_attr.show = show_state;
		fps->dev_attr.store = NULL;
		fps->dev_attr.attr.name = fps->name;
		fps->dev_attr.attr.mode = 0444;
		status = sysfs_create_file(&device->dev.kobj, &fps->dev_attr.attr);
		if (status) {
			int j;

			for (j = 0; j < i; ++j)
				sysfs_remove_file(&device->dev.kobj, &fan->fps[j].dev_attr.attr);
			break;
		}
	}

err:
	kfree(obj);
	if (!status)
		return 0;
rem_attr:
	sysfs_remove_file(&device->dev.kobj, &fan->fst_speed.attr);

	return status;
}

static int acpi_fan_probe(struct platform_device *pdev)
{
	int result = 0;
	struct thermal_cooling_device *cdev;
	struct acpi_fan *fan;
	struct acpi_device *device = ACPI_COMPANION(&pdev->dev);
	char *name;

	fan = devm_kzalloc(&pdev->dev, sizeof(*fan), GFP_KERNEL);
	if (!fan) {
		dev_err(&device->dev, "No memory for fan\n");
		return -ENOMEM;
	}
	device->driver_data = fan;
	platform_set_drvdata(pdev, fan);

	if (acpi_fan_is_acpi4(device)) {
		result = acpi_fan_get_fif(device);
		if (result)
			return result;

		result = acpi_fan_get_fps(device);
		if (result)
			return result;

		fan->acpi4 = true;
	} else {
		result = acpi_device_update_power(device, NULL);
		if (result) {
			dev_err(&device->dev, "Failed to set initial power state\n");
			goto err_end;
		}
	}

	if (!strncmp(pdev->name, "PNP0C0B", strlen("PNP0C0B")))
		name = "Fan";
	else
		name = acpi_device_bid(device);

	cdev = thermal_cooling_device_register(name, device,
						&fan_cooling_ops);
	if (IS_ERR(cdev)) {
		result = PTR_ERR(cdev);
		goto err_end;
	}

	dev_dbg(&pdev->dev, "registered as cooling_device%d\n", cdev->id);

	fan->cdev = cdev;
	result = sysfs_create_link(&pdev->dev.kobj,
				   &cdev->device.kobj,
				   "thermal_cooling");
	if (result)
		dev_err(&pdev->dev, "Failed to create sysfs link 'thermal_cooling'\n");

	result = sysfs_create_link(&cdev->device.kobj,
				   &pdev->dev.kobj,
				   "device");
	if (result) {
		dev_err(&pdev->dev, "Failed to create sysfs link 'device'\n");
		goto err_end;
	}

	return 0;

err_end:
	if (fan->acpi4) {
		int i;

		for (i = 0; i < fan->fps_count; ++i)
			sysfs_remove_file(&device->dev.kobj, &fan->fps[i].dev_attr.attr);
	}

	return result;
}

static int acpi_fan_remove(struct platform_device *pdev)
{
	struct acpi_fan *fan = platform_get_drvdata(pdev);

	if (fan->acpi4) {
		struct acpi_device *device = ACPI_COMPANION(&pdev->dev);
		int i;

		for (i = 0; i < fan->fps_count; ++i)
			sysfs_remove_file(&device->dev.kobj, &fan->fps[i].dev_attr.attr);
	}
	sysfs_remove_link(&pdev->dev.kobj, "thermal_cooling");
	sysfs_remove_link(&fan->cdev->device.kobj, "device");
	thermal_cooling_device_unregister(fan->cdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int acpi_fan_suspend(struct device *dev)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	if (fan->acpi4)
		return 0;

	acpi_device_set_power(ACPI_COMPANION(dev), ACPI_STATE_D0);

	return AE_OK;
}

static int acpi_fan_resume(struct device *dev)
{
	int result;
	struct acpi_fan *fan = dev_get_drvdata(dev);

	if (fan->acpi4)
		return 0;

	result = acpi_device_update_power(ACPI_COMPANION(dev), NULL);
	if (result)
		dev_err(dev, "Error updating fan power state\n");

	return result;
}
#endif

module_platform_driver(acpi_fan_driver);
