// SPDX-License-Identifier: GPL-2.0
/*
 * Confidential computing (coco) secret area handling
 *
 * Copyright (C) 2021 IBM Corporation
 * Author: Dov Murik <dovmurik@linux.ibm.com>
 */

#define pr_fmt(fmt) "efi: " fmt

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kmod.h>

#ifdef CONFIG_EFI_SECRET_MODULE

/*
 * Load the efi_secret module if the EFI secret area is populated
 */
static int __init load_efi_secret_module(void)
{
	struct linux_efi_coco_secret_area *area;
	efi_guid_t *header_guid;
	int ret = 0;

	if (efi.coco_secret == EFI_INVALID_TABLE_ADDR)
		return 0;

	area = memremap(efi.coco_secret, sizeof(*area), MEMREMAP_WB);
	if (!area) {
		pr_err("Failed to map confidential computing secret area descriptor\n");
		return -ENOMEM;
	}
	if (!area->base_pa || area->size < sizeof(*header_guid))
		goto unmap_desc;

	header_guid = (void __force *)ioremap_encrypted(area->base_pa, sizeof(*header_guid));
	if (!header_guid) {
		pr_err("Failed to map secret area\n");
		ret = -ENOMEM;
		goto unmap_desc;
	}
	if (efi_guidcmp(*header_guid, EFI_SECRET_TABLE_HEADER_GUID))
		goto unmap_encrypted;

	ret = request_module("efi_secret");

unmap_encrypted:
	iounmap((void __iomem *)header_guid);

unmap_desc:
	memunmap(area);
	return ret;
}
late_initcall(load_efi_secret_module);

#endif
