// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual Machine Generation ID driver
 *
 * Copyright (C) 2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2020 Amazon. All rights reserved.
 * Copyright (C) 2018 Red Hat Inc. All rights reserved.
 */

#define pr_fmt(fmt) "vmgenid: " fmt

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/uuid.h>

#define DEV_NAME "vmgenid"
ACPI_MODULE_NAME(DEV_NAME);

static struct {
	uuid_t uuid;
	void *uuid_iomap;
} vmgenid_data;

static int vmgenid_acpi_map(acpi_handle handle)
{
	phys_addr_t phys_addr = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	union acpi_object *pss;
	union acpi_object *element;
	int i;

	status = acpi_evaluate_object(handle, "ADDR", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating ADDR"));
		return -ENODEV;
	}
	pss = buffer.pointer;
	if (!pss || pss->type != ACPI_TYPE_PACKAGE || pss->package.count != 2)
		return -EINVAL;

	for (i = 0; i < pss->package.count; ++i) {
		element = &pss->package.elements[i];
		if (element->type != ACPI_TYPE_INTEGER)
			return -EINVAL;
		phys_addr |= element->integer.value << i * 32;
	}

	vmgenid_data.uuid_iomap = acpi_os_map_memory(phys_addr, sizeof(vmgenid_data.uuid));
	if (!vmgenid_data.uuid_iomap) {
		pr_err("failed to map memory at %pa, size %zu\n",
			&phys_addr, sizeof(vmgenid_data.uuid));
		return -ENOMEM;
	}

	memcpy_fromio(&vmgenid_data.uuid, vmgenid_data.uuid_iomap, sizeof(vmgenid_data.uuid));

	return 0;
}

static int vmgenid_acpi_add(struct acpi_device *device)
{
	int ret;

	if (!device)
		return -EINVAL;
	ret = vmgenid_acpi_map(device->handle);
	if (ret < 0) {
		pr_err("failed to map acpi device: %d\n", ret);
		return ret;
	}
	device->driver_data = &vmgenid_data;
	add_device_randomness(&vmgenid_data.uuid, sizeof(vmgenid_data.uuid));
	return 0;
}

static int vmgenid_acpi_remove(struct acpi_device *device)
{
	if (!device || acpi_driver_data(device) != &vmgenid_data)
		return -EINVAL;
	device->driver_data = NULL;
	if (vmgenid_data.uuid_iomap)
		acpi_os_unmap_memory(vmgenid_data.uuid_iomap, sizeof(vmgenid_data.uuid));
	vmgenid_data.uuid_iomap = NULL;
	return 0;
}

static void vmgenid_acpi_notify(struct acpi_device *device, u32 event)
{
	uuid_t old_uuid = vmgenid_data.uuid;

	if (!device || acpi_driver_data(device) != &vmgenid_data)
		return;
	memcpy_fromio(&vmgenid_data.uuid, vmgenid_data.uuid_iomap, sizeof(vmgenid_data.uuid));
	if (!memcmp(&old_uuid, &vmgenid_data.uuid, sizeof(vmgenid_data.uuid)))
		return;
	add_vmfork_randomness(&vmgenid_data.uuid, sizeof(vmgenid_data.uuid));
}

static const struct acpi_device_id vmgenid_ids[] = {
	{"VMGENID", 0},
	{"QEMUVGID", 0},
	{"", 0},
};

static struct acpi_driver acpi_vmgenid_driver = {
	.name = "vm_generation_id",
	.ids = vmgenid_ids,
	.owner = THIS_MODULE,
	.ops = {
		.add = vmgenid_acpi_add,
		.remove = vmgenid_acpi_remove,
		.notify = vmgenid_acpi_notify,
	}
};

static int __init vmgenid_init(void)
{
	return acpi_bus_register_driver(&acpi_vmgenid_driver);
}

static void __exit vmgenid_exit(void)
{
	acpi_bus_unregister_driver(&acpi_vmgenid_driver);
}

module_init(vmgenid_init);
module_exit(vmgenid_exit);

MODULE_DESCRIPTION("Virtual Machine Generation ID");
MODULE_LICENSE("GPL");
