// SPDX-License-Identifier: GPL-2.0-only
/*
 * Module to expose interface to control PCIe SSD LEDs as defined in the
 * "_DSM additions for PCIe SSD Status LED Management" ECN to the PCI
 * Firmware Specification Revision 3.2, dated 12 February 2020.
 *
 *  Copyright (c) 2021 Dell Inc.
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
	{ .name = "ok",			.mask = 1 << 2 },
	{ .name = "locate",		.mask = 1 << 3 },
	{ .name = "fail",		.mask = 1 << 4 },
	{ .name = "rebuild",		.mask = 1 << 5 },
	{ .name = "pfa",		.mask = 1 << 6 },
	{ .name = "hotspare",		.mask = 1 << 7 },
	{ .name = "criticalarray",	.mask = 1 << 8 },
	{ .name = "failedarray",	.mask = 1 << 9 },
	{ .name = "invaliddevice",	.mask = 1 << 10 },
	{ .name = "disabled",		.mask = 1 << 11 },
};

/*
 * ssd_status_dev could be the SSD's PCIe dev or its hotplug slot
 */
struct ssd_status_dev {
	struct list_head ssd_list;
	struct pci_dev *pdev;
	u32 supported_states;
	struct led_classdev led_cdev;
	enum led_brightness brightness;
};

struct mutex ssd_list_lock;
struct list_head ssd_list;

const guid_t pcie_ssdleds_dsm_guid =
	GUID_INIT(0x5d524d9d, 0xfff9, 0x4d4b,
		  0x8c, 0xb7, 0x74, 0x7e, 0xd5, 0x1e, 0x19, 0x4d);

#define GET_SUPPORTED_STATES_DSM	0x01
#define GET_STATE_DSM			0x02
#define SET_STATE_DSM			0x03

struct pci_ssdleds_dsm_output {
	u16 status;
	u8 function_specific_err;
	u8 vendor_specific_err;
	u32 state;
};

static void dsm_status_err_print(struct device *dev,
			     struct pci_ssdleds_dsm_output *output)
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
	struct pci_ssdleds_dsm_output *dsm_output;

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

	dsm_output = (struct pci_ssdleds_dsm_output *)out_obj->buffer.pointer;

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
	struct pci_ssdleds_dsm_output *dsm_output;

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

	dsm_output = (struct pci_ssdleds_dsm_output *)out_obj->buffer.pointer;
	if (dsm_output->status != 0) {
		dsm_status_err_print(dev, dsm_output);
		ACPI_FREE(out_obj);
		return -EIO;
	}

	*output = dsm_output->state;
	ACPI_FREE(out_obj);
	return 0;
}

static bool pdev_has_dsm(struct pci_dev *pdev)
{
	acpi_handle handle;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return false;

	return !!acpi_check_dsm(handle, &pcie_ssdleds_dsm_guid, 0x1,
		1 << GET_SUPPORTED_STATES_DSM ||
		1 << GET_STATE_DSM ||
		1 << SET_STATE_DSM);
}

static ssize_t current_states_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct device *dsm_dev = led_cdev->dev->parent;
	struct ssd_status_dev *ssd;
	u32 val;
	int err, i, result = 0;

	ssd = container_of(led_cdev, struct ssd_status_dev, led_cdev);

	err = dsm_get(dsm_dev, GET_STATE_DSM, &val);
	if (err < 0)
		return err;
	for (i = 0; i < ARRAY_SIZE(led_states); i++)
		if (led_states[i].mask & ssd->supported_states)
			result += sprintf(buf + result, "%-25s\t0x%04X [%c]\n",
					  led_states[i].name,
					  led_states[i].mask,
					  (val & led_states[i].mask)
					  ? '*' : ' ');

	result += sprintf(buf + result, "--\ncurrent_states = 0x%04X\n", val);
	return result;
}

static ssize_t current_states_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct device *dsm_dev = led_cdev->dev->parent;
	struct ssd_status_dev *ssd;
	u32 val;
	int err;

	ssd = container_of(led_cdev, struct ssd_status_dev, led_cdev);

	err = kstrtou32(buf, 10, &val);
	if (err)
		return err;

	val &= ssd->supported_states;
	if (val)
		ssd->brightness = LED_ON;
	err = dsm_set(dsm_dev, val);
	if (err < 0)
		return err;

	return size;
}

static ssize_t supported_states_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct ssd_status_dev *ssd;
	int i, result = 0;

	ssd = container_of(led_cdev, struct ssd_status_dev, led_cdev);

	for (i = 0; i < ARRAY_SIZE(led_states); i++)
		result += sprintf(buf + result, "%-25s\t0x%04X [%c]\n",
				  led_states[i].name,
				  led_states[i].mask,
				  (ssd->supported_states & led_states[i].mask)
				  ? '*' : ' ');

	result += sprintf(buf + result, "--\nsupported_states = 0x%04X\n",
			  ssd->supported_states);
	return result;
}

static DEVICE_ATTR_RW(current_states);
static DEVICE_ATTR_RO(supported_states);

static struct attribute *pcie_ssd_status_attrs[] = {
	&dev_attr_current_states.attr,
	&dev_attr_supported_states.attr,
	NULL
};

ATTRIBUTE_GROUPS(pcie_ssd_status);

static int ssdleds_set_brightness(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	struct device *dsm_dev = led_cdev->dev->parent;
	struct ssd_status_dev *ssd;
	int err;

	ssd = container_of(led_cdev, struct ssd_status_dev, led_cdev);
	ssd->brightness = brightness;

	if (brightness == LED_OFF) {
		err = dsm_set(dsm_dev, 0);
		if (err < 0)
			return err;
	}
	return 0;
}

static enum led_brightness ssdleds_get_brightness(struct led_classdev *led_cdev)
{
	struct ssd_status_dev *ssd;

	ssd = container_of(led_cdev, struct ssd_status_dev, led_cdev);
	return ssd->brightness;
}

static void remove_ssd(struct ssd_status_dev *ssd)
{
	mutex_lock(&ssd_list_lock);
	list_del(&ssd->ssd_list);
	mutex_unlock(&ssd_list_lock);
	led_classdev_unregister(&ssd->led_cdev);
	kfree(ssd);
}

static struct ssd_status_dev *pci_dev_to_ssd_status_dev(struct pci_dev *pdev)
{
	struct ssd_status_dev *ssd;

	mutex_lock(&ssd_list_lock);
	list_for_each_entry(ssd, &ssd_list, ssd_list)
		if (pdev == ssd->pdev) {
			mutex_unlock(&ssd_list_lock);
			return ssd;
		}
	mutex_unlock(&ssd_list_lock);
	return NULL;
}

static void remove_ssd_for_pdev(struct pci_dev *pdev)
{
	struct ssd_status_dev *ssd = pci_dev_to_ssd_status_dev(pdev);

	if (ssd)
		remove_ssd(ssd);
}

static void add_ssd(struct pci_dev *pdev)
{
	u32 supported_states;
	int ret;
	struct ssd_status_dev *ssd;
	char name[LED_MAX_NAME_SIZE];

	if (dsm_get(&pdev->dev, GET_SUPPORTED_STATES_DSM, &supported_states) < 0)
		return;

	ssd = kzalloc(sizeof(*ssd), GFP_KERNEL);
	if (!ssd)
		return;

	ssd->pdev = pdev;
	ssd->supported_states = supported_states;
	ssd->brightness = LED_ON;
	snprintf(name, sizeof(name), "%s::%s",
		 dev_name(&pdev->dev), "pcie_ssd_status");
	ssd->led_cdev.name = name;
	ssd->led_cdev.max_brightness = LED_ON;
	ssd->led_cdev.brightness_set_blocking = ssdleds_set_brightness;
	ssd->led_cdev.brightness_get = ssdleds_get_brightness;
	ssd->led_cdev.groups = pcie_ssd_status_groups;

	ret = led_classdev_register(&pdev->dev, &ssd->led_cdev);
	if (ret) {
		pr_warn("Failed to register LED %s\n", ssd->led_cdev.name);
		remove_ssd(ssd);
		return;
	}
	mutex_lock(&ssd_list_lock);
	list_add_tail(&ssd->ssd_list, &ssd_list);
	mutex_unlock(&ssd_list_lock);
}

static void probe_pdev(struct pci_dev *pdev)
{
	if (pci_dev_to_ssd_status_dev(pdev))
		/*
		 * leds have already been added for this pdev
		 */
		return;

	if (pdev_has_dsm(pdev))
		add_ssd(pdev);
}

static int pciessdleds_pci_bus_notifier_cb(struct notifier_block *nb,
					   unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);

	if (action == BUS_NOTIFY_ADD_DEVICE)
		probe_pdev(pdev);
	else if (action == BUS_NOTIFY_REMOVED_DEVICE)
		remove_ssd_for_pdev(pdev);
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
	mutex_init(&ssd_list_lock);
	INIT_LIST_HEAD(&ssd_list);

	bus_register_notifier(&pci_bus_type, &pciessdleds_pci_bus_nb);
	initial_scan_for_leds();
	return 0;
}

static void __exit pciessdleds_exit(void)
{
	struct ssd_status_dev *ssd, *temp;

	bus_unregister_notifier(&pci_bus_type, &pciessdleds_pci_bus_nb);
	list_for_each_entry_safe(ssd, temp, &ssd_list, ssd_list)
		remove_ssd(ssd);
}

module_init(pciessdleds_init);
module_exit(pciessdleds_exit);

MODULE_AUTHOR("Stuart Hayes <stuart.w.hayes@gmail.com>");
MODULE_DESCRIPTION("Support for PCIe SSD Status LED Management _DSM");
MODULE_LICENSE("GPL");
