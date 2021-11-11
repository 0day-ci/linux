// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/slab.h>

#define PHY_ACTIVITY_MAX_BLINK_MODE 9

static ssize_t blink_mode_common_show(int blink_mode, struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	struct hardware_phy_activity_data *trigger_data = led_cdev->trigger_data;
	int val;

	val = test_bit(blink_mode, &trigger_data->mode);
	return sprintf(buf, "%d\n", val ? 1 : 0);
}

static ssize_t blink_mode_common_store(int blink_mode, struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	struct hardware_phy_activity_data *trigger_data = led_cdev->trigger_data;
	unsigned long state;
	int ret;

	ret = kstrtoul(buf, 0, &state);
	if (ret)
		return ret;

	if (!!state)
		set_bit(blink_mode, &trigger_data->mode);
	else
		clear_bit(blink_mode, &trigger_data->mode);

	/* Update the configuration with every change */
	led_cdev->blink_set(led_cdev, 0, 0);
	return size;
}

#define DEFINE_HW_BLINK_MODE(blink_mode_name, blink_bit) \
	static ssize_t blink_mode_name##_show(struct device *dev, \
				struct device_attribute *attr, char *buf) \
	{ \
		return blink_mode_common_show(blink_bit, dev, attr, buf); \
	} \
	static ssize_t blink_mode_name##_store(struct device *dev, \
					struct device_attribute *attr, \
					const char *buf, size_t size) \
	{ \
		return blink_mode_common_store(blink_bit, dev, attr, buf, size); \
	} \
	DEVICE_ATTR_RW(blink_mode_name)

/* Expose sysfs for every blink to be configurable from userspace */
DEFINE_HW_BLINK_MODE(blink_tx, TRIGGER_PHY_ACTIVITY_BLINK_TX);
DEFINE_HW_BLINK_MODE(blink_rx, TRIGGER_PHY_ACTIVITY_BLINK_RX);
DEFINE_HW_BLINK_MODE(link_10M, TRIGGER_PHY_ACTIVITY_LINK_10M);
DEFINE_HW_BLINK_MODE(link_100M, TRIGGER_PHY_ACTIVITY_LINK_100M);
DEFINE_HW_BLINK_MODE(link_1000M, TRIGGER_PHY_ACTIVITY_LINK_1000M);
DEFINE_HW_BLINK_MODE(half_duplex, TRIGGER_PHY_ACTIVITY_HALF_DUPLEX);
DEFINE_HW_BLINK_MODE(full_duplex, TRIGGER_PHY_ACTIVITY_FULL_DUPLEX);
DEFINE_HW_BLINK_MODE(option_linkup_over, TRIGGER_PHY_ACTIVITY_OPTION_LINKUP_OVER);
DEFINE_HW_BLINK_MODE(option_power_on_reset, TRIGGER_PHY_ACTIVITY_OPTION_POWER_ON_RESET);

static struct attribute *blink_mode_tbl[PHY_ACTIVITY_MAX_BLINK_MODE] = {
	&dev_attr_blink_tx.attr,
	&dev_attr_blink_rx.attr,
	&dev_attr_link_10M.attr,
	&dev_attr_link_100M.attr,
	&dev_attr_link_1000M.attr,
	&dev_attr_half_duplex.attr,
	&dev_attr_full_duplex.attr,
	&dev_attr_option_linkup_over.attr,
	&dev_attr_option_power_on_reset.attr,
};

/* The attrs will be placed dynamically based on the supported triggers */
// static struct attribute *phy_activity_attrs[PHY_ACTIVITY_MAX_TRIGGERS + 1];

static int hardware_phy_activity_activate(struct led_classdev *led_cdev)
{
	struct hardware_phy_activity_data *trigger_data;
	struct attribute_group *phy_activity_group;
	struct attribute **phy_activity_attrs;
	unsigned long supported_mode = 0;
	int i, j, count = 0, ret;

	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		return -ENOMEM;

	led_set_trigger_data(led_cdev, trigger_data);

	/* Check supported blink modes
	 * Request one mode at time and check if it can run in hardware mode
	 */
	for (i = 0; i < PHY_ACTIVITY_MAX_BLINK_MODE; i++) {
		trigger_data->mode = 0;

		set_bit(i, &trigger_data->mode);

		if (!led_cdev->blink_set(led_cdev, 0, 0)) {
			set_bit(i, &supported_mode);
			count++;
		}
	}

	if (!count) {
		ret = -EINVAL;
		goto fail_alloc_driver_data;
	}

	phy_activity_group = kzalloc(sizeof(phy_activity_group), GFP_KERNEL);
	if (!phy_activity_group) {
		ret = -ENOMEM;
		goto fail_alloc_driver_data;
	}

	phy_activity_attrs = kcalloc(count + 1, sizeof(struct attribute *), GFP_KERNEL);
	if (!phy_activity_attrs)
		goto fail_alloc_attrs;

	phy_activity_group->name = "hardware-phy-activity";
	phy_activity_group->attrs = phy_activity_attrs;

	for (i = 0, j = 0; i < count; i++)
		if (test_bit(i, &supported_mode))
			phy_activity_attrs[j++] = blink_mode_tbl[i];

	ret = device_add_group(led_cdev->dev, phy_activity_group);
	if (ret)
		goto fail_alloc_group;

	trigger_data->group = phy_activity_group;

	/* Enable hardware mode. No custom configuration is applied,
	 * the LED driver will use whatever default configuration is
	 * currently configured.
	 */
	ret = led_cdev->hw_control_start(led_cdev);
	if (ret)
		goto fail_alloc_group;

	return 0;
fail_alloc_driver_data:
	kfree(trigger_data);
fail_alloc_group:
	kfree(phy_activity_attrs);
fail_alloc_attrs:
	kfree(phy_activity_group);
	return ret;
}

static void hardware_phy_activity_deactivate(struct led_classdev *led_cdev)
{
	struct hardware_phy_activity_data *trigger_data = led_get_trigger_data(led_cdev);

	led_cdev->hw_control_stop(led_cdev);
	device_remove_group(led_cdev->dev, trigger_data->group);
	kfree(trigger_data->group->attrs);
	kfree(trigger_data->group);
	kfree(trigger_data);
}

static struct led_trigger hardware_phy_activity_trigger = {
	.supported_blink_modes = HARDWARE_ONLY,
	.name       = "hardware-phy-activity",
	.activate   = hardware_phy_activity_activate,
	.deactivate = hardware_phy_activity_deactivate,
};

static int __init hardware_phy_activity_init(void)
{
	return led_trigger_register(&hardware_phy_activity_trigger);
}
device_initcall(hardware_phy_activity_init);
