// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/slab.h>

#define PHY_ACTIVITY_MAX_TRIGGERS 12

#define DEFINE_OFFLOAD_TRIGGER(trigger_name, trigger) \
	static ssize_t trigger_name##_show(struct device *dev, \
				struct device_attribute *attr, char *buf) \
	{ \
		struct led_classdev *led_cdev = led_trigger_get_led(dev); \
		int val; \
		val = led_cdev->hw_control_configure(led_cdev, trigger, BLINK_MODE_READ); \
		return sprintf(buf, "%d\n", val ? 1 : 0); \
	} \
	static ssize_t trigger_name##_store(struct device *dev, \
					struct device_attribute *attr, \
					const char *buf, size_t size) \
	{ \
		struct led_classdev *led_cdev = led_trigger_get_led(dev); \
		unsigned long state; \
		int cmd, ret; \
		ret = kstrtoul(buf, 0, &state); \
		if (ret) \
			return ret; \
		cmd = !!state ? BLINK_MODE_ENABLE : BLINK_MODE_DISABLE; \
		/* Update the configuration with every change */ \
		led_cdev->hw_control_configure(led_cdev, trigger, cmd); \
		return size; \
	} \
	DEVICE_ATTR_RW(trigger_name)

/* Expose sysfs for every blink to be configurable from userspace */
DEFINE_OFFLOAD_TRIGGER(blink_tx, BLINK_TX);
DEFINE_OFFLOAD_TRIGGER(blink_rx, BLINK_RX);
DEFINE_OFFLOAD_TRIGGER(keep_link_10m, KEEP_LINK_10M);
DEFINE_OFFLOAD_TRIGGER(keep_link_100m, KEEP_LINK_100M);
DEFINE_OFFLOAD_TRIGGER(keep_link_1000m, KEEP_LINK_1000M);
DEFINE_OFFLOAD_TRIGGER(keep_half_duplex, KEEP_HALF_DUPLEX);
DEFINE_OFFLOAD_TRIGGER(keep_full_duplex, KEEP_FULL_DUPLEX);
DEFINE_OFFLOAD_TRIGGER(option_linkup_over, OPTION_LINKUP_OVER);
DEFINE_OFFLOAD_TRIGGER(option_power_on_reset, OPTION_POWER_ON_RESET);
DEFINE_OFFLOAD_TRIGGER(option_blink_2hz, OPTION_BLINK_2HZ);
DEFINE_OFFLOAD_TRIGGER(option_blink_4hz, OPTION_BLINK_4HZ);
DEFINE_OFFLOAD_TRIGGER(option_blink_8hz, OPTION_BLINK_8HZ);

/* The attrs will be placed dynamically based on the supported triggers */
static struct attribute *phy_activity_attrs[PHY_ACTIVITY_MAX_TRIGGERS + 1];

static int offload_phy_activity_activate(struct led_classdev *led_cdev)
{
	u32 checked_list = 0;
	int i, trigger, ret;

	/* Scan the supported offload triggers and expose them in sysfs if supported */
	for (trigger = 0, i = 0; trigger < PHY_ACTIVITY_MAX_TRIGGERS; trigger++) {
		if (!(checked_list & BLINK_TX) &&
		    led_trigger_blink_mode_is_supported(led_cdev, BLINK_TX)) {
			phy_activity_attrs[i++] = &dev_attr_blink_tx.attr;
			checked_list |= BLINK_TX;
		}

		if (!(checked_list & BLINK_RX) &&
		    led_trigger_blink_mode_is_supported(led_cdev, BLINK_RX)) {
			phy_activity_attrs[i++] = &dev_attr_blink_rx.attr;
			checked_list |= BLINK_RX;
		}

		if (!(checked_list & KEEP_LINK_10M) &&
		    led_trigger_blink_mode_is_supported(led_cdev, KEEP_LINK_10M)) {
			phy_activity_attrs[i++] = &dev_attr_keep_link_10m.attr;
			checked_list |= KEEP_LINK_10M;
		}

		if (!(checked_list & KEEP_LINK_100M) &&
		    led_trigger_blink_mode_is_supported(led_cdev, KEEP_LINK_100M)) {
			phy_activity_attrs[i++] = &dev_attr_keep_link_100m.attr;
			checked_list |= KEEP_LINK_100M;
		}

		if (!(checked_list & KEEP_LINK_1000M) &&
		    led_trigger_blink_mode_is_supported(led_cdev, KEEP_LINK_1000M)) {
			phy_activity_attrs[i++] = &dev_attr_keep_link_1000m.attr;
			checked_list |= KEEP_LINK_1000M;
		}

		if (!(checked_list & KEEP_HALF_DUPLEX) &&
		    led_trigger_blink_mode_is_supported(led_cdev, KEEP_HALF_DUPLEX)) {
			phy_activity_attrs[i++] = &dev_attr_keep_half_duplex.attr;
			checked_list |= KEEP_HALF_DUPLEX;
		}

		if (!(checked_list & KEEP_FULL_DUPLEX) &&
		    led_trigger_blink_mode_is_supported(led_cdev, KEEP_FULL_DUPLEX)) {
			phy_activity_attrs[i++] = &dev_attr_keep_full_duplex.attr;
			checked_list |= KEEP_FULL_DUPLEX;
		}

		if (!(checked_list & OPTION_LINKUP_OVER) &&
		    led_trigger_blink_mode_is_supported(led_cdev, OPTION_LINKUP_OVER)) {
			phy_activity_attrs[i++] = &dev_attr_option_linkup_over.attr;
			checked_list |= OPTION_LINKUP_OVER;
		}

		if (!(checked_list & OPTION_POWER_ON_RESET) &&
		    led_trigger_blink_mode_is_supported(led_cdev, OPTION_POWER_ON_RESET)) {
			phy_activity_attrs[i++] = &dev_attr_option_power_on_reset.attr;
			checked_list |= OPTION_POWER_ON_RESET;
		}

		if (!(checked_list & OPTION_BLINK_2HZ) &&
		    led_trigger_blink_mode_is_supported(led_cdev, OPTION_BLINK_2HZ)) {
			phy_activity_attrs[i++] = &dev_attr_option_blink_2hz.attr;
			checked_list |= OPTION_BLINK_2HZ;
		}

		if (!(checked_list & OPTION_BLINK_4HZ) &&
		    led_trigger_blink_mode_is_supported(led_cdev, OPTION_BLINK_4HZ)) {
			phy_activity_attrs[i++] = &dev_attr_option_blink_4hz.attr;
			checked_list |= OPTION_BLINK_4HZ;
		}

		if (!(checked_list & OPTION_BLINK_8HZ) &&
		    led_trigger_blink_mode_is_supported(led_cdev, OPTION_BLINK_8HZ)) {
			phy_activity_attrs[i++] = &dev_attr_option_blink_8hz.attr;
			checked_list |= OPTION_BLINK_8HZ;
		}
	}

	/* Enable hardware mode. No custom configuration is applied,
	 * the LED driver will use whatever default configuration is
	 * currently configured.
	 */
	ret = led_cdev->hw_control_start(led_cdev);
	if (ret)
		return ret;

	return 0;
}

static void offload_phy_activity_deactivate(struct led_classdev *led_cdev)
{
	led_cdev->hw_control_stop(led_cdev);
}

static const struct attribute_group phy_activity_group = {
	.name = "hardware-phy-activity",
	.attrs = phy_activity_attrs,
};

static const struct attribute_group *phy_activity_groups[] = {
	&phy_activity_group,
	NULL,
};

static struct led_trigger offload_phy_activity_trigger = {
	.supported_blink_modes = HARDWARE_ONLY,
	.name       = "hardware-phy-activity",
	.activate   = offload_phy_activity_activate,
	.deactivate = offload_phy_activity_deactivate,
	.groups     = phy_activity_groups,
};

static int __init offload_phy_activity_init(void)
{
	return led_trigger_register(&offload_phy_activity_trigger);
}
device_initcall(offload_phy_activity_init);
