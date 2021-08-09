// SPDX-License-Identifier: GPL-2.0
/*
 * Confidential computing (coco) secret area handling
 *
 * Copyright (C) 2021 IBM Corporation
 * Author: Dov Murik <dovmurik@linux.ibm.com>
 */

#include <linux/efi.h>
#include <linux/sizes.h>
#include <asm/efi.h>

#include "efistub.h"

#define LINUX_EFI_COCO_SECRET_TABLE_GUID                                                           \
	EFI_GUID(0xadf956ad, 0xe98c, 0x484c, 0xae, 0x11, 0xb5, 0x1c, 0x7d, 0x33, 0x64, 0x47)

/**
 * struct efi_coco_secret_table - EFI config table that points to the
 * confidential computing secret area. The guid
 * LINUX_EFI_COCO_SECRET_TABLE_GUID holds this table.
 * @base:	Physical address of the EFI secret area
 * @size:	Size (in bytes) of the EFI secret area
 */
struct efi_coco_secret_table {
	u64 base;
	u64 size;
} __attribute((packed));

/*
 * Create a copy of EFI's confidential computing secret area (if available) so
 * that the secrets are accessible in the kernel after ExitBootServices.
 */
void efi_copy_coco_secret_area(void)
{
	efi_guid_t linux_secret_area_guid = LINUX_EFI_COCO_SECRET_AREA_GUID;
	efi_status_t status;
	struct efi_coco_secret_table *secret_table;
	struct linux_efi_coco_secret_area *secret_area;

	secret_table = get_efi_config_table(LINUX_EFI_COCO_SECRET_TABLE_GUID);
	if (!secret_table)
		return;

	if (secret_table->size == 0 || secret_table->size >= SZ_4G)
		return;

	/* Allocate space for the secret area and copy it */
	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
			     sizeof(*secret_area) + secret_table->size, (void **)&secret_area);

	if (status != EFI_SUCCESS) {
		efi_err("Unable to allocate memory for confidential computing secret area copy\n");
		return;
	}

	secret_area->size = secret_table->size;
	memcpy(secret_area->area, (void *)(unsigned long)secret_table->base, secret_table->size);

	status = efi_bs_call(install_configuration_table, &linux_secret_area_guid, secret_area);
	if (status != EFI_SUCCESS)
		goto err_free;

	return;

err_free:
	efi_bs_call(free_pool, secret_area);
}
