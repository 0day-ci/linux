// SPDX-License-Identifier: GPL-2.0-only
/*
 * Module to provide LED interfaces for PCIe SSD status LED states, as
 * defined in the "_DSM additions for PCIe SSD Status LED Management" ECN
 * to the PCI Firmware Specification Revision 3.2, dated 12 February 2020.
 *
 * The "_DSM..." spec is functionally similar to Native PCIe Enclosure
 * Management, but uses a _DSM ACPI method rather than a PCIe extended
 * capability.
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
	int bit;
};

static struct led_state led_states[] = {
	{ .name = "ok",		.bit = 2 },
	{ .name = "locate",	.bit = 3 },
	{ .name = "failed",	.bit = 4 },
	{ .name = "rebuild",	.bit = 5 },
	{ .name = "pfa",	.bit = 6 },
	{ .name = "hotspare",	.bit = 7 },
	{ .name = "ica",	.bit = 8 },
	{ .name = "ifa",	.bit = 9 },
	{ .name = "invalid",	.bit = 10 },
	{ .name = "disabled",	.bit = 11 },
};

struct drive_status_led_ops {
	int (*get_supported_states)(struct pci_dev *pdev, u32 *states);
	int (*get_current_states)(struct pci_dev *pdev, u32 *states);
	int (*set_current_states)(struct pci_dev *pdev, u32 states);
};

struct drive_status_state_led {
	struct led_classdev cdev;
	struct drive_status_dev *dsdev;
	int bit;
};

/*
 * drive_status_dev->dev could be the drive itself or its PCIe port
 */
struct drive_status_dev {
	struct list_head list;
	/* PCI device that has the LED controls */
	struct pci_dev *pdev;
	/* _DSM (or NPEM) LED ops */
	struct drive_status_led_ops *ops;
	/* currently active states */
	u32 states;
	int num_leds;
	struct drive_status_state_led leds[];
};

struct mutex drive_status_dev_list_lock;
struct list_head drive_status_dev_list;

/*
 * _DSM LED control
 */
const guid_t pcie_ssd_leds_dsm_guid =
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

static void dsm_status_err_print(struct pci_dev *pdev,
				 struct ssdleds_dsm_output *output)
{
	switch (output->status) {
	case 0:
		break;
	case 1:
		pci_dbg(pdev, "_DSM not supported\n");
		break;
	case 2:
		pci_dbg(pdev, "_DSM invalid input parameters\n");
		break;
	case 3:
		pci_dbg(pdev, "_DSM communication error\n");
		break;
	case 4:
		pci_dbg(pdev, "_DSM function-specific error 0x%x\n",
			output->function_specific_err);
		break;
	case 5:
		pci_dbg(pdev, "_DSM vendor-specific error 0x%x\n",
			output->vendor_specific_err);
		break;
	default:
		pci_dbg(pdev, "_DSM returned unknown status 0x%x\n",
			output->status);
	}
}

static int dsm_set(struct pci_dev *pdev, u32 value)
{
	acpi_handle handle;
	union acpi_object *out_obj, arg3[2];
	struct ssdleds_dsm_output *dsm_output;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	arg3[0].type = ACPI_TYPE_PACKAGE;
	arg3[0].package.count = 1;
	arg3[0].package.elements = &arg3[1];

	arg3[1].type = ACPI_TYPE_BUFFER;
	arg3[1].buffer.length = 4;
	arg3[1].buffer.pointer = (u8 *)&value;

	out_obj = acpi_evaluate_dsm_typed(handle, &pcie_ssd_leds_dsm_guid,
				1, SET_STATE_DSM, &arg3[0], ACPI_TYPE_BUFFER);
	if (!out_obj)
		return -EIO;

	if (out_obj->buffer.length < 8) {
		ACPI_FREE(out_obj);
		return -EIO;
	}

	dsm_output = (struct ssdleds_dsm_output *)out_obj->buffer.pointer;

	if (dsm_output->status != 0) {
		dsm_status_err_print(pdev, dsm_output);
		ACPI_FREE(out_obj);
		return -EIO;
	}
	ACPI_FREE(out_obj);
	return 0;
}

static int dsm_get(struct pci_dev *pdev, u64 dsm_func, u32 *output)
{
	acpi_handle handle;
	union acpi_object *out_obj;
	struct ssdleds_dsm_output *dsm_output;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	out_obj = acpi_evaluate_dsm_typed(handle, &pcie_ssd_leds_dsm_guid, 0x1,
					  dsm_func, NULL, ACPI_TYPE_BUFFER);
	if (!out_obj)
		return -EIO;

	if (out_obj->buffer.length < 8) {
		ACPI_FREE(out_obj);
		return -EIO;
	}

	dsm_output = (struct ssdleds_dsm_output *)out_obj->buffer.pointer;
	if (dsm_output->status != 0) {
		dsm_status_err_print(pdev, dsm_output);
		ACPI_FREE(out_obj);
		return -EIO;
	}

	*output = dsm_output->state;
	ACPI_FREE(out_obj);
	return 0;
}

static int get_supported_states_dsm(struct pci_dev *pdev, u32 *states)
{
	return dsm_get(pdev, GET_SUPPORTED_STATES_DSM, states);
}

static int get_current_states_dsm(struct pci_dev *pdev, u32 *states)
{
	return dsm_get(pdev, GET_STATE_DSM, states);
}

static int set_current_states_dsm(struct pci_dev *pdev, u32 states)
{
	return dsm_set(pdev, states);
}

static bool pdev_has_dsm(struct pci_dev *pdev)
{
	acpi_handle handle;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return false;

	return acpi_check_dsm(handle, &pcie_ssd_leds_dsm_guid, 0x1,
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

static int set_brightness(struct led_classdev *led_cdev,
				       enum led_brightness brightness)
{
	struct drive_status_state_led *led;
	int err;

	led = container_of(led_cdev, struct drive_status_state_led, cdev);

	if (brightness == LED_OFF)
		clear_bit(led->bit, (unsigned long *)&(led->dsdev->states));
	else
		set_bit(led->bit, (unsigned long *)&(led->dsdev->states));
	err = led->dsdev->ops->set_current_states(led->dsdev->pdev,
						  led->dsdev->states);
	if (err < 0)
		return err;
	return 0;
}

static enum led_brightness get_brightness(struct led_classdev *led_cdev)
{
	struct drive_status_state_led *led;

	led = container_of(led_cdev, struct drive_status_state_led, cdev);
	return test_bit(led->bit, (unsigned long *)&led->dsdev->states)
		? LED_ON : LED_OFF;
}

static struct drive_status_dev *to_drive_status_dev(struct pci_dev *pdev)
{
	struct drive_status_dev *dsdev;

	mutex_lock(&drive_status_dev_list_lock);
	list_for_each_entry(dsdev, &drive_status_dev_list, list)
		if (pdev == dsdev->pdev) {
			mutex_unlock(&drive_status_dev_list_lock);
			return dsdev;
		}
	mutex_unlock(&drive_status_dev_list_lock);
	return NULL;
}

static void remove_drive_status_dev(struct drive_status_dev *dsdev)
{
	if (dsdev) {
		int i;

		mutex_lock(&drive_status_dev_list_lock);
		list_del(&dsdev->list);
		mutex_unlock(&drive_status_dev_list_lock);
		for (i = 0; i < dsdev->num_leds; i++)
			led_classdev_unregister(&dsdev->leds[i].cdev);
		kfree(dsdev);
	}
}

static void add_drive_status_dev(struct pci_dev *pdev,
				 struct drive_status_led_ops *ops)
{
	u32 supported;
	int ret, num_leds, i;
	struct drive_status_dev *dsdev;
	char name[LED_MAX_NAME_SIZE];
	struct drive_status_state_led *led;

	if (to_drive_status_dev(pdev))
		/*
		 * leds have already been added for this dev
		 */
		return;

	if (ops->get_supported_states(pdev, &supported) < 0)
		return;
	num_leds = hweight32(supported);
	if (num_leds == 0)
		return;

	dsdev = kzalloc(struct_size(dsdev, leds, num_leds), GFP_KERNEL);
	if (!dsdev)
		return;

	dsdev->num_leds = 0;
	dsdev->pdev = pdev;
	dsdev->ops = ops;
	dsdev->states = 0;
	if (ops->set_current_states(pdev, dsdev->states)) {
		kfree(dsdev);
		return;
	}
	INIT_LIST_HEAD(&dsdev->list);
	/*
	 * add LEDs only for supported states
	 */
	for (i = 0; i < ARRAY_SIZE(led_states); i++) {
		if (!test_bit(led_states[i].bit, (unsigned long *)&supported))
			continue;

		led = &dsdev->leds[dsdev->num_leds];
		led->dsdev = dsdev;
		led->bit = led_states[i].bit;

		snprintf(name, sizeof(name), "%s::%s",
			 pci_name(pdev), led_states[i].name);
		led->cdev.name = name;
		led->cdev.max_brightness = LED_ON;
		led->cdev.brightness_set_blocking = set_brightness;
		led->cdev.brightness_get = get_brightness;
		ret = 0;
		ret = led_classdev_register(&pdev->dev, &led->cdev);
		if (ret) {
			pr_warn("Failed to register LEDs for %s\n", pci_name(pdev));
			remove_drive_status_dev(dsdev);
			return;
		}
		dsdev->num_leds++;
	}

	mutex_lock(&drive_status_dev_list_lock);
	list_add_tail(&dsdev->list, &drive_status_dev_list);
	mutex_unlock(&drive_status_dev_list_lock);
}

/*
 * code specific to PCIe devices
 */
static void probe_pdev(struct pci_dev *pdev)
{
	/*
	 * This is only supported on PCIe storage devices and PCIe ports
	 */
	if (pdev->class != PCI_CLASS_STORAGE_EXPRESS &&
	    pdev->class != PCI_CLASS_BRIDGE_PCI)
		return;
	if (pdev_has_dsm(pdev))
		add_drive_status_dev(pdev, &dsm_drive_status_led_ops);
}

static int ssd_leds_pci_bus_notifier_cb(struct notifier_block *nb,
					   unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);

	if (action == BUS_NOTIFY_ADD_DEVICE)
		probe_pdev(pdev);
	else if (action == BUS_NOTIFY_DEL_DEVICE)
		remove_drive_status_dev(to_drive_status_dev(pdev));
	return NOTIFY_DONE;
}

static struct notifier_block ssd_leds_pci_bus_nb = {
	.notifier_call = ssd_leds_pci_bus_notifier_cb,
	.priority = INT_MIN,
};

static void initial_scan_for_leds(void)
{
	struct pci_dev *pdev = NULL;

	for_each_pci_dev(pdev)
		probe_pdev(pdev);
}

static int __init ssd_leds_init(void)
{
	mutex_init(&drive_status_dev_list_lock);
	INIT_LIST_HEAD(&drive_status_dev_list);

	bus_register_notifier(&pci_bus_type, &ssd_leds_pci_bus_nb);
	initial_scan_for_leds();
	return 0;
}

static void __exit ssd_leds_exit(void)
{
	struct drive_status_dev *dsdev, *temp;

	bus_unregister_notifier(&pci_bus_type, &ssd_leds_pci_bus_nb);
	list_for_each_entry_safe(dsdev, temp, &drive_status_dev_list, list)
		remove_drive_status_dev(dsdev);
}

module_init(ssd_leds_init);
module_exit(ssd_leds_exit);

MODULE_AUTHOR("Stuart Hayes <stuart.w.hayes@gmail.com>");
MODULE_DESCRIPTION("Support for PCIe SSD Status LEDs");
MODULE_LICENSE("GPL");
