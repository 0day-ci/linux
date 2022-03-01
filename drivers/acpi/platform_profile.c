// SPDX-License-Identifier: GPL-2.0-or-later

/* Platform profile sysfs interface */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_profile.h>
#include <linux/power_supply.h>
#include <linux/sysfs.h>

static struct platform_profile_handler *cur_profile;
static DEFINE_MUTEX(profile_lock);

static const char * const profile_names[] = {
	[PLATFORM_PROFILE_LOW_POWER] = "low-power",
	[PLATFORM_PROFILE_COOL] = "cool",
	[PLATFORM_PROFILE_QUIET] = "quiet",
	[PLATFORM_PROFILE_BALANCED] = "balanced",
	[PLATFORM_PROFILE_BALANCED_PERFORMANCE] = "balanced-performance",
	[PLATFORM_PROFILE_PERFORMANCE] = "performance",
};
static_assert(ARRAY_SIZE(profile_names) == PLATFORM_PROFILE_LAST);

static struct notifier_block ac_nb;
static int cur_profile_ac;
static int cur_profile_dc;

static int platform_profile_set(void)
{
	int profile, err;

	if (cur_profile_dc == PLATFORM_PROFILE_UNCONFIGURED)
		profile = cur_profile_ac;
	else {
		if (power_supply_is_system_supplied() > 0)
			profile = cur_profile_ac;
		else
			profile = cur_profile_dc;
	}

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	err = cur_profile->profile_set(cur_profile, profile);
	if (err)
		return err;

	sysfs_notify(acpi_kobj, NULL, "platform_profile");
	mutex_unlock(&profile_lock);
	return 0;
}

static int platform_profile_acpi_event(struct notifier_block *nb,
					unsigned long val,
					void *data)
{
	struct acpi_bus_event *entry = (struct acpi_bus_event *)data;

	WARN_ON(cur_profile_dc == PLATFORM_PROFILE_UNCONFIGURED);

	/* if power supply changed, then update profile */
	if (strcmp(entry->device_class, "ac_adapter") == 0)
		return platform_profile_set();

	return 0;
}

static ssize_t platform_profile_choices_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int len = 0;
	int err, i;

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	if (!cur_profile) {
		mutex_unlock(&profile_lock);
		return -ENODEV;
	}

	for_each_set_bit(i, cur_profile->choices, PLATFORM_PROFILE_LAST) {
		if (len == 0)
			len += sysfs_emit_at(buf, len, "%s", profile_names[i]);
		else
			len += sysfs_emit_at(buf, len, " %s", profile_names[i]);
	}
	len += sysfs_emit_at(buf, len, "\n");
	mutex_unlock(&profile_lock);
	return len;
}

static ssize_t platform_profile_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	enum platform_profile_option profile = PLATFORM_PROFILE_BALANCED;
	int err;

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	if (!cur_profile) {
		mutex_unlock(&profile_lock);
		return -ENODEV;
	}

	err = cur_profile->profile_get(cur_profile, &profile);
	mutex_unlock(&profile_lock);
	if (err)
		return err;

	/* Check that profile is valid index */
	if (WARN_ON((profile < 0) || (profile >= ARRAY_SIZE(profile_names))))
		return -EIO;

	return sysfs_emit(buf, "%s\n", profile_names[profile]);
}

static ssize_t configured_profile_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf, int profile)
{
	if (profile == PLATFORM_PROFILE_UNCONFIGURED)
		return sysfs_emit(buf, "Not-configured\n");

	/* Check that profile is valid index */
	if (WARN_ON((profile < 0) || (profile >= ARRAY_SIZE(profile_names))))
		return -EIO;
	return sysfs_emit(buf, "%s\n", profile_names[profile]);
}

static ssize_t platform_profile_ac_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return configured_profile_show(dev, attr, buf, cur_profile_ac);
}

static ssize_t platform_profile_dc_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return configured_profile_show(dev, attr, buf, cur_profile_dc);
}

static int profile_select(const char *buf)
{
	int err, i;

	err = mutex_lock_interruptible(&profile_lock);
	if (err)
		return err;

	if (!cur_profile) {
		mutex_unlock(&profile_lock);
		return -ENODEV;
	}

	/* Scan for a matching profile */
	i = sysfs_match_string(profile_names, buf);
	if (i < 0) {
		mutex_unlock(&profile_lock);
		return -EINVAL;
	}

	/* Check that platform supports this profile choice */
	if (!test_bit(i, cur_profile->choices)) {
		mutex_unlock(&profile_lock);
		return -EOPNOTSUPP;
	}

	mutex_unlock(&profile_lock);
	return i;
}

static ssize_t platform_profile_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int profile, err;

	profile	= profile_select(buf);
	if (profile < 0)
		return profile;

	cur_profile_ac = profile;
	err = platform_profile_set();
	if (err)
		return err;
	return count;
}

static ssize_t platform_profile_ac_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	return platform_profile_store(dev, attr, buf, count);
}

static ssize_t platform_profile_dc_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int profile, err;

	profile = profile_select(buf);
	if (profile < 0)
		return profile;

	/* We need to register for ACPI events now */
	if (cur_profile_dc == PLATFORM_PROFILE_UNCONFIGURED)
		register_acpi_notifier(&ac_nb);

	cur_profile_dc = profile;
	err = platform_profile_set();
	if (err)
		return err;
	return count;
}

static DEVICE_ATTR_RO(platform_profile_choices);
static DEVICE_ATTR_RW(platform_profile);
static DEVICE_ATTR_RW(platform_profile_ac);
static DEVICE_ATTR_RW(platform_profile_dc);

static struct attribute *platform_profile_attrs[] = {
	&dev_attr_platform_profile_choices.attr,
	&dev_attr_platform_profile.attr,
	&dev_attr_platform_profile_ac.attr,
	&dev_attr_platform_profile_dc.attr,
	NULL
};

static const struct attribute_group platform_profile_group = {
	.attrs = platform_profile_attrs
};

void platform_profile_notify(void)
{
	if (!cur_profile)
		return;
	sysfs_notify(acpi_kobj, NULL, "platform_profile");
}
EXPORT_SYMBOL_GPL(platform_profile_notify);

int platform_profile_register(struct platform_profile_handler *pprof)
{
	int err;

	mutex_lock(&profile_lock);
	/* We can only have one active profile */
	if (cur_profile) {
		mutex_unlock(&profile_lock);
		return -EEXIST;
	}

	/* Sanity check the profile handler field are set */
	if (!pprof || bitmap_empty(pprof->choices, PLATFORM_PROFILE_LAST) ||
		!pprof->profile_set || !pprof->profile_get) {
		mutex_unlock(&profile_lock);
		return -EINVAL;
	}

	err = sysfs_create_group(acpi_kobj, &platform_profile_group);
	if (err) {
		mutex_unlock(&profile_lock);
		return err;
	}

	cur_profile = pprof;
	cur_profile_ac = cur_profile_dc = PLATFORM_PROFILE_UNCONFIGURED;
	mutex_unlock(&profile_lock);
	ac_nb.notifier_call = platform_profile_acpi_event;
	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_register);

int platform_profile_remove(void)
{
	sysfs_remove_group(acpi_kobj, &platform_profile_group);
	if (cur_profile_dc != PLATFORM_PROFILE_UNCONFIGURED)
		unregister_acpi_notifier(&ac_nb);

	mutex_lock(&profile_lock);
	cur_profile = NULL;
	mutex_unlock(&profile_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(platform_profile_remove);

MODULE_AUTHOR("Mark Pearson <markpearson@lenovo.com>");
MODULE_LICENSE("GPL");
