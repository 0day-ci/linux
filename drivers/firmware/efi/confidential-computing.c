// SPDX-License-Identifier: GPL-2.0
/*
 * Confidential computing secret area handling
 *
 * Copyright (C) 2021 IBM Corporation
 * Author: Dov Murik <dovmurik@linux.ibm.com>
 */

#define pr_fmt(fmt) "efi: " fmt

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <asm/early_ioremap.h>

/*
 * Reserve the confidential computing secret area memory
 */
int __init efi_confidential_computing_secret_area_reserve(void)
{
	struct linux_efi_confidential_computing_secret_area *secret_area;
	unsigned long secret_area_size;

	if (efi.confidential_computing_secret == EFI_INVALID_TABLE_ADDR)
		return 0;

	secret_area = early_memremap(efi.confidential_computing_secret, sizeof(*secret_area));
	if (!secret_area) {
		pr_err("Failed to map confidential computing secret area\n");
		efi.confidential_computing_secret = EFI_INVALID_TABLE_ADDR;
		return -ENOMEM;
	}

	secret_area_size = sizeof(*secret_area) + secret_area->size;
	memblock_reserve(efi.confidential_computing_secret, secret_area_size);

	pr_info("Reserved memory of EFI-provided confidential computing secret area");

	early_memunmap(secret_area, sizeof(*secret_area));
	return 0;
}
