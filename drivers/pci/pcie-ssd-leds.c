// SPDX-License-Identifier: GPL-2.0-only
/*
 * Module to provide LED interface control for PCIe SSD status LED states,
 * as defined in the "_DSM additions for PCIe SSD Status LED Management" ECN
 * to the PCI Firmware Specification Revision 3.2, dated 12 February 2020.
 *
 * Copyright (c) 2021 Dell Inc.
 *
 * TODO: Add NPEM support
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <uapi/linux/uleds.h>

#define DRIVER_NAME	"pcie-ssd-leds"
#define DRIVER_VERSION	"v1.0"

struct led_state {
	char *name;
	u32 mask;
};

static struct led_state led_states[] = {
	{ .name = "ok",			.mask = BIT(2) },
	{ .name = "locate",		.mask = BIT(3) },
	{ .name = "failed",		.mask = BIT(4) },
	{ .name = "rebuild",		.mask = BIT(5) },
	{ .name = "pfa",		.mask = BIT(6) },
	{ .name = "hotspare",		.mask = BIT(7) },
	{ .name = "ica",		.mask = BIT(8) },
	{ .name = "ifa",		.mask = BIT(9) },
	{ .name = "invalid",		.mask = BIT(10) },
	{ .name = "disabled",		.mask = BIT(11) },
};

struct drive_status_led_ops {
	int (*get_supported_states)(struct device *dev, u32 *states);
	int (*get_current_states)(struct device *dev, u32 *states);
	int (*set_current_states)(struct device *dev, u32 states);
};

/*
 * one drive_status_dev struct for each drive/slot device with status LEDs
 */
struct drive_status_dev {
	struct list_head drive_list;
	struct device *dev;
	struct drive_status_led_ops *ops;
	u32 supported_states;
	struct led_classdev led_cdev;
	enum led_brightness brightness;
};

struct mutex drive_list_lock;
struct list_head drive_list;

/*
 * code specific to _DSM method
 */
const guid_t pcie_ssdleds_dsm_guid =
	GUID_INIT(0x5d524d9d, 0xfff9, 0x4d4b,
		  0x8c, 0xb7, 0x74, 0x7e, 0xd5, 0x1e, 0x19, 0x4d);

#define GET_SUPPORTED_STATES_DSM	0x01
#define GET_STATE_DSM			0x02
#define SET_STATE_DSM			0x03

struct ssdleds_dsm_output {
	u16 status;
	u8 function_specific_err;
	u8 vendor_specific_err;
	u32 state;
};

static void dsm_status_err_print(struct device *dev,
			     struct ssdleds_dsm_output *output)
{
	switch (output->status) {
	case 0:
		break;
	case 1:
		dev_dbg(dev, "_DSM not supported\n");
		break;
	case 2:
		dev_dbg(dev, "_DSM invalid input parameters\n");
		break;
	case 3:
		dev_dbg(dev, "_DSM communication error\n");
		break;
	case 4:
		dev_dbg(dev, "_DSM function-specific error 0x%x\n",
			output->function_specific_err);
		break;
	case 5:
		dev_dbg(dev, "_DSM vendor-specific error 0x%x\n",
			output->vendor_specific_err);
		break;
	default:
		dev_dbg(dev, "_DSM returned unknown status 0x%x\n",
			output->status);
	}
}

static int dsm_set(struct device *dev, u32 value)
{
	acpi_handle handle;
	union acpi_object *out_obj, arg3[2];
	struct ssdleds_dsm_output *dsm_output;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -ENODEV;

	arg3[0].type = ACPI_TYPE_PACKAGE;
	arg3[0].package.count = 1;
	arg3[0].package.elements = &arg3[1];

	arg3[1].type = ACPI_TYPE_BUFFER;
	arg3[1].buffer.length = 4;
	arg3[1].buffer.pointer = (u8 *)&value;

	out_obj = acpi_evaluate_dsm_typed(handle, &pcie_ssdleds_dsm_guid,
				1, SET_STATE_DSM, &arg3[0], ACPI_TYPE_BUFFER);
	if (!out_obj)
		return -EIO;

	if (out_obj->buffer.length < 8) {
		ACPI_FREE(out_obj);
		return -EIO;
	}

	dsm_output = (struct ssdleds_dsm_output *)out_obj->buffer.pointer;

	if (dsm_output->status != 0) {
		dsm_status_err_print(dev, dsm_output);
		ACPI_FREE(out_obj);
		return -EIO;
	}
	ACPI_FREE(out_obj);
	return 0;
}

static int dsm_get(struct device *dev, u64 dsm_func, u32 *output)
{
	acpi_handle handle;
	union acpi_object *out_obj;
	struct ssdleds_dsm_output *dsm_output;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -ENODEV;

	out_obj = acpi_evaluate_dsm_typed(handle, &pcie_ssdleds_dsm_guid, 0x1,
					  dsm_func, NULL, ACPI_TYPE_BUFFER);
	if (!out_obj)
		return -EIO;

	if (out_obj->buffer.length < 8) {
		ACPI_FREE(out_obj);
		return -EIO;
	}

	dsm_output = (struct ssdleds_dsm_output *)out_obj->buffer.pointer;
	if (dsm_output->status != 0) {
		dsm_status_err_print(dev, dsm_output);
		ACPI_FREE(out_obj);
		return -EIO;
	}

	*output = dsm_output->state;
	ACPI_FREE(out_obj);
	return 0;
}

static int get_supported_states_dsm(struct device *dev, u32 *states)
{
	return dsm_get(dev, GET_SUPPORTED_STATES_DSM, states);
}

static int get_current_states_dsm(struct device *dev, u32 *states)
{
	return dsm_get(dev, GET_STATE_DSM, states);
}

static int set_current_states_dsm(struct device *dev, u32 states)
{
	return dsm_set(dev, states);
}

static bool pdev_has_dsm(struct pci_dev *pdev)
{
	acpi_handle handle;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return false;

	return acpi_check_dsm(handle, &pcie_ssdleds_dsm_guid, 0x1,
			      1 << GET_SUPPORTED_STATES_DSM ||
			      1 << GET_STATE_DSM ||
			      1 << SET_STATE_DSM);
}

struct drive_status_led_ops dsm_drive_status_led_ops = {
	.get_supported_states = get_supported_states_dsm,
	.get_current_states = get_current_states_dsm,
	.set_current_states = set_current_states_dsm,
};

/*
 * code not specific to method (_DSM/NPEM)
 */
static ssize_t states_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct drive_status_dev *dsdev;
	u32 val;
	int err, i, res = 0;

	dsdev = container_of(led_cdev, struct drive_status_dev, led_cdev);

	err = dsdev->ops->get_current_states(dsdev->dev, &val);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(led_states); i++) {
		if (led_states[i].mask & dsdev->supported_states & val)
			res += sysfs_emit_at(buf, res, "[%s] ",
					     led_states[i].name);
		else if (led_states[i].mask & dsdev->supported_states)
			res += sysfs_emit_at(buf, res, "%s",
					     led_states[i].name);
	}
	res += sysfs_emit_at(buf, res, "\n");
	return res;
}

static ssize_t states_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct drive_status_dev *dsdev;
	u32 states = 0;
	int err;

	/*
	 * parse input
	 *
	 * If multiple states are being set, they should be separated by
	 * spaces or commas.  Input buffer may end with `\n`.
	 */
	while (*buf) {
		size_t len;
		int i;

		while (*buf == ' ' || *buf == ',' || *buf == '\n')
			buf++;
		len = strcspn(buf, " ,\n");
		if (!len)
			break;
		for (i = 0; i < ARRAY_SIZE(led_states); i++) {
			if (!strncmp(buf, led_states[i].name, len)) {
				states |= led_states[i].mask;
				buf += len;
				break;
			}
		}
		if (i == ARRAY_SIZE(led_states))
			return -EINVAL;
	}

	/*
	 * set states
	 */
	dsdev = container_of(led_cdev, struct drive_status_dev, led_cdev);
	if (states & ~dsdev->supported_states)
		return -EINVAL;
	if (states)
		dsdev->brightness = LED_ON;

	err = dsdev->ops->set_current_states(dsdev->dev, states);
	if (err < 0)
		return err;

	return size;
}
static DEVICE_ATTR_RW(states);

static struct attribute *drive_status_attrs[] = {
	&dev_attr_states.attr,
	NULL
};

ATTRIBUTE_GROUPS(drive_status);

static int drive_status_set_brightness(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	struct drive_status_dev *dsdev;
	int err;

	dsdev = container_of(led_cdev, struct drive_status_dev, led_cdev);
	dsdev->brightness = brightness;

	if (brightness == LED_OFF) {
		err = dsdev->ops->set_current_states(dsdev->dev, 0);
		if (err < 0)
			return err;
	}
	return 0;
}

static enum led_brightness drive_status_get_brightness(struct led_classdev *led_cdev)
{
	struct drive_status_dev *dsdev;

	dsdev = container_of(led_cdev, struct drive_status_dev, led_cdev);
	return dsdev->brightness;
}

static struct drive_status_dev *to_drive_status_dev(struct device *dev)
{
	struct drive_status_dev *dsdev;

	mutex_lock(&drive_list_lock);
	list_for_each_entry(dsdev, &drive_list, drive_list)
		if (dev == dsdev->dev) {
			mutex_unlock(&drive_list_lock);
			return dsdev;
		}
	mutex_unlock(&drive_list_lock);
	return NULL;
}

static void remove_drive_status_dev(struct drive_status_dev *dsdev)
{
	if (dsdev) {
		mutex_lock(&drive_list_lock);
		list_del(&dsdev->drive_list);
		mutex_unlock(&drive_list_lock);
		led_classdev_unregister(&dsdev->led_cdev);
		kfree(dsdev);
	}
}

static void add_drive_status_dev(struct device *dev,
				 struct drive_status_led_ops *ops)
{
	u32 supported_states;
	int ret;
	struct drive_status_dev *dsdev;
	char name[LED_MAX_NAME_SIZE];

	if (to_drive_status_dev(dev))
		/*
		 * led has already been added for this dev
		 */
		return;

	if (ops->get_supported_states(dev, &supported_states) < 0)
		return;

	dsdev = kzalloc(sizeof(*dsdev), GFP_KERNEL);
	if (!dsdev)
		return;

	dsdev->dev = dev;
	dsdev->ops = ops;
	dsdev->supported_states = supported_states;
	dsdev->brightness = LED_ON;
	snprintf(name, sizeof(name), "%s::%s",
		 dev_name(dev), "drive_status");

	dsdev->led_cdev.name = name;
	dsdev->led_cdev.max_brightness = LED_ON;
	dsdev->led_cdev.brightness_set_blocking = drive_status_set_brightness;
	dsdev->led_cdev.brightness_get = drive_status_get_brightness;
	dsdev->led_cdev.groups = drive_status_groups;
	ret = led_classdev_register(dev, &dsdev->led_cdev);
	if (ret) {
		pr_warn("Failed to register LED %s\n", dsdev->led_cdev.name);
		remove_drive_status_dev(dsdev);
		return;
	}

	mutex_lock(&drive_list_lock);
	list_add_tail(&dsdev->drive_list, &drive_list);
	mutex_unlock(&drive_list_lock);
}

/*
 * code specific to PCIe devices
 */
static void probe_pdev(struct pci_dev *pdev)
{
	if (pdev_has_dsm(pdev))
		add_drive_status_dev(&pdev->dev, &dsm_drive_status_led_ops);
}

static int pciessdleds_pci_bus_notifier_cb(struct notifier_block *nb,
					   unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);

	if (action == BUS_NOTIFY_ADD_DEVICE)
		probe_pdev(pdev);
	else if (action == BUS_NOTIFY_DEL_DEVICE)
		remove_drive_status_dev(to_drive_status_dev(&pdev->dev));
	return NOTIFY_DONE;
}

static struct notifier_block pciessdleds_pci_bus_nb = {
	.notifier_call = pciessdleds_pci_bus_notifier_cb,
	.priority = INT_MIN,
};

static void initial_scan_for_leds(void)
{
	struct pci_dev *pdev = NULL;

	for_each_pci_dev(pdev)
		probe_pdev(pdev);
}

static int __init pciessdleds_init(void)
{
	mutex_init(&drive_list_lock);
	INIT_LIST_HEAD(&drive_list);

	bus_register_notifier(&pci_bus_type, &pciessdleds_pci_bus_nb);
	initial_scan_for_leds();
	return 0;
}

static void __exit pciessdleds_exit(void)
{
	struct drive_status_dev *dsdev, *temp;

	bus_unregister_notifier(&pci_bus_type, &pciessdleds_pci_bus_nb);
	list_for_each_entry_safe(dsdev, temp, &drive_list, drive_list)
		remove_drive_status_dev(dsdev);
}

module_init(pciessdleds_init);
module_exit(pciessdleds_exit);

MODULE_AUTHOR("Stuart Hayes <stuart.w.hayes@gmail.com>");
MODULE_DESCRIPTION("Support for PCIe SSD Status LED Management _DSM");
MODULE_LICENSE("GPL");
